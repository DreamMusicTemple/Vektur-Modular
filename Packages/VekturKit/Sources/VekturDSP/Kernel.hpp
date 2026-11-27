// Kernel.hpp — the real-time DSP kernel shared by the AUv3 and standalone app.
#pragma once

#include "VekturDSP.h"
#include "ParameterStore.hpp"
#include "SPSCQueue.hpp"
#include "graph/Graph.hpp"
#include "graph/VoiceGraph.hpp"
#include "seq/Arpeggiator.hpp"
#include "seq/StepSequencer.hpp"
#include "seq/Generative.hpp"
#include "fx/EffectsEngine.hpp"
#include "midi/MidiLearn.hpp"
#include "Recorder.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vektur {

class Kernel {
public:
    Kernel();   // registers every parameter in ParamTable.hpp
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    // [control]
    bool registerParam(const ParamInfo& info) noexcept { return params_.registerParam(info); }
    int  allocate(double sampleRate, uint32_t channelCount, uint32_t maxFrames);
    void deallocate();
    bool isAllocated() const noexcept { return allocated_; }
    uint32_t maxFrames() const noexcept { return maxFrames_; }
    uint32_t channelCount() const noexcept { return channelCount_; }

    // [any]
    void  setParam(uint64_t a, float v) noexcept { params_.set(a, v); }
    float getParam(uint64_t a) const noexcept { return params_.get(a); }

    // [render]
    void render(float* const* channels, uint32_t channelCount, uint32_t frames,
                const VKEvent* events, uint32_t eventCount) noexcept;

    float* scratchChannel(uint32_t c) noexcept { return scratch_[c].data(); }
    VKEvent* eventScratch() noexcept { return eventScratch_.data(); }
    void flagEventsDropped() noexcept { pendingFlags_ |= VK_TELEMETRY_FLAG_EVENTS_DROP; }

    bool postCommand(const VKCommand& c) noexcept { return commands_.push(c); }      // [ui]
    bool popTelemetry(VKTelemetry& t) noexcept { return telemetry_.pop(t); }         // [ui]

    // [ui] patching
    int32_t setPatch(const VKCable* cables, uint32_t count, VKPatchResult& result) noexcept;
    float connectionLevel(uint32_t slot) const noexcept {
        return slot < VK_TELEMETRY_SLOTS ? levels_[slot].load(std::memory_order_relaxed) : 0.f;
    }

    // Test hooks (non-RT inspection)
    uint32_t activeVoiceCount() const noexcept;
    uint32_t polyphonyLimit() const noexcept { return currentPolyphony_; }
    const GraphRuntime& runtime() const noexcept { return rt_; }
    // Start a note on a specific voice (tests of per-voice character). [control]
    void testNoteOnVoice(uint32_t voice, int note, float velocity) noexcept {
        if (voice < VK_MAX_VOICES) voices_[voice].start(note, velocity, ++noteCounter_, static_cast<float>(note));
    }
    const CompiledGraph& graph() const noexcept { return graphs_.front(); }

private:
    void handleCommand(const VKCommand& c) noexcept;
    void handleMidi(const uint8_t* d, uint8_t len) noexcept;
    // Phase 7 — MIDI engine (KernelMidi.cpp)
    void handleUmp(uint32_t w0, uint32_t w1) noexcept;
    void handleSystem(uint8_t status, uint8_t d1, uint8_t d2) noexcept;
    void handleControl(int ch, uint8_t cc, uint8_t v7, float hiRes) noexcept;
    void handleRpn(int ch, uint16_t number, uint16_t data14) noexcept;
    void applyMappings(uint8_t kind, int ch, uint16_t number, float in01, uint8_t raw7, bool gate, int relStep = 0) noexcept;
    void learned(uint8_t kind, int ch, uint16_t number) noexcept;
    void setParamFromMidi(uint64_t address, float value) noexcept;
    float velocityCurve(float v) const noexcept;
    bool channelAccepted(int ch) const noexcept;
    bool clockActive() const noexcept;
    double clockTempo() const noexcept;
    double clockBeatNow() const noexcept;
    void clockOut(double beatStart, uint32_t offset, uint32_t frames) noexcept;
    int midiOutMode() const noexcept { return static_cast<int>(std::lround(params_.get(VK_P_GLOBAL_MIDI_OUT))); }
    void midiOutNote(int note, int velocity) noexcept;
    void midiOutPush(uint32_t offset, uint8_t a, uint8_t b, uint8_t c, uint8_t len) noexcept;
    // Physical keys (MIDI in): routed to the note generators or straight to the voices.
    void noteOn(int note, int velocity, int channel = -1) noexcept;   // channel ≥ 0 only for MPE member channels
    void noteOff(int note, int channel = -1) noexcept;
    // Voices. `key` identifies the note for its note-off (physical key, or
    // generator source × 128 + note for arp / sequencer / generative notes).
    void soundOn(int key, int note, int velocity, int channel) noexcept;
    void soundOff(int key, int note, int channel) noexcept;
    // Phase 5 — note generators
    struct GenSink {
        Kernel* k; int src;
        void noteOn(int n, int v) noexcept { k->engineNoteOn(src, n, v); }
        void noteOff(int n) noexcept { k->engineNoteOff(src, n); }
    };
    void engineNoteOn(int src, int note, int velocity) noexcept;
    void engineNoteOff(int src, int note) noexcept;
    bool keysConsumed() const noexcept;
    void runGenerators() noexcept;
    uint32_t framesToNextGeneratorEvent() const noexcept;
    void relocateGenerators(double beat) noexcept;
    void clearGenerators() noexcept;
    void physPush(int note, int velocity) noexcept;
    void physRemove(int note) noexcept;
    void record(int note, int velocity, int source) noexcept;
    void polyStart(int key, int note, int zone, int channel, float vel, uint64_t age) noexcept;
    bool handleMpeMember(uint8_t status, int channel, uint8_t d1, uint8_t d2) noexcept;
    void publishPerformance() noexcept;
    void setSustainPedal(bool down) noexcept;
    void allNotesOff() noexcept;
    void panic() noexcept;
    void applyPolyphony() noexcept;
    void applyVoiceMode() noexcept;
    void buildModFrame(uint32_t frames) noexcept;
    // Phase 4A — mono / legato / unison
    int  monoTarget() const noexcept;
    void monoNoteOn(int note, float vel, uint64_t age) noexcept;
    void monoNoteOff(int note) noexcept;
    void duoUpdate(bool noteOn, float vel, uint64_t age) noexcept;
    void pushHeld(int note) noexcept;
    void removeHeld(int note) noexcept;
    uint32_t unisonCount() const noexcept;
    void buildGlobalContext(uint32_t frames) noexcept;
    void updateGraphRuntime() noexcept;
    void renderSpan(float* const* channels, uint32_t channelCount, uint32_t offset, uint32_t frames) noexcept;
    void publishTelemetry(float* const* channels, uint32_t channelCount, uint32_t frames, uint64_t nanos) noexcept;

    ParameterStore params_;
    std::array<VoiceGraph, VK_MAX_VOICES> voices_{};
    GraphExchange graphs_;
    GraphRuntime rt_{};
    GlobalContext gc_{};
    std::array<std::atomic<float>, VK_TELEMETRY_SLOTS> levels_{};
    uint32_t measureCounter_ = 0;
    int swapPhase_ = 0;          // 0 idle, 1 fading out before a graph swap, 2 fading in
    float swapGain_ = 1.f;
    SampleHold sampleHold_;
    Noise shNoise_{0xC0FFEEu};
    SPSCQueue<VKCommand, 256> commands_;
    SPSCQueue<VKTelemetry, 256> telemetry_;

    std::vector<float> busL_, busR_;                            // sized in allocate()
    std::array<std::vector<float>, VK_MAX_CHANNELS> scratch_;   // sized in allocate()
    std::array<VKEvent, VK_MAX_EVENTS_PER_RENDER> eventScratch_{};

    double sampleRate_ = 48000.0;
    uint32_t channelCount_ = 2;
    uint32_t maxFrames_ = 0;
    bool allocated_ = false;

    uint64_t noteCounter_ = 0;
    uint64_t sampleTime_ = 0;
    uint32_t currentPolyphony_ = 8;
    float pitchBendNorm_ = 0.f;
    float lastNotePitch_ = 60.f;
    bool sustainDown_ = false;
    uint32_t pendingFlags_ = 0;
    float gainCurrent_ = 0.f;

    // Phase 4A
    ModFrame modFrame_{};
    Lfo freeLfo_[3]{};
    Rng lfoRng_{0x1F0u};
    float ctl_[MS_COUNT]{};            // MIDI controller sources (0…1, bend −1…1)
    int voiceMode_ = 0;                // 0 poly, 1 mono, 2 legato, 3 unison, 4 duo
    std::array<int8_t, 16> held_{};    // keys held, oldest first
    uint32_t heldCount_ = 0;
    float monoVelocity_ = 0.f;
    int monoNote_ = -1;                // note currently sounding in mono modes
    std::array<bool, VK_MAX_REGISTERED_PARAMS> isModDest_{};   // registration slot is a matrix destination
    std::array<int32_t, VK_MOD_SLOTS * kMaxCutoffSlots> modDestList_{};
    uint32_t modDestCount_ = 0;
    // Phase 4B
    double hostTempo_ = 120.0, hostBeat_ = 0.0, beatPos_ = 0.0;
    bool hostValid_ = false, hostPlaying_ = false, playing_ = false;
    double tempo_ = 120.0;
    float chanBend_[16]{}, chanPressure_[16]{};
    float chanTimbre_[16]{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    uint32_t keyDown_[4]{};
    std::array<std::atomic<float>, VK_MOD_SLOTS> slotMeter_{};
    std::array<std::atomic<uint32_t>, 4> heldMask_{};
    // Phase 7 — MIDI engine
    midi::MappingExchange mappings_;
    std::array<midi::MappingState, VK_MIDI_MAX_MAPPINGS> mapState_{};
    std::atomic<bool> learnArmed_{false};
    SPSCQueue<VKMidiUIEvent, 256> midiUi_;
    std::array<std::atomic<uint32_t>, VK_ADDRESS_SPACE / 32> midiDirty_{};
    bool midiDirtyPending_ = false;
    struct ChannelState {
        uint8_t rpnMsb = 127, rpnLsb = 127, nrpnMsb = 127, nrpnLsb = 127;
        bool nrpn = false;
        uint8_t dataMsb = 0, dataLsb = 0, bankMsb = 0, bankLsb = 0;
        uint8_t ccMsb[32]{};
    };
    std::array<ChannelState, 16> chan_{};
    struct ClockState {
        double lastTick = -1.0;      // absolute sample of the last tick
        double interval = 0.0;       // smoothed samples per tick
        double tickBeat = 0.0;       // beat position of the last tick
        double sppBeat = 0.0;
        uint32_t ticks = 0;
        bool running = false;
    } clk_;
    uint64_t eventSample_ = 0;
    uint32_t spanPos_ = 0;
    float hiResVel_ = -1.f;
    int syncInEffect_ = 0;
    double beatsPerBar_ = 4.0, barOrigin_ = 0.0;
    std::array<VKMidiOutEvent, VK_MIDI_OUT_MAX> midiOut_{};
    uint32_t midiOutCount_ = 0;
    double nextClockOutTick_ = 0.0;
    bool clockOutOn_ = false, clockOutResync_ = true;
    std::atomic<const void*> midiOutBlock_{nullptr};
    AudioRecorder recorder_;
    std::atomic<uint32_t> midiIn_{0}, midiOutTotal_{0}, midiLast_{0};
    std::atomic<double> stClockTempo_{0.0};
    std::atomic<uint32_t> stClockFlags_{0};
    // Phase 6
    fx::EffectsEngine fx_;
    uint32_t fxFlags_ = 0;
    // Phase 5
    seq::Arpeggiator arp_;
    seq::StepSequencer seq_;
    seq::GenerativePlayer gen_;
    double clockBeat_ = 0.0;           // generators' clock: host beat while playing, free-running otherwise
    bool clockFollowing_ = false;
    int arpMode_ = 0;
    bool consumed_ = false;
    struct PhysKey { int8_t note; uint8_t velocity; bool sustained; };
    std::array<PhysKey, 32> phys_{};   // keys held for the generators, oldest first
    uint32_t physCount_ = 0;
    uint32_t direct_[4]{};             // keys that went straight to the voices
    bool captureArmed_ = false;
    SPSCQueue<VKNoteRecord, 2048> captured_;
    std::atomic<int32_t> stSeqStep_{-1}, stArpNote_{-1}, stGenBar_{-1}, stGenPhrase_{-1};
    std::atomic<uint32_t> stFlags_{0};
    std::array<std::atomic<float>, 3> stRows_{};
    std::atomic<double> stBeat_{0.0};
public:
    // Phase 6 — effects
    fx::EffectsEngine& effects() noexcept { return fx_; }
    const fx::EffectsEngine& effects() const noexcept { return fx_; }
    uint32_t updateEffects() { return fx_.update(params_); }   // [ui]
    // Phase 7 — MIDI
    void setMidiMappings(const VKMidiMapping* m, uint32_t n) noexcept { mappings_.publish(m, n); }   // [ui]
    void midiLearn(bool armed) noexcept { learnArmed_.store(armed, std::memory_order_relaxed); }    // [ui]
    bool popMidiUi(VKMidiUIEvent& e) noexcept { return midiUi_.pop(e); }                            // [ui]
    uint32_t takeMidiChanges(uint64_t* out, uint32_t cap) noexcept;                                // [ui]
    void midiStatus(VKMidiStatus& s) const noexcept;                                               // [any]
    uint32_t midiOutput(const VKMidiOutEvent** ev) const noexcept { if (ev) *ev = midiOut_.data(); return midiOutCount_; }
    void setMidiOutBlock(const void* b) noexcept { midiOutBlock_.store(b, std::memory_order_release); }
    const void* midiOutBlock() const noexcept { return midiOutBlock_.load(std::memory_order_acquire); }
    void setTransportEx(double tempo, double beat, bool playing, double barStart, double num, double den) noexcept {
        setTransport(tempo, beat, playing);
        if (num >= 1.0 && den >= 1.0 && std::isfinite(barStart)) { beatsPerBar_ = std::clamp(num * 4.0 / den, 0.5, 32.0); barOrigin_ = barStart; }
    }
    double beatsPerBar() const noexcept { return beatsPerBar_; }
    // Phase 9 — recording tap
    AudioRecorder& recorder() noexcept { return recorder_; }
    // [ui] Phase 5
    bool popNoteRecord(VKNoteRecord& r) noexcept { return captured_.pop(r); }
    void sequencerStatus(VKSequencerStatus& s) const noexcept {
        s.seqStep = stSeqStep_.load(std::memory_order_relaxed); s.arpNote = stArpNote_.load(std::memory_order_relaxed);
        s.genBar = stGenBar_.load(std::memory_order_relaxed); s.genPhrase = stGenPhrase_.load(std::memory_order_relaxed);
        s.flags = stFlags_.load(std::memory_order_relaxed);
        for (size_t r = 0; r < 3; ++r) s.rows[r] = stRows_[r].load(std::memory_order_relaxed);
        s.beat = stBeat_.load(std::memory_order_relaxed);
    }
    double clockBeat() const noexcept { return clockBeat_; }
    const seq::Arpeggiator& arpeggiator() const noexcept { return arp_; }
    const seq::StepSequencer& sequencer() const noexcept { return seq_; }
    const seq::GenerativePlayer& generative() const noexcept { return gen_; }
    const ParameterStore& params() const noexcept { return params_; }
public:
    // [render] host musical context for the next render call
    void setTransport(double tempo, double beat, bool playing) noexcept {
        if (!(tempo > 0.0) || !std::isfinite(tempo) || !std::isfinite(beat)) return;
        hostTempo_ = tempo; hostBeat_ = beat; hostPlaying_ = playing; hostValid_ = true;
        beatsPerBar_ = 4.0; barOrigin_ = 0.0;   // no meter supplied: 4/4 (setTransportEx sets it)
    }
    float slotMeter(uint32_t s) const noexcept { return s < VK_MOD_SLOTS ? slotMeter_[s].load(std::memory_order_relaxed) : 0.f; }
    void heldNotes(uint32_t out[4]) const noexcept { for (int i = 0; i < 4; ++i) out[i] = heldMask_[static_cast<size_t>(i)].load(std::memory_order_relaxed); }
    double beatPosition() const noexcept { return beatPos_; }
    double tempo() const noexcept { return tempo_; }
    bool isPlaying() const noexcept { return playing_; }
    // Test hooks (non-RT inspection)
    const ModFrame& modFrame() const noexcept { return modFrame_; }
    const VoiceGraph& voice(uint32_t i) const noexcept { return voices_[i % VK_MAX_VOICES]; }
    float controller(uint32_t src) const noexcept { return src < MS_COUNT ? ctl_[src] : 0.f; }
};

inline constexpr std::array<uint32_t, VK_POLYPHONY_CHOICES> kPolyphonyTable{1, 2, 4, 6, 8, 12, 16, 24, 32};

} // namespace vektur
