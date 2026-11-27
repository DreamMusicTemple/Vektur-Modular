// VoiceGraph.hpp — one polyphonic voice: a full set of persistent module
// instances run in the order of the current CompiledGraph.
#pragma once

#include "Graph.hpp"
#include "GraphModules.hpp"

#include <array>
#include <memory>

namespace vektur {

// Per-block data shared by all voices (computed once by the kernel).
struct GraphRuntime {
    bool active[kMaxInstances]{};       // module produces audible/used signal this block
    float gain[kMaxEdges]{};            // effective gain per edge (normal/cable × attenuator)
    float gainPrev[kMaxEdges]{};        // previous block's gain (for click-free ramps)
    bool outUsed[kMaxInstances][kMaxPorts]{};  // output port feeds a live edge
    bool measure = false;               // compute connection levels this block
    float peak[kMaxEdges]{};            // max |signal| per edge across voices (if measure)
    bool lfoDemand[3]{};                // LFO k is needed this block (matrix or rack module)
    // Phase 4A — per-voice modulation of input attenuators
    float preAtten[kMaxEdges]{};        // edge gain before its attenuator
    float attenScale[kMaxEdges]{};
    uint64_t attenAddr[kMaxEdges]{};
    int32_t attenSlot[kMaxEdges]{};     // registration slot of the attenuator, -1 = none
    bool modLive[kMaxEdges]{};          // attenuator is a matrix destination (may be non-zero per voice)
};

class VoiceGraph {
public:
    void prepare(double sampleRate, uint32_t voiceIndex);   // [control] creates modules
    void reset() noexcept;

    // Allocator interface (unchanged since Phase 1).
    void start(int note, float velocity, uint64_t age, float glideFromSemis) noexcept;
    void retrigger(float velocity, uint64_t age) noexcept;
    void release() noexcept;
    void kill() noexcept;
    void setSustained(bool s) noexcept { sustained_ = s; }
    // Phase 4A — mono/legato/unison support.
    void setNote(int note) noexcept;                  // change pitch without retriggering (legato)
    void setGlide(bool g) noexcept { vc_.glide = g; }
    void setDetune(float semis) noexcept { vc_.detuneSemis = semis; }
    void setPolyPressure(float p) noexcept { polyAT_ = p; }
    // Phase 4B — key/zone/channel tags (note-off matching), MPE per-note data.
    void setTags(int key, int zone, int channel) noexcept { key_ = key; zone_ = zone; channel_ = channel; }
    int key() const noexcept { return key_; }
    int zone() const noexcept { return zone_; }
    int channel() const noexcept { return channel_; }
    void setNoteBend(float semis) noexcept { vc_.noteBendSemis = semis; }
    void setTimbre(float t) noexcept { timbre_ = t; }
    float timbre() const noexcept { return timbre_; }
    float slotValue(uint32_t s) const noexcept { return s < VK_MOD_SLOTS ? slotValue_[s] : 0.f; }
    float matrixOffset(int32_t slot) const noexcept { return slot >= 0 && slot < static_cast<int32_t>(VK_MAX_REGISTERED_PARAMS) ? mod_[slot] : 0.f; }
    const VoiceContext& context() const noexcept { return vc_; }

    void render(float* outL, float* outR, uint32_t frames, const GlobalContext& g,
                const CompiledGraph& cg, GraphRuntime& rt) noexcept;
    void clearFeedback() noexcept;

    bool isIdle() const noexcept { return state_ == State::Idle; }
    bool isReleasing() const noexcept { return state_ == State::Releasing; }
    bool isHeld() const noexcept { return state_ == State::Held; }
    bool isSustained() const noexcept { return sustained_; }
    bool isStealing() const noexcept { return state_ == State::Stealing; }
    int note() const noexcept { return state_ == State::Stealing ? pending_.note : vc_.note; }
    float level() const noexcept { return lastPeak_; }
    uint64_t age() const noexcept { return state_ == State::Stealing ? pending_.age : age_; }

private:
    enum class State : uint8_t { Idle, Held, Releasing, Stealing };
    struct Pending { int note = -1; float velocity = 0.f; uint64_t age = 0; float glideFrom = 0.f; };

    void begin(int note, float velocity, uint64_t age, float glideFrom, const GlobalContext* g) noexcept;
    void runGraph(uint32_t n, const GlobalContext& g, const CompiledGraph& cg, GraphRuntime& rt,
                  const float* G1, const float* G0) noexcept;
    void runModulation(uint32_t frames, const GlobalContext& g, const GraphRuntime& rt) noexcept;
    void noteStartModulation(const GlobalContext* g) noexcept;

    double sr_ = 48000.0;
    uint32_t index_ = 0;
    std::array<std::unique_ptr<Module>, I_COUNT> mods_{};
    float out_[I_COUNT][kMaxPorts][VK_CONTROL_BLOCK]{};
    float in_[I_COUNT][kMaxPorts][VK_CONTROL_BLOCK]{};
    float fb_[kMaxFeedback][VK_CONTROL_BLOCK]{};
    uint32_t fbLen_ = 0;
    float voiceL_[VK_CONTROL_BLOCK]{}, voiceR_[VK_CONTROL_BLOCK]{};

    VoiceContext vc_{};
    Rng rng_;
    State state_ = State::Idle;
    Pending pending_{};
    uint64_t age_ = 0;
    bool sustained_ = false;
    bool startPending_ = false, startFromIdle_ = false;
    float fade_ = 1.f, fadeStep_ = 0.f, lastPeak_ = 0.f;
    uint32_t silentFrames_ = 0;

    // Phase 4A — modulation state
    float mod_[VK_MAX_REGISTERED_PARAMS]{};     // normalised offsets, by registration slot
    int32_t touched_[VK_MOD_SLOTS * kMaxCutoffSlots]{};
    uint32_t touchedCount_ = 0;
    Lfo lfo_[3]{};
    float lfoBuf_[3][VK_CONTROL_BLOCK]{};
    float slew_[VK_MOD_SLOTS]{};
    bool slewPrimed_ = false;
    float polyAT_ = 0.f, random_ = 0.f, spread_ = 0.f;
    float egLast_[2]{};
    double noteFrames_ = 0.0;                   // frames since note start (LFO delay)
    float vGain_[kMaxEdges]{}, vGainPrev_[kMaxEdges]{};   // per-voice edge gains when attenuators are modulated
    bool edgeModPrev_ = false;
    int key_ = -1, zone_ = 0, channel_ = 0;
    float timbre_ = 0.5f;
    float slotValue_[VK_MOD_SLOTS]{};
};

} // namespace vektur
