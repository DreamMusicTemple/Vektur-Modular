// Kernel.cpp — see Kernel.hpp. Everything reachable from render() is
// allocation-free, lock-free and bounded in time.
#include "Kernel.hpp"
#include "ParamTable.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace vektur {

namespace {
const uint64_t kMasterVolume  = VK_P_GLOBAL_MASTER_VOLUME;
const uint64_t kMasterTune    = VK_P_GLOBAL_MASTER_TUNE;
const uint64_t kPolyphony     = VK_P_GLOBAL_POLYPHONY;
const uint64_t kBendRange     = VK_P_GLOBAL_PITCH_BEND_RANGE;

constexpr uint64_t kPerf    = VK_P_GLOBAL_VOICE_MODE;            // global indices 10–16
constexpr uint64_t kMatrix  = 60ull * VK_PARAMS_PER_MODULE;
constexpr uint64_t kLfoBase = 26ull * VK_PARAMS_PER_MODULE;
constexpr uint64_t kMacro   = 29ull * VK_PARAMS_PER_MODULE;
constexpr uint64_t kZoneMode = VK_P_GLOBAL_ZONE_MODE, kSplitPoint = VK_P_GLOBAL_SPLIT_POINT;
constexpr uint64_t kZoneATranspose = VK_P_GLOBAL_ZONE_A_TRANSPOSE, kZoneBTranspose = VK_P_GLOBAL_ZONE_B_TRANSPOSE;
constexpr uint64_t kMpeRange = VK_P_GLOBAL_MPE_PITCH_RANGE;

inline float dbToGainSafe(float db) noexcept { return db <= -60.f ? 0.f : std::pow(10.f, db / 20.f); }
constexpr float kSafetyCeiling = 2.0f;   // ≈ +6 dBFS hard ceiling, last line of defence
} // namespace

Kernel::Kernel() {
    for (const auto& d : kParamTable)
        params_.registerParam({d.address, d.minValue, d.maxValue, d.defaultValue, d.smoothingMs, d.flags});
    for (auto& l : levels_) l.store(0.f, std::memory_order_relaxed);
    // Targets of the "all filter cutoffs" virtual destination.
    for (uint64_t a : {static_cast<uint64_t>(VK_P_VCFA_CUTOFF), static_cast<uint64_t>(VK_P_VCFB_CUTOFF),
                       static_cast<uint64_t>(VK_P_OTA_CUTOFF), static_cast<uint64_t>(VK_P_DIODELADDER_CUTOFF),
                       static_cast<uint64_t>(VK_P_TRANSISTORLADDER_CUTOFF), static_cast<uint64_t>(VK_P_VCBPF_CENTRE),
                       static_cast<uint64_t>(VK_P_COMB_FREQUENCY)}) {
        const int32_t slot = params_.slotOf(a);
        if (slot >= 0 && modFrame_.cutoffCount < kMaxCutoffSlots) modFrame_.cutoffSlots[modFrame_.cutoffCount++] = slot;
    }
    ctl_[MS_EXPRESSION] = 1.f;    // MIDI convention: expression rests at full
    ctl_[MS_CONSTANT] = 1.f;
    // Start with the normalled (unpatched) voice.
    VKPatchResult r;
    compilePatch(nullptr, 0, graphs_.writeSlot(), r);
    graphs_.publish();
    graphs_.acquire();
}

int32_t Kernel::setPatch(const VKCable* cables, uint32_t count, VKPatchResult& result) noexcept {
    const int32_t rc = compilePatch(cables, count, graphs_.writeSlot(), result);
    graphs_.publish();
    return rc;
}

int Kernel::allocate(double sampleRate, uint32_t channelCount, uint32_t maxFrames) {
    if (sampleRate < 8000.0 || sampleRate > 768000.0) return -1;
    if (channelCount == 0 || channelCount > VK_MAX_CHANNELS) return -2;
    if (maxFrames == 0 || maxFrames > 16384) return -3;

    sampleRate_ = sampleRate;
    channelCount_ = channelCount;
    maxFrames_ = maxFrames;
    busL_.assign(maxFrames, 0.f);
    busR_.assign(maxFrames, 0.f);
    for (auto& s : scratch_) s.assign(maxFrames, 0.f);
    params_.prepare(sampleRate);
    for (uint32_t i = 0; i < VK_MAX_VOICES; ++i) voices_[i].prepare(sampleRate, i);
    sampleHold_.prepare(sampleRate);
    sampleHold_.reset();
    lastNotePitch_ = 60.f;
    swapPhase_ = 0; swapGain_ = 1.f;
    graphs_.acquire();
    buildGlobalContext(0);
    updateGraphRuntime();
    for (uint32_t e = 0; e < kMaxEdges; ++e) rt_.gainPrev[e] = rt_.gain[e];

    commands_.drain();
    noteCounter_ = 0;
    sampleTime_ = 0;
    pitchBendNorm_ = 0.f;
    sustainDown_ = false;
    pendingFlags_ = 0;
    gainCurrent_ = dbToGainSafe(params_.smoothed(kMasterVolume));
    applyPolyphony();
    clearGenerators();
    clockBeat_ = 0.0; clockFollowing_ = false; arpMode_ = 0; consumed_ = false;
    params_.pollChanges();
    fx_.allocate(static_cast<float>(sampleRate), maxFrames, params_);
    captured_.drain();
    clk_ = ClockState{};
    for (auto& c : chan_) c = ChannelState{};
    midiOutCount_ = 0; clockOutOn_ = false; clockOutResync_ = true; hiResVel_ = -1.f;
    allocated_ = true;
    return 0;
}

void Kernel::deallocate() {
    allocated_ = false;
    busL_.clear(); busL_.shrink_to_fit();
    busR_.clear(); busR_.shrink_to_fit();
    for (auto& s : scratch_) { s.clear(); s.shrink_to_fit(); }
    for (auto& v : voices_) v.reset();
}

uint32_t Kernel::activeVoiceCount() const noexcept {
    uint32_t n = 0;
    for (const auto& v : voices_) n += v.isIdle() ? 0u : 1u;
    return n;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void Kernel::render(float* const* channels, uint32_t channelCount, uint32_t frames,
                    const VKEvent* events, uint32_t eventCount) noexcept {
    const auto t0 = std::chrono::steady_clock::now();
    channelCount = std::min(channelCount, channelCount_);

    if (!allocated_ || frames == 0 || frames > maxFrames_) {
        for (uint32_t c = 0; c < channelCount; ++c)
            if (channels[c]) std::memset(channels[c], 0, sizeof(float) * frames);
        return;
    }

    VKCommand cmd;
    while (commands_.pop(cmd)) handleCommand(cmd);
    midiOutCount_ = 0;
    if (mappings_.acquire()) for (auto& st : mapState_) st = midi::MappingState{};
    // Musical time. Sync source Auto: the host's context when supplied this
    // call, else incoming MIDI clock, else the internal tempo with a stopped
    // transport (beat position keeps counting).
    const long sync = std::lround(params_.get(VK_P_GLOBAL_SYNC_SOURCE));
    const bool clockOk = clockActive();
    double tolerance = 0.01;
    if (hostValid_ && sync == 0) { tempo_ = hostTempo_; beatPos_ = hostBeat_; playing_ = hostPlaying_; syncInEffect_ = 1; }
    else if (clockOk && sync != 1) { tempo_ = clockTempo(); beatPos_ = clockBeatNow(); playing_ = clk_.running; syncInEffect_ = 2; tolerance = 0.1; }
    else { tempo_ = std::clamp(static_cast<double>(params_.get(VK_P_GLOBAL_TEMPO)), 20.0, 300.0); playing_ = false; syncInEffect_ = 0; }
    if (!hostValid_ || sync != 0) { beatsPerBar_ = 4.0; barOrigin_ = 0.0; }
    hostValid_ = false;
    // Generators' clock follows the host while its transport moves; a jump
    // (loop, locate) or a transport start re-aligns them.
    if (playing_) {
        if (!clockFollowing_ || std::fabs(beatPos_ - clockBeat_) > tolerance) { relocateGenerators(beatPos_); clockOutResync_ = true; }
        clockBeat_ = beatPos_;
        clockFollowing_ = true;
    } else clockFollowing_ = false;
    // Structural parameters (polyphony) must be current before any note
    // event in this buffer is allocated a voice.
    params_.pollChanges();
    applyPolyphony();
    applyVoiceMode();   // a mode change resets the voices, so it must precede this buffer's notes

    uint32_t e = 0;
    uint32_t pos = 0;
    while (pos < frames) {
        // Apply every event due at (or before) `pos`.
        spanPos_ = pos;
        while (e < eventCount && events[e].sampleOffset <= pos) {
            const VKEvent& ev = events[e++];
            eventSample_ = sampleTime_ + ev.sampleOffset;
            switch (ev.type) {
                case VK_EVENT_MIDI1:      handleMidi(ev.data, ev.length); break;
                case VK_EVENT_UMP:        handleUmp(ev.ump[0], ev.ump[1]); break;
                case VK_EVENT_PARAM:
                    params_.applyEvent(ev.address, ev.value, 0, false);
                    if (ev.address == kPolyphony) applyPolyphony();
                    break;
                case VK_EVENT_PARAM_RAMP:
                    params_.applyEvent(ev.address, ev.value, ev.rampFrames, true);
                    if (ev.address == kPolyphony) applyPolyphony();
                    break;
                default: break;
            }
        }
        // Arpeggiator, sequencer and generative notes due at `pos`.
        runGenerators();
        // Render up to the next event, generator note or control-block boundary.
        uint32_t end = std::min(frames, pos + VK_CONTROL_BLOCK);
        if (e < eventCount) end = std::min(end, std::max(events[e].sampleOffset, pos + 1));
        end = std::min(end, pos + std::max(framesToNextGeneratorEvent(), 1u));
        const double spanBeat = clockBeat_;
        renderSpan(channels, channelCount, pos, end - pos);
        clockOut(spanBeat, pos, end - pos);
        pos = end;
    }
    // Events stamped at/after the end of the buffer are applied now rather
    // than lost (hosts should not send them, but some do).
    spanPos_ = frames > 0 ? frames - 1 : 0;
    while (e < eventCount) {
        const VKEvent& ev = events[e++];
        eventSample_ = sampleTime_ + ev.sampleOffset;
        if (ev.type == VK_EVENT_MIDI1) handleMidi(ev.data, ev.length);
        else if (ev.type == VK_EVENT_UMP) handleUmp(ev.ump[0], ev.ump[1]);
        else params_.applyEvent(ev.address, ev.value, ev.rampFrames, ev.type == VK_EVENT_PARAM_RAMP);
    }

    if (midiDirtyPending_) { midiDirtyPending_ = false; midiUi_.push(VKMidiUIEvent{VK_MIDI_UI_PARAMS_CHANGED, 0, 0, 0, 0, 0}); }
    if (recorder_.isRecording()) recorder_.write(channels[0], channels[channelCount > 1 ? 1 : 0], frames);
    const auto t1 = std::chrono::steady_clock::now();
    publishPerformance();
    publishTelemetry(channels, channelCount, frames,
                     static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    sampleTime_ += frames;
}

void Kernel::renderSpan(float* const* channels, uint32_t channelCount, uint32_t offset, uint32_t frames) noexcept {
    params_.pollChanges();
    applyPolyphony();
    applyVoiceMode();

    // Patch swap: fade the bus out (~1.3 ms), adopt the new graph, fade back in.
    constexpr float kSwapFrames = 64.f;
    if (swapPhase_ == 0 && graphs_.hasPending()) swapPhase_ = 1;
    if (swapPhase_ == 1 && swapGain_ <= 0.f) {
        graphs_.acquire();
        for (auto& v : voices_) v.clearFeedback();
        updateGraphRuntime();
        for (uint32_t e = 0; e < kMaxEdges; ++e) rt_.gainPrev[e] = rt_.gain[e];
        swapPhase_ = 2;
    }

    buildGlobalContext(frames);
    updateGraphRuntime();
    buildModFrame(frames);

    float* L = busL_.data() + offset;
    float* R = busR_.data() + offset;
    std::memset(L, 0, sizeof(float) * frames);
    std::memset(R, 0, sizeof(float) * frames);
    const CompiledGraph& cg = graphs_.front();
    for (auto& v : voices_) v.render(L, R, frames, gc_, cg, rt_);
    for (uint32_t e = 0; e < kMaxEdges; ++e) rt_.gainPrev[e] = rt_.gain[e];

    // Effects (before the master gain, so tails follow the volume).
    {
        fx::FxContext fc;
        fc.sampleRate = static_cast<float>(sampleRate_); fc.tempo = tempo_; fc.beat = clockBeat_; fc.playing = playing_; fc.beatsPerBar = beatsPerBar_;
        fx_.process(L, R, frames, params_, fc);
        if (fx_.nonFinite()) fxFlags_ |= VK_TELEMETRY_FLAG_NONFINITE;   // the slot already reset itself
    }

    if (rt_.measure) {   // publish connection levels (max across voices, with decay)
        for (uint32_t e = 0; e < cg.edgeCount; ++e) {
            const uint32_t slot = cg.edges[e].slot;
            const float prev = levels_[slot].load(std::memory_order_relaxed);
            levels_[slot].store(std::max(rt_.peak[e], prev * 0.7f), std::memory_order_relaxed);
        }
    }

    // Master gain: interpolate per sample towards the end-of-span target.
    params_.advance(frames);
    const float gainTarget = dbToGainSafe(params_.smoothed(kMasterVolume));
    const float gainStep = (gainTarget - gainCurrent_) / static_cast<float>(frames);

    auto safe = [this](float s) noexcept {
        if (!std::isfinite(s)) { pendingFlags_ |= VK_TELEMETRY_FLAG_NONFINITE; return 0.f; }
        if (s > kSafetyCeiling)  { pendingFlags_ |= VK_TELEMETRY_FLAG_CLIPPED; return kSafetyCeiling; }
        if (s < -kSafetyCeiling) { pendingFlags_ |= VK_TELEMETRY_FLAG_CLIPPED; return -kSafetyCeiling; }
        return s;
    };
    float g = gainCurrent_;
    const float swapStep = swapPhase_ == 1 ? -1.f / kSwapFrames : (swapPhase_ == 2 ? 1.f / kSwapFrames : 0.f);
    for (uint32_t i = 0; i < frames; ++i) {
        g += gainStep;
        float sg = 1.f;
        if (swapPhase_ != 0) { swapGain_ = clampf(swapGain_ + swapStep, 0.f, 1.f); sg = swapGain_; }
        L[i] = safe(L[i] * g * sg);
        R[i] = safe(R[i] * g * sg);
    }
    if (swapPhase_ == 2 && swapGain_ >= 1.f) swapPhase_ = 0;
    beatPos_ += static_cast<double>(frames) / sampleRate_ * tempo_ / 60.0;
    clockBeat_ += static_cast<double>(frames) / sampleRate_ * tempo_ / 60.0;
    gainCurrent_ = gainTarget;

    if (pendingFlags_ & VK_TELEMETRY_FLAG_NONFINITE) {
        for (auto& v : voices_) v.reset();   // recover from a numerical blow-up
    }
    if (channelCount >= 2) {
        if (channels[0]) std::memcpy(channels[0] + offset, L, sizeof(float) * frames);
        if (channels[1]) std::memcpy(channels[1] + offset, R, sizeof(float) * frames);
    } else if (channelCount == 1 && channels[0]) {
        for (uint32_t i = 0; i < frames; ++i) channels[0][offset + i] = 0.5f * (L[i] + R[i]);
    }
}

void Kernel::buildGlobalContext(uint32_t frames) noexcept {
    GlobalContext& g = gc_;
    g.params = &params_;
    g.sampleRate = sampleRate_;
    g.authentic = std::lround(params_.get(VK_P_GLOBAL_ENGINE_MODE)) == 0;
    g.instability = params_.smoothed(VK_P_GLOBAL_INSTABILITY) / 100.f;
    g.glideMs = params_.smoothed(VK_P_GLOBAL_GLIDE);
    g.stereoSpread = params_.smoothed(VK_P_GLOBAL_STEREO_SPREAD) / 100.f;
    g.routing = std::clamp(static_cast<int>(std::lround(params_.get(VK_P_GLOBAL_FILTER_ROUTING))), 0, 3);
    static constexpr int osFactors[3] = {1, 2, 4};
    g.oversampling = osFactors[std::clamp(static_cast<int>(std::lround(params_.get(VK_P_GLOBAL_OVERSAMPLING))), 0, 2)];
    g.hqOscillators = params_.get(VK_P_GLOBAL_OSCILLATOR_QUALITY) >= 0.5f;
    const float bendUp = params_.smoothed(kBendRange);
    const float bendDownRaw = params_.get(kPerf + 5);
    const float bendDown = bendDownRaw < 0.f ? bendUp : bendDownRaw;
    g.pitchOffsetSemis = pitchBendNorm_ * (pitchBendNorm_ >= 0.f ? bendUp : bendDown) + params_.smoothed(kMasterTune) / 100.f;
    g.glideConstantTime = params_.get(kPerf + 6) >= 0.5f;
    g.modf = &modFrame_;

    // Global sample & hold (control rate): one shared random stream for all voices.
    g.shClockOffset = -1;
    if (frames > 0) {
        const bool pinkSource = params_.get(VK_P_SAMPLEHOLD_SOURCE) >= 0.5f;
        sampleHold_.setRate(params_.smoothed(VK_P_SAMPLEHOLD_RATE));
        sampleHold_.setSlew(params_.smoothed(VK_P_SAMPLEHOLD_SLEW));
        const float noiseSample = pinkSource ? clampf(shNoise_.pink() * 3.f, -1.f, 1.f) : shNoise_.white();
        g.shClockOffset = sampleHold_.wrapOffset(frames);
        g.shNoise = noiseSample;
        sampleHold_.advance(frames, noiseSample);
    }
    g.shGlobalValue = sampleHold_.value();
}

// Effective edge gains and module activity for this block. Identical for all
// voices, so computed once here instead of per voice.
void Kernel::updateGraphRuntime() noexcept {
    const Rack& rk = rack();
    const CompiledGraph& cg = graphs_.front();
    GraphRuntime& rt = rt_;
    for (uint32_t e = 0; e < cg.edgeCount; ++e) {
        const CompiledEdge& ce = cg.edges[e];
        float gain = ce.gain.kind == GainSpec::Kind::Routing ? ce.gain.table[gc_.routing] : ce.gain.value;
        gain *= ce.cableGain;
        rt.preAtten[e] = gain;
        rt.attenSlot[e] = -1; rt.modLive[e] = false;
        const Atten& a = rk.inst[ce.dst].atten[ce.dstPort];
        if (a.address != kNoAddress) {
            gain *= params_.smoothed(a.address) * a.scale;
            rt.attenAddr[e] = a.address; rt.attenScale[e] = a.scale;
            rt.attenSlot[e] = params_.slotOf(a.address);
            // A matrix destination can open this edge per voice even when its knob is at 0.
            rt.modLive[e] = rt.attenSlot[e] >= 0 && isModDest_[static_cast<uint32_t>(rt.attenSlot[e])] && rt.preAtten[e] != 0.f;
        }
        rt.gain[e] = gain;
    }
    // Activity: a module runs if it reaches the output through non-zero gains
    // (or drives a user cable). Two passes cover feedback edges.
    for (uint32_t m = 0; m < kMaxInstances; ++m) rt.active[m] = false;
    rt.active[I_OUTPUT] = cg.needed[I_OUTPUT];
    for (int pass = 0; pass < 2; ++pass)
        for (int oi = static_cast<int>(cg.orderCount) - 1; oi >= 0; --oi) {
            const uint32_t m = cg.order[oi];
            if (m == I_OUTPUT) continue;
            bool act = cg.userOut[m];
            for (uint32_t e = 0; e < cg.edgeCount && !act; ++e) {
                const CompiledEdge& ce = cg.edges[e];
                if (ce.src == m && rt.active[ce.dst] && (rt.gain[e] != 0.f || rt.gainPrev[e] != 0.f || rt.modLive[e])) act = true;
            }
            rt.active[m] = act;
        }
    for (uint32_t m = 0; m < kMaxInstances; ++m) for (uint32_t p = 0; p < kMaxPorts; ++p) rt.outUsed[m][p] = false;
    for (uint32_t e = 0; e < cg.edgeCount; ++e) {
        const CompiledEdge& ce = cg.edges[e];
        if (rt.active[ce.dst] && (rt.gain[e] != 0.f || rt.gainPrev[e] != 0.f || rt.modLive[e])) rt.outUsed[ce.src][ce.srcPort] = true;
    }
    rt.measure = (++measureCounter_ & 3u) == 0;
    if (rt.measure) for (uint32_t e = 0; e < cg.edgeCount; ++e) rt.peak[e] = 0.f;
}

void Kernel::publishTelemetry(float* const* channels, uint32_t channelCount, uint32_t frames, uint64_t nanos) noexcept {
    VKTelemetry t{};
    for (uint32_t c = 0; c < channelCount && c < VK_MAX_CHANNELS; ++c) {
        if (!channels[c]) continue;
        float peak = 0.f; double sum = 0.0;
        for (uint32_t i = 0; i < frames; ++i) {
            const float a = std::fabs(channels[c][i]);
            peak = std::max(peak, a);
            sum += static_cast<double>(a) * a;
        }
        t.peak[c] = peak;
        t.rms[c] = static_cast<float>(std::sqrt(sum / frames));
    }
    t.activeVoices = activeVoiceCount();
    t.frames = frames;
    t.renderNanos = nanos;
    t.sampleTime = sampleTime_;
    t.flags = pendingFlags_ | fxFlags_;
    pendingFlags_ = 0; fxFlags_ = 0;
    telemetry_.push(t);   // dropped silently if the UI is not draining
}

// ---------------------------------------------------------------------------
// Commands & MIDI
// ---------------------------------------------------------------------------
void Kernel::handleCommand(const VKCommand& c) noexcept {
    switch (c.type) {
        case VK_CMD_ALL_NOTES_OFF: allNotesOff(); break;
        case VK_CMD_PANIC:         panic(); break;
        case VK_CMD_RESET_METERS:  break;
        case VK_CMD_CAPTURE:       captureArmed_ = c.arg0 != 0; break;
        default: break;
    }
}

// Physical key down: to the note generators when one of them takes the keys
// (arp on, sequencer in Key mode, generative Hold to Play), else to the voices.
void Kernel::noteOn(int note, int velocity, int channel) noexcept {
    keyDown_[note >> 5] |= 1u << (note & 31);
    const bool wasDirect = direct_[note >> 5] & (1u << (note & 31));
    if (keysConsumed()) {
        if (wasDirect) {   // pressed before a generator took the keys: end that note
            direct_[note >> 5] &= ~(1u << (note & 31));
            record(note, 0, VK_SOURCE_KEYS);
            soundOff(note, note, -1);
        }
        physPush(note, velocity);
        const int mode = static_cast<int>(std::lround(params_.get(seq::Arpeggiator::kBase)));
        if (mode != 0) {
            const long restart = std::lround(params_.get(seq::Arpeggiator::kBase + 16));
            arp_.keyDown(note, velocity, mode, clockBeat_, restart == 1 || (restart == 0 && playing_));
        }
        return;
    }
    direct_[note >> 5] |= 1u << (note & 31);
    if (wasDirect) record(note, 0, VK_SOURCE_KEYS);   // a repeated note-on retriggers: keep the capture paired
    record(note, velocity, VK_SOURCE_KEYS);
    if (midiOutMode() == 2) { if (wasDirect) midiOutNote(note, 0); midiOutNote(note, velocity); }
    soundOn(note, note, velocity, channel);
}

void Kernel::noteOff(int note, int channel) noexcept {
    keyDown_[note >> 5] &= ~(1u << (note & 31));
    for (uint32_t i = 0; i < physCount_; ++i)
        if (phys_[i].note == note) {
            if (sustainDown_) { phys_[i].sustained = true; return; }
            physRemove(note);
            arp_.keyUp(note, arpMode_);
            return;
        }
    // Played straight to the voices (MPE notes are matched by channel too).
    if (direct_[note >> 5] & (1u << (note & 31))) { record(note, 0, VK_SOURCE_KEYS); if (midiOutMode() == 2) midiOutNote(note, 0); }
    direct_[note >> 5] &= ~(1u << (note & 31));
    soundOff(note, note, channel);
}

void Kernel::soundOn(int key, int note, int velocity, int channel) noexcept {
    // High-resolution (MIDI 2.0) velocity of the note being handled, when there is one.
    const float vel = hiResVel_ >= 0.f && key == note ? hiResVel_ : static_cast<float>(velocity) / 127.f;
    hiResVel_ = -1.f;
    const uint64_t age = ++noteCounter_;
    const bool legatoOnly = std::lround(params_.get(kPerf + 2)) == 1;
    const bool glide = !legatoOnly || heldCount_ > 0;
    pushHeld(note);
    if (voiceMode_ != 0) { monoNoteOn(note, vel, age); return; }
    const uint32_t limit = currentPolyphony_;
    for (uint32_t i = 0; i < limit; ++i) voices_[i].setGlide(glide);

    // Key zones (poly): split routes each key to one zone, layer plays both.
    const int zoneMode = static_cast<int>(std::lround(params_.get(kZoneMode)));
    const int split = static_cast<int>(std::lround(params_.get(kSplitPoint)));
    const int tA = static_cast<int>(std::lround(params_.get(kZoneATranspose)));
    const int tB = static_cast<int>(std::lround(params_.get(kZoneBTranspose)));
    auto sound = [&](int zone, int transpose) { polyStart(key, std::clamp(note + transpose, 0, 127), zone, channel, vel, age); };
    if (zoneMode == 1) { if (note < split) sound(0, tA); else sound(1, tB); }
    else if (zoneMode == 2) { sound(0, tA); sound(1, tB); }
    else sound(0, 0);
}

void Kernel::polyStart(int key, int note, int zone, int channel, float vel, uint64_t age) noexcept {
    const uint32_t limit = currentPolyphony_;
    auto tag = [&](VoiceGraph& v) {
        v.setTags(key, zone, std::max(channel, 0));
        if (channel >= 0) {   // MPE member channel: adopt the channel's current per-note data
            v.setNoteBend(chanBend_[channel] * params_.get(kMpeRange));
            v.setPolyPressure(chanPressure_[channel]);
            v.setTimbre(chanTimbre_[channel]);
        } else { v.setNoteBend(0.f); v.setTimbre(0.5f); }
    };
    // 1. Same key (and zone / channel) already sounding → retrigger in place.
    for (uint32_t i = 0; i < limit; ++i) {
        VoiceGraph& v = voices_[i];
        if (!v.isIdle() && !v.isStealing() && v.key() == key && v.zone() == zone && (channel < 0 || v.channel() == channel)) {
            v.retrigger(vel, age); tag(v); lastNotePitch_ = static_cast<float>(note); return;
        }
    }
    // 2. Free voice.
    for (uint32_t i = 0; i < limit; ++i)
        if (voices_[i].isIdle()) { voices_[i].start(note, vel, age, lastNotePitch_); tag(voices_[i]); lastNotePitch_ = static_cast<float>(note); return; }
    // 3. Steal: quietest releasing voice first, then the oldest held voice.
    int best = -1;
    float bestLevel = 2.f;
    for (uint32_t i = 0; i < limit; ++i)
        if (voices_[i].isReleasing() && voices_[i].level() < bestLevel) { bestLevel = voices_[i].level(); best = static_cast<int>(i); }
    if (best < 0) {
        uint64_t oldest = UINT64_MAX;
        for (uint32_t i = 0; i < limit; ++i)
            if (!voices_[i].isStealing() && voices_[i].age() < oldest) { oldest = voices_[i].age(); best = static_cast<int>(i); }
    }
    if (best >= 0) { voices_[best].start(note, vel, age, lastNotePitch_); tag(voices_[best]); }   // fades out the old note first
    lastNotePitch_ = static_cast<float>(note);
}

void Kernel::soundOff(int key, int note, int channel) noexcept {
    if (voiceMode_ != 0) { monoNoteOff(note); return; }
    removeHeld(note);
    for (auto& v : voices_) {
        if (v.isHeld() && v.key() == key && (channel < 0 || v.channel() == channel)) {
            if (sustainDown_) v.setSustained(true);
            else v.release();
        }
    }
}

void Kernel::setSustainPedal(bool down) noexcept {
    sustainDown_ = down;
    if (down) return;
    for (auto& v : voices_) if (v.isSustained()) v.release();
    // Keys held for the generators by the pedal are released now.
    for (uint32_t i = 0; i < physCount_;) {
        if (phys_[i].sustained) { const int n = phys_[i].note; physRemove(n); arp_.keyUp(n, arpMode_); }
        else ++i;
    }
}

void Kernel::allNotesOff() noexcept {
    sustainDown_ = false;
    for (auto& w : keyDown_) w = 0;
    clearGenerators();
    heldCount_ = 0; monoNote_ = -1;
    ctl_[MS_AFTERTOUCH] = 0.f;
    for (auto& v : voices_) v.release();
}

void Kernel::panic() noexcept {
    sustainDown_ = false;
    for (auto& w : keyDown_) w = 0;
    clearGenerators();
    for (int c = 0; c < 16; ++c) { chanBend_[c] = 0.f; chanPressure_[c] = 0.f; chanTimbre_[c] = 0.5f; }
    heldCount_ = 0; monoNote_ = -1;
    ctl_[MS_AFTERTOUCH] = 0.f;
    pitchBendNorm_ = 0.f;
    for (auto& v : voices_) v.kill();
}

void Kernel::applyPolyphony() noexcept {
    const float raw = params_.get(kPolyphony);
    const auto idx = static_cast<uint32_t>(std::clamp(std::lround(raw), 0L, static_cast<long>(VK_POLYPHONY_CHOICES - 1)));
    const uint32_t limit = kPolyphonyTable[idx];
    if (limit == currentPolyphony_) return;
    if (limit < currentPolyphony_)
        for (uint32_t i = limit; i < VK_MAX_VOICES; ++i) voices_[i].kill();
    currentPolyphony_ = limit;
}

// ---------------------------------------------------------------------------
// Phase 4A — modulation frame & voice modes
// ---------------------------------------------------------------------------
void Kernel::buildModFrame(uint32_t frames) noexcept {
    ModFrame& f = modFrame_;
    for (uint32_t k = 0; k < 3; ++k) {
        const uint64_t b = kLfoBase + k * VK_PARAMS_PER_MODULE;
        LfoCfg& c = f.lfo[k];
        c.shape = static_cast<uint8_t>(std::clamp(std::lround(params_.get(b)), 0L, 6L));
        c.keyed = params_.get(b + 2) >= 0.5f;
        c.delayMs = params_.get(b + 3);
        c.phase = params_.get(b + 4) / 360.f;
        c.rateAddr = b + 1; c.levelAddr = b + 5;
        c.used = false;
        const long div = std::clamp(std::lround(params_.get(b + 6)), 0L, 15L);
        c.syncedRate = 0.f;
        if (div > 0) {
            const double beats = kSyncBeats[div];
            c.syncedRate = static_cast<float>(tempo_ / 60.0 / beats);
            // Free + host playing: lock the shared phase to the song position.
            if (!c.keyed && playing_) freeLfo_[k].setPhase(static_cast<float>(std::fmod(beatPos_ / beats, 1.0)) + c.phase);
        }
        freeLfo_[k].run(f.lfoFree[k], frames, c.syncedRate > 0.f ? c.syncedRate : params_.smoothed(b + 1), c.shape, sampleRate_, lfoRng_);
    }
    for (uint32_t i = 0; i < MS_COUNT; ++i) f.global[i] = ctl_[i];
    f.global[MS_BEND] = pitchBendNorm_;
    for (uint32_t m = 0; m < 4; ++m) f.global[MS_MACRO1 + m] = params_.smoothed(kMacro + m) * 0.01f;
    f.global[MS_CONSTANT] = 1.f;

    f.slotCount = 0;
    for (uint32_t i = 0; i < modDestCount_; ++i) isModDest_[static_cast<uint32_t>(modDestList_[i])] = false;
    modDestCount_ = 0;
    auto markDest = [this](int32_t slot) noexcept {
        if (slot < 0 || isModDest_[static_cast<uint32_t>(slot)] || modDestCount_ >= modDestList_.size()) return;
        isModDest_[static_cast<uint32_t>(slot)] = true; modDestList_[modDestCount_++] = slot;
    };
    for (uint32_t s = 0; s < VK_MOD_SLOTS; ++s) {
        const uint64_t b = kMatrix + s * 16;
        if (params_.get(b) < 0.5f) continue;
        const auto src = static_cast<uint8_t>(std::clamp(std::lround(params_.get(b + 1)), 0L, static_cast<long>(MS_COUNT - 1)));
        if (src == MS_NONE) continue;
        const long dest = std::lround(params_.get(b + 3));
        ModSlotCfg c;
        if (dest == static_cast<long>(VK_MOD_DEST_PITCH)) c.kind = ModDestKind::Pitch;
        else if (dest == static_cast<long>(VK_MOD_DEST_AMP)) c.kind = ModDestKind::Amp;
        else if (dest == static_cast<long>(VK_MOD_DEST_PAN)) c.kind = ModDestKind::Pan;
        else if (dest == static_cast<long>(VK_MOD_DEST_CUTOFFALL)) c.kind = ModDestKind::CutoffAll;
        else if (dest > 0 && dest < static_cast<long>(VK_ADDRESS_SPACE) && params_.isModulatable(static_cast<uint64_t>(dest))) {
            c.kind = ModDestKind::Param; c.paramSlot = params_.slotOf(static_cast<uint64_t>(dest));
        } else continue;   // none / unknown / not modulatable
        c.src = src;
        c.via = static_cast<uint8_t>(std::clamp(std::lround(params_.get(b + 2)), 0L, static_cast<long>(MS_COUNT - 1)));
        c.amount = params_.smoothed(b + 4) * 0.01f;
        c.offset = params_.smoothed(b + 5) * 0.01f;
        c.curve = static_cast<uint8_t>(std::clamp(std::lround(params_.get(b + 6)), 0L, 3L));
        c.polarity = static_cast<uint8_t>(std::clamp(std::lround(params_.get(b + 7)), 0L, 2L));
        c.invert = params_.get(b + 8) >= 0.5f;
        c.slewMs = params_.get(b + 9);
        c.index = static_cast<uint8_t>(s);
        if (c.kind == ModDestKind::Param) markDest(c.paramSlot);
        if (c.kind == ModDestKind::CutoffAll) for (uint32_t k = 0; k < f.cutoffCount; ++k) markDest(f.cutoffSlots[k]);
        for (uint8_t sv : {c.src, c.via})
            if (sv >= MS_LFO1 && sv <= MS_LFO3) f.lfo[sv - MS_LFO1].used = true;
        f.slots[f.slotCount++] = c;
    }
    for (uint32_t k = 0; k < 3; ++k) rt_.lfoDemand[k] = f.lfo[k].used || rt_.active[I_LFO1 + k];
}

uint32_t Kernel::unisonCount() const noexcept {
    if (voiceMode_ != 3) return 1;
    return 2u + static_cast<uint32_t>(std::clamp(std::lround(params_.get(kPerf + 3)), 0L, 6L));
}

void Kernel::applyVoiceMode() noexcept {
    const int m = static_cast<int>(std::clamp(std::lround(params_.get(kPerf)), 0L, 4L));
    if (m != voiceMode_) {
        for (auto& v : voices_) { v.kill(); v.setDetune(0.f); v.setGlide(true); }
        heldCount_ = 0; monoNote_ = -1; voiceMode_ = m;
    }
    if (voiceMode_ == 3) {
        const uint32_t u = unisonCount();
        const float cents = params_.smoothed(kPerf + 4);
        for (uint32_t i = 0; i < u; ++i)
            voices_[i].setDetune(cents * 0.01f * (2.f * static_cast<float>(i) / static_cast<float>(u - 1) - 1.f));
    }
}

void Kernel::pushHeld(int note) noexcept {
    removeHeld(note);
    if (heldCount_ == held_.size()) {   // drop the oldest key
        for (uint32_t i = 1; i < heldCount_; ++i) held_[i - 1] = held_[i];
        --heldCount_;
    }
    held_[heldCount_++] = static_cast<int8_t>(note);
}

void Kernel::removeHeld(int note) noexcept {
    for (uint32_t i = 0; i < heldCount_; ++i)
        if (held_[i] == note) {
            for (uint32_t j = i + 1; j < heldCount_; ++j) held_[j - 1] = held_[j];
            --heldCount_;
            return;
        }
}

int Kernel::monoTarget() const noexcept {
    if (heldCount_ == 0) return -1;
    const int prio = static_cast<int>(std::lround(params_.get(kPerf + 1)));
    int t = held_[heldCount_ - 1];
    if (prio == 1) for (uint32_t i = 0; i < heldCount_; ++i) t = std::min<int>(t, held_[i]);
    if (prio == 2) for (uint32_t i = 0; i < heldCount_; ++i) t = std::max<int>(t, held_[i]);
    return t;
}

// Mono (retrigger), legato (no retrigger while keys overlap) and unison
// (mono on 2–8 detuned voices). The note stack restores earlier keys.
void Kernel::monoNoteOn(int note, float vel, uint64_t age) noexcept {
    if (voiceMode_ == 4) { duoUpdate(true, vel, age); return; }
    const bool overlapping = heldCount_ > 1;
    const int target = monoTarget();
    if (target != note && monoNote_ >= 0) return;   // priority keeps the current key
    const bool legatoOnly = std::lround(params_.get(kPerf + 2)) == 1;
    const bool glide = !legatoOnly || overlapping;
    const uint32_t u = unisonCount();
    for (uint32_t i = 0; i < u; ++i) {
        VoiceGraph& v = voices_[i];
        v.setGlide(glide);
        if (v.isIdle() || v.isStealing()) v.start(target, vel, age, lastNotePitch_);
        else if (voiceMode_ == 2 && overlapping && v.isHeld()) v.setNote(target);
        else { v.setNote(target); v.retrigger(vel, age); }
    }
    lastNotePitch_ = static_cast<float>(target);
    monoNote_ = target;
    monoVelocity_ = vel;
}

void Kernel::monoNoteOff(int note) noexcept {
    removeHeld(note);
    if (voiceMode_ == 4) { duoUpdate(false, 0.f, 0); return; }
    if (note != monoNote_) return;
    const uint32_t u = unisonCount();
    if (heldCount_ > 0) {
        const int target = monoTarget();
        for (uint32_t i = 0; i < u; ++i) { voices_[i].setGlide(true); voices_[i].setNote(target); }
        lastNotePitch_ = static_cast<float>(target);
        monoNote_ = target;
        return;
    }
    for (uint32_t i = 0; i < u; ++i) {
        if (sustainDown_ && voices_[i].isHeld()) voices_[i].setSustained(true);
        else voices_[i].release();
    }
    monoNote_ = -1;
}

// Duophonic: voice 0 takes the lowest held key, voice 1 the highest; one key
// sounds on both. A voice whose key changes on a note-on retriggers; on a
// note-off the remaining voice slides to its new key without retriggering.
void Kernel::duoUpdate(bool noteOn, float vel, uint64_t age) noexcept {
    if (heldCount_ == 0) {
        for (uint32_t i = 0; i < 2; ++i) {
            if (sustainDown_ && voices_[i].isHeld()) voices_[i].setSustained(true);
            else voices_[i].release();
        }
        monoNote_ = -1;
        return;
    }
    int lo = held_[0], hi = held_[0];
    for (uint32_t i = 0; i < heldCount_; ++i) { lo = std::min<int>(lo, held_[i]); hi = std::max<int>(hi, held_[i]); }
    const int target[2] = {lo, hi};
    const bool legatoOnly = std::lround(params_.get(kPerf + 2)) == 1;
    for (uint32_t i = 0; i < 2; ++i) {
        VoiceGraph& v = voices_[i];
        const bool sounding = !v.isIdle() && !v.isStealing() && !v.isReleasing();
        if (sounding && v.note() == target[i]) continue;
        v.setGlide(!legatoOnly || sounding);
        if (!noteOn) { if (sounding) v.setNote(target[i]); continue; }
        if (v.isIdle() || v.isStealing()) v.start(target[i], vel, age, sounding ? static_cast<float>(v.note()) : lastNotePitch_);
        else { v.setNote(target[i]); v.retrigger(vel, age); }
    }
    lastNotePitch_ = static_cast<float>(hi);
    monoNote_ = hi;
}

// Meters for the UI: matrix slot values of the newest voice, keys held.
void Kernel::publishPerformance() noexcept {
    int newest = -1; uint64_t age = 0;
    for (uint32_t i = 0; i < VK_MAX_VOICES; ++i)
        if (!voices_[i].isIdle() && voices_[i].age() >= age) { age = voices_[i].age(); newest = static_cast<int>(i); }
    for (uint32_t s = 0; s < VK_MOD_SLOTS; ++s)
        slotMeter_[s].store(newest >= 0 ? voices_[static_cast<uint32_t>(newest)].slotValue(s) : 0.f, std::memory_order_relaxed);
    for (size_t i = 0; i < 4; ++i) heldMask_[i].store(keyDown_[i], std::memory_order_relaxed);
    stSeqStep_.store(seq_.active() ? seq_.step() : -1, std::memory_order_relaxed);
    stArpNote_.store(arp_.running() ? arp_.lastNote() : -1, std::memory_order_relaxed);
    stGenBar_.store(gen_.currentBar(clockBeat_), std::memory_order_relaxed);
    stGenPhrase_.store(gen_.active() ? static_cast<int32_t>(gen_.phraseIndex()) : -1, std::memory_order_relaxed);
    for (size_t r = 0; r < 3; ++r) stRows_[r].store(seq_.row(static_cast<int>(r)), std::memory_order_relaxed);
    stBeat_.store(clockBeat_, std::memory_order_relaxed);
    stClockTempo_.store(clk_.interval > 0.0 ? clockTempo() : 0.0, std::memory_order_relaxed);
    stClockFlags_.store((clk_.running ? 1u : 0u) | (clockActive() ? 2u : 0u) | (static_cast<uint32_t>(syncInEffect_) << 4), std::memory_order_relaxed);
    stFlags_.store((arp_.running() ? VK_SEQ_FLAG_ARP_RUNNING : 0u) | (seq_.active() ? VK_SEQ_FLAG_SEQ_RUNNING : 0u) |
                   (gen_.active() ? VK_SEQ_FLAG_GEN_RUNNING : 0u) | (playing_ ? VK_SEQ_FLAG_HOST_PLAYING : 0u) |
                   (captureArmed_ ? VK_SEQ_FLAG_CAPTURING : 0u), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Phase 5 — note generators
// ---------------------------------------------------------------------------
bool Kernel::keysConsumed() const noexcept {
    if (std::lround(params_.get(seq::Arpeggiator::kBase)) != 0) return true;
    if (std::lround(params_.get(seq::StepSequencer::kBase)) == seq::StepSequencer::Key) return true;
    return std::lround(params_.get(seq::GenerativePlayer::kBase)) != 0 && std::lround(params_.get(seq::GenerativePlayer::kBase + 13)) == 2;
}

void Kernel::physPush(int note, int velocity) noexcept {
    physRemove(note);
    if (physCount_ == phys_.size()) { for (uint32_t i = 1; i < physCount_; ++i) phys_[i - 1] = phys_[i]; --physCount_; }
    phys_[physCount_++] = {static_cast<int8_t>(note), static_cast<uint8_t>(std::clamp(velocity, 1, 127)), false};
}

void Kernel::physRemove(int note) noexcept {
    for (uint32_t i = 0; i < physCount_; ++i)
        if (phys_[i].note == note) { for (uint32_t j = i + 1; j < physCount_; ++j) phys_[j - 1] = phys_[j]; --physCount_; return; }
}

void Kernel::record(int note, int velocity, int source) noexcept {
    if (!captureArmed_) return;
    VKNoteRecord r{clockBeat_, static_cast<uint8_t>(note), static_cast<uint8_t>(velocity), static_cast<uint8_t>(source), 0};
    if (!captured_.push(r)) pendingFlags_ |= VK_TELEMETRY_FLAG_EVENTS_DROP;
}

void Kernel::engineNoteOn(int src, int note, int velocity) noexcept {
    record(note, velocity, src);
    if (midiOutMode() >= 1) midiOutNote(note, velocity);
    soundOn(128 * src + note, note, velocity, -1);
}

void Kernel::engineNoteOff(int src, int note) noexcept {
    record(note, 0, src);
    if (midiOutMode() >= 1) midiOutNote(note, 0);
    soundOff(128 * src + note, note, -1);
}

void Kernel::relocateGenerators(double beat) noexcept {
    GenSink a{this, VK_SOURCE_ARP}, s{this, VK_SOURCE_SEQ}, g{this, VK_SOURCE_GEN};
    arp_.relocate(beat, a);
    seq_.relocate(beat, s);
    gen_.relocate(beat, g);
}

void Kernel::clearGenerators() noexcept {
    arp_.clear(); seq_.clear(); gen_.clear();
    physCount_ = 0;
    for (auto& w : direct_) w = 0;
}

void Kernel::runGenerators() noexcept {
    GenSink a{this, VK_SOURCE_ARP}, s{this, VK_SOURCE_SEQ}, g{this, VK_SOURCE_GEN};
    const double beat = clockBeat_;

    // Arp mode changes: flush, and hand it the keys currently held.
    const int arpMode = static_cast<int>(std::clamp(std::lround(params_.get(seq::Arpeggiator::kBase)), 0L, 2L));
    if (arpMode != arpMode_) {
        arp_.flush(a); arp_.clearKeys();
        arpMode_ = arpMode;
        if (arpMode != 0) {
            const long restart = std::lround(params_.get(seq::Arpeggiator::kBase + 16));
            for (uint32_t i = 0; i < physCount_; ++i)
                arp_.keyDown(phys_[i].note, phys_[i].velocity, arpMode, beat, restart == 1 || (restart == 0 && playing_));
        }
    }
    // Keys stop being consumed (all generators that take keys turned off):
    // anything still held starts sounding normally.
    const bool consumed = keysConsumed();
    if (consumed_ && !consumed) {
        for (uint32_t i = 0; i < physCount_; ++i) {
            const int n = phys_[i].note;
            direct_[n >> 5] |= 1u << (n & 31);
            record(n, phys_[i].velocity, VK_SOURCE_KEYS);
            soundOn(n, n, phys_[i].velocity, -1);
        }
        physCount_ = 0;
    }
    consumed_ = consumed;

    // Sequencer.
    const long run = std::lround(params_.get(seq::StepSequencer::kBase));
    const bool seqOn = run == seq::StepSequencer::Always || (run == seq::StepSequencer::Transport && playing_)
                       || (run == seq::StepSequencer::Key && physCount_ > 0);
    if (seq_.setActive(seqOn, beat, run == seq::StepSequencer::Key, run == seq::StepSequencer::Transport) && !seqOn) seq_.flush(s);
    const int keyTranspose = run == seq::StepSequencer::Key && physCount_ > 0 ? phys_[physCount_ - 1].note - 60 : 0;
    seq_.advance(beat, params_, keyTranspose, s);
    ctl_[MS_SEQ_A] = seq_.row(0); ctl_[MS_SEQ_B] = seq_.row(1); ctl_[MS_SEQ_C] = seq_.row(2);

    // Arpeggiator (optionally transposed by the running sequencer).
    const int arpTranspose = params_.get(seq::Arpeggiator::kBase + 23) >= 0.5f && seq_.active() ? seq_.transposeOut() : 0;
    arp_.advance(beat, params_, arpTranspose, a);

    // Generative engine.
    const bool genMode = std::lround(params_.get(seq::GenerativePlayer::kBase)) != 0;
    const long follow = std::lround(params_.get(seq::GenerativePlayer::kBase + 13));
    const bool genOn = genMode && (follow != 2 || physCount_ > 0);
    if (gen_.setActive(genOn, beat, follow == 2) && !genOn) gen_.flush(g);
    int genTranspose = 0;
    if (follow != 0 && physCount_ > 0) {
        int lowest = 127;
        for (uint32_t i = 0; i < physCount_; ++i) lowest = std::min<int>(lowest, phys_[i].note);
        const int root = static_cast<int>(std::lround(params_.get(seq::GenerativePlayer::kBase + 3)));
        genTranspose = ((lowest - root) % 12 + 12) % 12;
        if (genTranspose > 5) genTranspose -= 12;
    }
    gen_.advance(beat, params_, genTranspose, g, beatsPerBar_, barOrigin_);
}

uint32_t Kernel::framesToNextGeneratorEvent() const noexcept {
    const double next = std::min(arp_.nextBeat(), std::min(seq_.nextBeat(), gen_.nextBeat()));
    const double ahead = next - clockBeat_;
    if (!(ahead < 1e6)) return VK_CONTROL_BLOCK;
    const double frames = std::ceil(ahead * 60.0 / tempo_ * sampleRate_ - 1e-6);
    return frames <= 1.0 ? 1u : (frames >= VK_CONTROL_BLOCK ? VK_CONTROL_BLOCK : static_cast<uint32_t>(frames));
}

} // namespace vektur
