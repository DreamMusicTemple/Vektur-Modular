// Graph.hpp — compiled patch description and the patch compiler.
//
// A CompiledGraph is plain data (no pointers, no heap), so it can be built on
// the UI thread into a preallocated slot and handed to the audio thread through
// a lock-free triple buffer. Module instances are persistent and owned by each
// voice: recompiling never resets oscillator phases or filter states.
#pragma once

#include "Rack.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace vektur {

inline constexpr uint32_t kMaxEdges = kMaxNormals + VK_MAX_CABLES;
inline constexpr uint32_t kMaxFeedback = 16;
inline constexpr uint8_t kNoFeedback = 0xFF;

struct CompiledEdge {
    uint8_t src = 0, srcPort = 0, dst = 0, dstPort = 0;
    GainSpec gain{};
    float cableGain = 1.f;
    uint16_t slot = 0;                  // telemetry slot
    uint8_t feedback = kNoFeedback;     // index into the voice's feedback buffers
    bool user = false;
};

struct CompiledGraph {
    uint32_t generation = 0;
    uint8_t order[kMaxInstances]{};     // processing order (needed modules only)
    uint32_t orderCount = 0;
    bool needed[kMaxInstances]{};
    bool userOut[kMaxInstances]{};      // has a user cable from one of its outputs
    CompiledEdge edges[kMaxEdges]{};    // sorted by (dst, dstPort)
    uint32_t edgeCount = 0;
    uint16_t inStart[kMaxInstances][kMaxPorts]{};
    uint8_t inCount[kMaxInstances][kMaxPorts]{};
    uint32_t feedbackCount = 0;
};

// Validates and compiles. Never throws, never allocates. Returns 0 when every
// cable was accepted, otherwise the number of rejected cables (which are
// simply left out of the compiled graph).
int32_t compilePatch(const VKCable* cables, uint32_t count, CompiledGraph& out, VKPatchResult& result) noexcept;

// Lock-free triple buffer: one writer ([ui]) and one reader ([render]).
class GraphExchange {
public:
    GraphExchange() noexcept { state_.store(packState(1, false), std::memory_order_relaxed); }
    CompiledGraph& writeSlot() noexcept { return slots_[back_]; }
    void publish() noexcept {
        const uint8_t prev = state_.exchange(packState(back_, true), std::memory_order_acq_rel);
        back_ = prev & 0x3;
    }
    bool hasPending() const noexcept { return (state_.load(std::memory_order_acquire) & 0x4) != 0; }
    // Reader: adopt the newest published graph if any. Returns true if it changed.
    bool acquire() noexcept {
        if (!hasPending()) return false;
        const uint8_t prev = state_.exchange(packState(front_, false), std::memory_order_acq_rel);
        front_ = prev & 0x3;
        return true;
    }
    const CompiledGraph& front() const noexcept { return slots_[front_]; }

private:
    static uint8_t packState(uint8_t idx, bool dirty) noexcept { return static_cast<uint8_t>(idx | (dirty ? 0x4 : 0)); }
    std::array<CompiledGraph, 3> slots_{};
    std::atomic<uint8_t> state_{0};   // middle slot index | dirty flag
    uint8_t front_ = 0, back_ = 2;    // owned by reader / writer respectively
};

} // namespace vektur
