// VoiceGraph.cpp — see VoiceGraph.hpp. Real-time safe except prepare().
#include "VoiceGraph.hpp"
#include "../modules/LadderVariants.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vektur {

namespace {
constexpr uint32_t kSilentFramesToFree = 1024;   // ≈ 21 ms of digital silence after release
constexpr float kSilence = 1.0e-5f;              // −100 dBFS
}

void VoiceGraph::prepare(double sampleRate, uint32_t voiceIndex) {
    sr_ = sampleRate;
    index_ = voiceIndex;
    const Rack& rk = rack();
    for (uint32_t m = 0; m < rk.count; ++m) {
        if (!mods_[m]) mods_[m] = makeModule(rk.inst[m].type, m);
        mods_[m]->prepare(sampleRate, voiceIndex, rk.inst[m].paramModule);
    }
    rng_ = Rng(hash32(0x7E4u + voiceIndex * 2246822519u));
    Rng tol(hash32(0xA11CE5u + voiceIndex * 7919u));
    vc_ = {};
    vc_.index = voiceIndex;
    vc_.tolCutoff = tol.bipolar();
    vc_.tolTime = tol.bipolar();
    vc_.outL = voiceL_;
    vc_.outR = voiceR_;
    spread_ = static_cast<float>(voiceIndex % 8) / 3.5f - 1.f;   // −1 … +1 across 8 positions
    (void)Halfband2x::coef();
    (void)DiodeLadder::design();   // one-time table init off the render thread
    reset();
}

void VoiceGraph::reset() noexcept {
    for (auto& m : mods_) if (m) m->reset();
    clearFeedback();
    state_ = State::Idle; pending_ = {}; sustained_ = false;
    vc_.gate = false; vc_.triggerPending = false; vc_.thermalCents = 0.f;
    fade_ = 1.f; silentFrames_ = 0; lastPeak_ = 0.f; startPending_ = false;
    std::memset(mod_, 0, sizeof(mod_)); touchedCount_ = 0;
    std::memset(slew_, 0, sizeof(slew_)); slewPrimed_ = false;
    vc_.pitchModSemis = 0.f; vc_.ampMod = 1.f; vc_.panMod = 0.f; polyAT_ = 0.f; vc_.noteBendSemis = 0.f; timbre_ = 0.5f;
    egLast_[0] = egLast_[1] = 0.f;
}

void VoiceGraph::clearFeedback() noexcept { std::memset(fb_, 0, sizeof(fb_)); fbLen_ = 0; }

void VoiceGraph::begin(int note, float velocity, uint64_t age, float glideFrom, const GlobalContext* g) noexcept {
    const bool fromIdle = state_ == State::Idle || state_ == State::Stealing;
    vc_.note = note; vc_.velocity = velocity; vc_.glideFrom = glideFrom;
    vc_.gate = true; vc_.triggerPending = true;
    age_ = age; sustained_ = false; fade_ = 1.f; pending_ = {}; silentFrames_ = 0;
    state_ = State::Held;
    if (fromIdle) polyAT_ = 0.f;
    noteStartModulation(g);
    if (g) { for (auto& m : mods_) if (m) m->noteStart(vc_, *g, fromIdle); startPending_ = false; }
    else { startPending_ = true; startFromIdle_ = fromIdle; }
}

void VoiceGraph::start(int note, float velocity, uint64_t age, float glideFromSemis) noexcept {
    if (state_ == State::Idle) { begin(note, velocity, age, glideFromSemis, nullptr); return; }
    pending_ = {note, velocity, age, glideFromSemis};
    fadeStep_ = static_cast<float>(1.0 / (0.002 * sr_));   // 2 ms click-free steal
    state_ = State::Stealing;
}

void VoiceGraph::setNote(int note) noexcept {
    if (state_ == State::Stealing && pending_.note >= 0) { pending_.note = note; return; }
    if (state_ != State::Idle) vc_.note = note;
}

// Per-note modulation state: keyed LFOs restart at their phase, the per-note
// random source is redrawn and the LFO delay timer restarts.
void VoiceGraph::noteStartModulation(const GlobalContext* g) noexcept {
    random_ = rng_.bipolar();
    noteFrames_ = 0.0;
    if (g && g->modf)
        for (uint32_t k = 0; k < 3; ++k)
            if (g->modf->lfo[k].keyed) lfo_[k].reset(g->modf->lfo[k].phase, rng_);
}

void VoiceGraph::retrigger(float velocity, uint64_t age) noexcept {
    vc_.velocity = velocity; age_ = age; sustained_ = false;
    vc_.gate = true; vc_.triggerPending = true; silentFrames_ = 0;
    state_ = State::Held;
}

void VoiceGraph::release() noexcept {
    sustained_ = false;
    if (state_ == State::Held) { vc_.gate = false; state_ = State::Releasing; silentFrames_ = 0; }
}

void VoiceGraph::kill() noexcept {
    if (state_ == State::Idle) return;
    pending_ = {};
    fadeStep_ = static_cast<float>(1.0 / (0.002 * sr_));
    state_ = State::Stealing;
}

void VoiceGraph::render(float* outL, float* outR, uint32_t frames, const GlobalContext& g,
                        const CompiledGraph& cg, GraphRuntime& rt) noexcept {
    if (state_ == State::Idle) return;
    if (startPending_) {
        noteStartModulation(&g);
        for (auto& m : mods_) if (m) m->noteStart(vc_, g, startFromIdle_);
        startPending_ = false;
    }
    runModulation(frames, g, rt);
    GlobalContext gv = g;
    gv.mod = mod_;
    // Edge gains for this voice: global gains unless an attenuator carries a matrix offset.
    bool edgeMod = false;
    for (uint32_t e = 0; e < cg.edgeCount; ++e) {
        const int32_t sl = rt.attenSlot[e];
        if (sl >= 0 && mod_[sl] != 0.f) {
            vGain_[e] = rt.preAtten[e] * g.params->modulated(rt.attenAddr[e], mod_) * rt.attenScale[e];
            edgeMod = true;
        } else vGain_[e] = rt.gain[e];
    }
    if (!edgeModPrev_) std::memcpy(vGainPrev_, rt.gainPrev, sizeof(float) * cg.edgeCount);
    const float* G1 = edgeMod || edgeModPrev_ ? vGain_ : rt.gain;
    const float* G0 = edgeMod || edgeModPrev_ ? vGainPrev_ : rt.gainPrev;
    struct GainCommit {   // runs on every exit path of render()
        VoiceGraph& v; bool mod; uint32_t n;
        ~GainCommit() { if (mod) std::memcpy(v.vGainPrev_, v.vGain_, sizeof(float) * n); v.edgeModPrev_ = mod; }
    } commit{*this, edgeMod, cg.edgeCount};
    // Common-mode thermal drift of this voice (τ = 20 s).
    if (g.instability > 0.f) {
        const float dt = static_cast<float>(frames / sr_);
        vc_.thermalCents += -vc_.thermalCents * dt / 20.f + 0.316f * std::sqrt(dt) * 1.732f * rng_.bipolar();
    }

    uint32_t pos = 0;
    while (pos < frames && state_ != State::Idle) {
        uint32_t n = frames - pos;
        bool transition = false;
        if (state_ == State::Stealing) {
            // Sample at which the 2 ms fade reaches zero (matches the per-sample loop below).
            const uint32_t t = static_cast<uint32_t>(std::max(0.f, std::ceil(fade_ / fadeStep_) - 1.f));
            if (t < n) { n = t + 1; transition = true; }
        }
        vc_.lfoOffset = pos;
        runGraph(n, gv, cg, rt, G1, G0);
        if (rt.active[I_EG1]) egLast_[0] = out_[I_EG1][0][n - 1];
        if (rt.active[I_EG2]) egLast_[1] = out_[I_EG2][0][n - 1];

        float peak = 0.f;
        for (uint32_t i = 0; i < n; ++i) {
            float l = voiceL_[i], r = voiceR_[i];
            if (state_ == State::Stealing) {
                fade_ -= fadeStep_;
                const float f = (transition && i == n - 1) ? 0.f : std::max(fade_, 0.f);
                l *= f; r *= f;
            }
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            outL[pos + i] += l; outR[pos + i] += r;
        }
        lastPeak_ = peak;
        pos += n;

        if (transition) {
            if (pending_.note >= 0) begin(pending_.note, pending_.velocity, pending_.age, pending_.glideFrom, &gv);
            else { state_ = State::Idle; vc_.gate = false; return; }
            continue;
        }
        if (state_ == State::Releasing) {
            silentFrames_ = peak < kSilence ? silentFrames_ + n : 0;
            if (silentFrames_ >= kSilentFramesToFree) { state_ = State::Idle; return; }
        }
    }
}

// Evaluates LFOs and the matrix for this block (control rate; values are the
// end-of-block source values, so modulation lags by at most one 32-frame block).
void VoiceGraph::runModulation(uint32_t frames, const GlobalContext& g, const GraphRuntime& rt) noexcept {
    for (uint32_t i = 0; i < touchedCount_; ++i) mod_[touched_[i]] = 0.f;
    touchedCount_ = 0;
    vc_.pitchModSemis = 0.f; vc_.ampMod = 1.f; vc_.panMod = 0.f;
    std::memset(slotValue_, 0, sizeof(slotValue_));
    const ModFrame* mf = g.modf;
    if (!mf || frames == 0) { for (auto& l : vc_.lfo) l = nullptr; return; }

    // LFOs (rate/level read through the matrix of the previous block).
    float lfoEnd[3]{};
    for (uint32_t k = 0; k < 3; ++k) {
        const LfoCfg& c = mf->lfo[k];
        if (!rt.lfoDemand[k]) { vc_.lfo[k] = nullptr; continue; }
        float* buf = lfoBuf_[k];
        const float rate = c.syncedRate > 0.f ? c.syncedRate : g.params->modulated(c.rateAddr, mod_);
        if (c.keyed) lfo_[k].run(buf, frames, rate, c.shape, sr_, rng_);
        else std::memcpy(buf, mf->lfoFree[k], sizeof(float) * frames);
        const float level = g.params->modulated(c.levelAddr, mod_) * 0.01f;
        const double delayFrames = c.delayMs * 0.001 * sr_;
        for (uint32_t i = 0; i < frames; ++i) {
            float d = 1.f;
            if (delayFrames > 0.0) d = static_cast<float>(std::min(1.0, (noteFrames_ + i) / delayFrames));
            buf[i] *= level * d;
        }
        vc_.lfo[k] = buf;
        lfoEnd[k] = buf[frames - 1];
    }
    noteFrames_ += frames;

    // Source values for this voice.
    float src[MS_COUNT];
    for (uint32_t i = 0; i < MS_COUNT; ++i) src[i] = mf->global[i];
    src[MS_LFO1] = lfoEnd[0]; src[MS_LFO2] = lfoEnd[1]; src[MS_LFO3] = lfoEnd[2];
    src[MS_EG1] = egLast_[0]; src[MS_EG2] = egLast_[1];
    src[MS_VELOCITY] = vc_.velocity;
    src[MS_KEY] = clampf((static_cast<float>(vc_.note) - 60.f) / 60.f, -1.f, 1.f);
    src[MS_POLY_AT] = polyAT_;
    src[MS_RANDOM] = random_;
    src[MS_SPREAD] = spread_;
    src[MS_TIMBRE] = timbre_;
    src[MS_ZONE] = static_cast<float>(zone_);

    const float slewDt = static_cast<float>(frames / sr_) * 1000.f;
    for (uint32_t s = 0; s < mf->slotCount; ++s) {
        const ModSlotCfg& c = mf->slots[s];
        float x = shapeModValue(src[c.src], kSourceBipolar[c.src], c);
        if (c.via != MS_NONE) x *= src[c.via];
        float y = c.offset + c.amount * x;
        float& st = slew_[c.index];
        if (c.slewMs > 0.f && slewPrimed_) y = st + (y - st) * (1.f - std::exp(-slewDt / c.slewMs));
        st = y;
        slotValue_[c.index] = y;
        switch (c.kind) {
            case ModDestKind::Param:
                if (mod_[c.paramSlot] == 0.f && touchedCount_ < VK_MOD_SLOTS * kMaxCutoffSlots) touched_[touchedCount_++] = c.paramSlot;
                mod_[c.paramSlot] += y; break;
            case ModDestKind::CutoffAll:
                for (uint32_t k = 0; k < mf->cutoffCount; ++k) {
                    const int32_t ps = mf->cutoffSlots[k];
                    if (mod_[ps] == 0.f && touchedCount_ < VK_MOD_SLOTS * kMaxCutoffSlots) touched_[touchedCount_++] = ps;
                    mod_[ps] += y;
                }
                break;
            case ModDestKind::Pitch: vc_.pitchModSemis += y * kPitchModSemis; break;
            case ModDestKind::Amp:   vc_.ampMod += y; break;
            case ModDestKind::Pan:   vc_.panMod += y; break;
            case ModDestKind::None:  break;
        }
    }
    slewPrimed_ = true;
}

void VoiceGraph::runGraph(uint32_t n, const GlobalContext& g, const CompiledGraph& cg, GraphRuntime& rt,
                          const float* G1, const float* G0) noexcept {
    const Rack& rk = rack();
    for (uint32_t oi = 0; oi < cg.orderCount; ++oi) {
        const uint32_t m = cg.order[oi];
        if (!rt.active[m]) continue;
        const InstanceDesc& d = rk.inst[m];
        Ports ports;
        for (uint32_t p = 0; p < d.nIn; ++p) {
            const uint32_t start = cg.inStart[m][p], count = cg.inCount[m][p];
            // Collect contributing edges.
            uint32_t live = 0, only = 0;
            for (uint32_t e = start; e < start + count; ++e) {
                const CompiledEdge& ce = cg.edges[e];
                if ((G1[e] != 0.f || G0[e] != 0.f) && (rt.active[ce.src] || ce.feedback != kNoFeedback)) { ++live; only = e; }
            }
            if (live == 0) { ports.in[p] = nullptr; continue; }
            const CompiledEdge& first = cg.edges[only];
            if (live == 1 && first.feedback == kNoFeedback && G1[only] == 1.f && G0[only] == 1.f) {
                ports.in[p] = out_[first.src][first.srcPort];   // zero-copy
                if (rt.measure) {
                    float pk = 0.f;
                    for (uint32_t i = 0; i < n; ++i) pk = std::max(pk, std::fabs(ports.in[p][i]));
                    rt.peak[only] = std::max(rt.peak[only], pk);
                }
                continue;
            }
            float* buf = in_[m][p];
            std::memset(buf, 0, sizeof(float) * n);
            for (uint32_t e = start; e < start + count; ++e) {
                const CompiledEdge& ce = cg.edges[e];
                const float g1 = G1[e], g0 = G0[e];
                if (g0 == 0.f && g1 == 0.f) continue;
                const bool isFb = ce.feedback != kNoFeedback;
                if (!isFb && !rt.active[ce.src]) continue;
                const float* src = isFb ? fb_[ce.feedback] : out_[ce.src][ce.srcPort];
                const float step = (g1 - g0) / static_cast<float>(n);
                float pk = 0.f;
                for (uint32_t i = 0; i < n; ++i) {
                    float x = isFb ? (fbLen_ ? src[std::min(i, fbLen_ - 1)] : 0.f) : src[i];
                    if (isFb) x = 2.f * fastTanh(0.5f * x);   // feedback limiter
                    const float v = x * (g0 + step * static_cast<float>(i + 1));
                    buf[i] += v;
                    pk = std::max(pk, std::fabs(v));
                }
                if (rt.measure) rt.peak[e] = std::max(rt.peak[e], pk);
            }
            ports.in[p] = buf;
        }
        for (uint32_t o = 0; o < d.nOut; ++o) { ports.out[o] = out_[m][o]; ports.outUsed[o] = rt.outUsed[m][o]; }
        mods_[m]->process(ports, n, vc_, g);
    }
    // Feedback edges read this block's source output during the next block.
    for (uint32_t e = 0; e < cg.edgeCount; ++e) {
        const CompiledEdge& ce = cg.edges[e];
        if (ce.feedback == kNoFeedback) continue;
        if (rt.active[ce.src]) std::memcpy(fb_[ce.feedback], out_[ce.src][ce.srcPort], sizeof(float) * n);
        else std::memset(fb_[ce.feedback], 0, sizeof(float) * n);
    }
    fbLen_ = n;
}

} // namespace vektur
