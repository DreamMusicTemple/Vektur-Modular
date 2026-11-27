// EffectsEngine.hpp — eight effect slots on three buses (insert chain, send
// A, send B), each chain series or parallel; per-slot wet/dry, output gain
// and click-free bypass; sends tapped pre- or post-inserts; full latency
// compensation of every path, reported as one constant latency.
//
// Threading: update()/setUserIR()/collect() are [ui] (one caller thread,
// they allocate). Built effects reach the audio thread through a per-slot
// atomic pointer and are adopted with a 3 ms fade; replaced instances go to
// a lock-free graveyard and are freed on the [ui] thread. process() is
// [render]: no allocation, no locks.
#pragma once

#include "Effect.hpp"
#include "FxDSP.hpp"
#include "../ParameterStore.hpp"
#include "../SPSCQueue.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace vektur::fx {

class EffectsEngine {
public:
    static constexpr uint32_t kSlots = 8;
    static constexpr uint32_t kMaxCompensation = 16384;   // samples per path
    static constexpr uint64_t kSlotBase = 39ull * VK_PARAMS_PER_MODULE;
    static constexpr uint64_t kBusBase = 47ull * VK_PARAMS_PER_MODULE;
    static uint64_t slotAddr(uint32_t slot, uint32_t index) noexcept { return kSlotBase + slot * VK_PARAMS_PER_MODULE + index; }

    EffectsEngine() = default;
    ~EffectsEngine();
    EffectsEngine(const EffectsEngine&) = delete;
    EffectsEngine& operator=(const EffectsEngine&) = delete;

    // [control] render stopped: sizes buffers and rebuilds every slot synchronously.
    void allocate(float sampleRate, uint32_t maxFrames, const ParameterStore& p);
    // [ui] rebuilds slots whose type / structural setting / user IR changed. Returns slots rebuilt.
    uint32_t update(const ParameterStore& p);
    // [ui] user IR for a slot's convolution reverb (resampled to the engine rate).
    void setUserIR(uint32_t slot, const float* left, const float* right, uint32_t frames, float irSampleRate);
    void clearUserIR(uint32_t slot);
    uint32_t userIRFrames(uint32_t slot) const noexcept { return slot < kSlots && userIR_[slot] ? static_cast<uint32_t>(userIR_[slot]->left.size()) : 0; }
    // [ui] frees replaced effect instances.
    void collect();

    // [render]
    void process(float* L, float* R, uint32_t n, const ParameterStore& p, const FxContext& c) noexcept;
    void reset() noexcept;

    // [any]
    uint32_t latency() const noexcept { return latency_.load(std::memory_order_relaxed); }
    float meter(uint32_t slot) const noexcept { return slot < kSlots ? meters_[slot].load(std::memory_order_relaxed) : 0.f; }
    uint32_t activeType(uint32_t slot) const noexcept { return slot < kSlots ? types_[slot].load(std::memory_order_relaxed) : 0; }
    // Test / diagnostics hooks.
    float slotGain(uint32_t s) const noexcept { return s < kSlots ? slots_[s].swapGain * slots_[s].enGain : 0.f; }
    float outputGain() const noexcept { return routeGain_ * globalEn_; }
    bool nonFinite() noexcept { return nonFinite_.exchange(false, std::memory_order_relaxed); }

private:
    struct Built { uint32_t type = 0; int structural = -1; uint32_t irGen = 0; };
    struct Slot {
        Effect* cur = nullptr;
        uint32_t type = 0, lat = 0;
        std::atomic<Effect*> pending{nullptr};
        int swap = 0;              // 0 idle, 1 fading out for a swap, 2 fading in
        float swapGain = 1.f, enGain = 1.f;
        uint64_t silent = 0;
        CompDelay dry[2], align[2];
    };
    struct Routing {
        uint8_t bus[kSlots]{}; uint8_t insertPar = 0, aPar = 0, bPar = 0, aPost = 1, bPost = 1;
        uint32_t lat[kSlots]{};
        bool operator==(const Routing& o) const noexcept;
    };

    int structuralOf(uint32_t type, uint32_t slot, const ParameterStore& p) const noexcept;
    Effect* build(uint32_t slot, const ParameterStore& p, Built& sig);
    Routing readRouting(const ParameterStore& p) const noexcept;
    uint32_t runChain(uint8_t bus, bool parallel, const float* inL, const float* inR, float* outL, float* outR,
                      uint32_t n, const ParameterStore& p, const FxContext& c) noexcept;
    void runSlot(uint32_t s, float* L, float* R, uint32_t n, const ParameterStore& p, const FxContext& c) noexcept;
    void adopt(uint32_t s) noexcept;

    float sr_ = 48000.f;
    uint32_t maxFrames_ = 0;
    std::array<Slot, kSlots> slots_;
    std::array<Built, kSlots> built_{};
    std::array<std::unique_ptr<ImpulseResponse>, kSlots> userIR_;
    uint32_t irGen_[kSlots]{};
    SPSCQueue<Effect*, 64> graveyard_;
    std::array<std::atomic<float>, kSlots> meters_{};
    std::array<std::atomic<uint32_t>, kSlots> types_{};
    std::atomic<uint32_t> latency_{0};
    std::atomic<bool> nonFinite_{false};

    Routing routing_{};
    bool routingFading_ = false;
    float routeGain_ = 1.f, globalEn_ = 1.f;
    uint32_t total_ = 0, latMain_ = 0, latA_ = 0, latB_ = 0;
    CompDelay alignMain_[2], alignA_[2], alignB_[2], bypass_[2];
    std::vector<float> dryL_, dryR_, mainL_, mainR_, aL_, aR_, bL_, bR_, tL_, tR_, accL_, accR_, sdL_, sdR_;
};

} // namespace vektur::fx
