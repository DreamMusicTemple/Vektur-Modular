// MidiLearn.hpp — Phase 7 MIDI Learn: a lock-free published mapping table
// and the per-mapping value logic (range/invert, absolute with pickup,
// toggle, three relative encodings). Real-time safe.
#pragma once

#include "VekturDSP.h"
#include "../ParameterStore.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vektur::midi {

struct MappingTable {
    std::array<VKMidiMapping, VK_MIDI_MAX_MAPPINGS> m{};
    uint32_t count = 0;
    uint32_t generation = 0;
};

// Triple buffer: [ui] writes and publishes, [render] adopts the newest.
class MappingExchange {
public:
    MappingExchange() noexcept { state_.store(1, std::memory_order_relaxed); }
    void publish(const VKMidiMapping* src, uint32_t n) noexcept {
        MappingTable& t = slots_[back_];
        t.count = 0;
        for (uint32_t i = 0; i < n && i < VK_MIDI_MAX_MAPPINGS; ++i) {
            VKMidiMapping m = src[i];
            if (m.source > VK_MIDI_SRC_NOTE || m.address >= VK_ADDRESS_SPACE) continue;
            if (!(m.minimum == m.minimum) || !(m.maximum == m.maximum)) continue;
            m.minimum = std::fmin(std::fmax(m.minimum, 0.f), 1.f);
            m.maximum = std::fmin(std::fmax(m.maximum, 0.f), 1.f);
            if (!(m.sensitivity > 0.f) || m.sensitivity > 1.f) m.sensitivity = 1.f / 128.f;
            t.m[t.count++] = m;
        }
        t.generation = ++gen_;
        const uint8_t prev = state_.exchange(static_cast<uint8_t>(back_ | 4), std::memory_order_acq_rel);
        back_ = prev & 3;
    }
    // Returns true when a new table was adopted.
    bool acquire() noexcept {
        if (!(state_.load(std::memory_order_acquire) & 4)) return false;
        const uint8_t prev = state_.exchange(front_, std::memory_order_acq_rel);
        front_ = prev & 3;
        return true;
    }
    const MappingTable& front() const noexcept { return slots_[front_]; }
private:
    std::array<MappingTable, 3> slots_{};
    std::atomic<uint8_t> state_{1};
    uint8_t front_ = 0, back_ = 2;
    uint32_t gen_ = 0;
};

struct MappingState {
    bool picked = false;      // pickup: controller has reached the parameter
    float lastIn = -1.f;      // last normalised controller value
    float lastSet = -1.f;     // last normalised value we wrote
    bool high = false;        // toggle / note edge detection
};

// Decodes a 7-bit relative step.
inline int relativeStep(uint8_t v, uint8_t encoding) noexcept {
    switch (encoding) {
        case VK_MIDI_REL_BINARY_OFFSET: return static_cast<int>(v) - 64;
        case VK_MIDI_REL_SIGNED_BIT: return (v & 0x40) ? -static_cast<int>(v & 0x3F) : static_cast<int>(v & 0x3F);
        default: return v < 64 ? static_cast<int>(v) : static_cast<int>(v) - 128;
    }
}

// Applies one incoming value to one mapping. `in01` is the controller value
// (0…1, any resolution); `raw7` the 7-bit value (relative modes); `gate`
// true for note-on / pressed. Returns true and the new parameter value when
// the parameter should change.
inline bool apply(const VKMidiMapping& m, MappingState& st, float in01, uint8_t raw7, bool gate,
                  const ParameterStore& ps, float& out) noexcept {
    const float cur = ps.normalise(m.address, ps.get(m.address));
    // Something else (host, UI, automation) moved the parameter: re-arm pickup.
    if (st.lastSet >= 0.f && std::fabs(cur - st.lastSet) > 1e-4f) st.picked = false;
    float n;
    switch (m.mode) {
        case VK_MIDI_MODE_TOGGLE: {
            const bool press = m.source == VK_MIDI_SRC_NOTE ? gate : in01 >= 0.5f;
            const bool rising = press && !st.high;
            st.high = press;
            if (!rising) return false;
            const float lo = std::fmin(m.minimum, m.maximum), hi = std::fmax(m.minimum, m.maximum);
            n = std::fabs(cur - hi) < std::fabs(cur - lo) ? lo : hi;   // flip to the other end
            break;
        }
        case VK_MIDI_MODE_RELATIVE: {
            const int step = relativeStep(raw7, m.relative);
            if (step == 0) return false;
            const float dir = m.maximum >= m.minimum ? 1.f : -1.f;
            const float lo = std::fmin(m.minimum, m.maximum), hi = std::fmax(m.minimum, m.maximum);
            n = std::fmin(hi, std::fmax(lo, cur + dir * static_cast<float>(step) * m.sensitivity));
            break;
        }
        default: {
            const float x = m.source == VK_MIDI_SRC_NOTE ? (gate ? 1.f : 0.f) : in01;
            n = m.minimum + (m.maximum - m.minimum) * x;
            if (m.pickup && !st.picked) {
                // Picked up when the controller reaches or crosses the parameter's position.
                const float prev = st.lastIn;
                const bool crossed = prev >= 0.f && ((prev - cur) * (n - cur) <= 0.f);
                if (std::fabs(n - cur) < 0.02f || crossed) st.picked = true;
                st.lastIn = n;
                if (!st.picked) return false;
            }
            st.lastIn = n;
            break;
        }
    }
    st.lastSet = n;
    out = ps.denormalise(m.address, n);
    return true;
}

} // namespace vektur::midi
