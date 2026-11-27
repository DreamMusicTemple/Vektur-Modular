// ParameterStore.hpp — lock-free parameter storage plus render-side smoothing.
//
// Writers (UI, AUParameterTree observers, MIDI-learn later) store targets into
// a flat array of atomics indexed directly by parameter address. The render
// thread polls registered slots once per control block and ramps towards the
// target, so a knob turn or automation lane never produces a step.
#pragma once

#include "VekturDSP.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace vektur {

struct ParamInfo {
    uint64_t address = 0;
    float minValue = 0.f;
    float maxValue = 1.f;
    float defaultValue = 0.f;
    float smoothingMs = 10.f;
    uint32_t flags = 0;          // kParamFlagLog | kParamFlagModulatable
};

inline constexpr uint32_t kParamFlagLog = 1u;
inline constexpr uint32_t kParamFlagModulatable = 2u;

// Linear ramp evaluated at control rate; consumers may interpolate per sample.
struct ParamRamp {
    float current = 0.f;
    float target = 0.f;
    float step = 0.f;
    uint32_t remaining = 0;

    void snap(float v) noexcept { current = target = v; step = 0.f; remaining = 0; }

    void rampTo(float v, uint32_t frames) noexcept {
        target = v;
        if (frames == 0) { snap(v); return; }
        remaining = frames;
        step = (target - current) / static_cast<float>(frames);
    }

    // Advance by `frames`; returns the value at the end of the span.
    float advance(uint32_t frames) noexcept {
        if (remaining == 0) return current;
        if (frames >= remaining) { current = target; remaining = 0; step = 0.f; }
        else { current += step * static_cast<float>(frames); remaining -= frames; }
        return current;
    }
};

class ParameterStore {
public:
    ParameterStore() : values_(std::make_unique<std::atomic<float>[]>(VK_ADDRESS_SPACE)),
                       slotForAddress_(std::make_unique<int16_t[]>(VK_ADDRESS_SPACE)) {
        for (uint32_t i = 0; i < VK_ADDRESS_SPACE; ++i) {
            values_[i].store(0.f, std::memory_order_relaxed);
            slotForAddress_[i] = -1;
        }
    }

    // [control]
    bool registerParam(const ParamInfo& info) noexcept {
        if (info.address >= VK_ADDRESS_SPACE) return false;
        if (!(info.minValue <= info.maxValue)) return false;
        auto existing = slotForAddress_[info.address];
        int16_t slot = existing;
        if (slot < 0) {
            if (count_ >= VK_MAX_REGISTERED_PARAMS) return false;
            slot = static_cast<int16_t>(count_++);
            slotForAddress_[info.address] = slot;
        }
        const auto si = static_cast<uint32_t>(slot);
        if (si >= VK_MAX_REGISTERED_PARAMS) return false;
        ParamInfo clean = info;
        clean.defaultValue = std::clamp(info.defaultValue, info.minValue, info.maxValue);
        clean.smoothingMs = std::max(0.f, info.smoothingMs);
        infos_[si] = clean;
        values_[info.address].store(clean.defaultValue, std::memory_order_relaxed);
        lastSeen_[si] = clean.defaultValue;
        ramps_[si].snap(clean.defaultValue);
        return true;
    }

    // [control] — called from allocate(); converts ms → frames.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        for (uint32_t s = 0; s < count_; ++s) {
            const float v = values_[infos_[s].address].load(std::memory_order_relaxed);
            lastSeen_[s] = v;
            ramps_[s].snap(v);
        }
    }

    // [any]
    void set(uint64_t address, float value) noexcept {
        if (address >= VK_ADDRESS_SPACE) return;
        const int16_t slot = slotForAddress_[address];
        if (slot >= 0) {
            const auto& i = infos_[slot];
            if (!std::isfinite(value)) value = i.defaultValue;
            value = std::clamp(value, i.minValue, i.maxValue);
        }
        values_[address].store(value, std::memory_order_relaxed);
    }

    // [any]
    float get(uint64_t address) const noexcept {
        if (address >= VK_ADDRESS_SPACE) return 0.f;
        return values_[address].load(std::memory_order_relaxed);
    }

    // [any] Normalised (0…1, following the taper) ↔ value, for MIDI Learn.
    float normalise(uint64_t address, float value) const noexcept {
        const int16_t slot = address < VK_ADDRESS_SPACE ? slotForAddress_[address] : -1;
        if (slot < 0) return std::clamp(value, 0.f, 1.f);
        const auto& i = infos_[slot];
        if (!(i.maxValue > i.minValue)) return 0.f;
        if ((i.flags & kParamFlagLog) && i.minValue > 0.f)
            return std::clamp(std::log(std::max(value, i.minValue) / i.minValue) / std::log(i.maxValue / i.minValue), 0.f, 1.f);
        return std::clamp((value - i.minValue) / (i.maxValue - i.minValue), 0.f, 1.f);
    }
    float denormalise(uint64_t address, float n) const noexcept {
        const int16_t slot = address < VK_ADDRESS_SPACE ? slotForAddress_[address] : -1;
        n = std::clamp(n, 0.f, 1.f);
        if (slot < 0) return n;
        const auto& i = infos_[slot];
        if ((i.flags & kParamFlagLog) && i.minValue > 0.f) return i.minValue * std::pow(i.maxValue / i.minValue, n);
        return i.minValue + n * (i.maxValue - i.minValue);
    }
    bool isRegistered(uint64_t address) const noexcept { return address < VK_ADDRESS_SPACE && slotForAddress_[address] >= 0; }

    // [render] Host-scheduled automation: sample-accurate, optional explicit ramp.
    void applyEvent(uint64_t address, float value, uint32_t rampFrames, bool explicitRamp) noexcept {
        if (address >= VK_ADDRESS_SPACE) return;
        const int16_t slot = slotForAddress_[address];
        set(address, value);
        if (slot < 0) return;
        const float clamped = values_[address].load(std::memory_order_relaxed);
        lastSeen_[slot] = clamped;
        ramps_[slot].rampTo(clamped, explicitRamp ? rampFrames : defaultRampFrames(slot));
    }

    // [render] Poll atomics for asynchronous changes; call once per control block.
    void pollChanges() noexcept {
        for (uint32_t s = 0; s < count_; ++s) {
            const float v = values_[infos_[s].address].load(std::memory_order_relaxed);
            if (v != lastSeen_[s]) {
                lastSeen_[s] = v;
                ramps_[s].rampTo(v, defaultRampFrames(s));
            }
        }
    }

    // [render]
    void advance(uint32_t frames) noexcept {
        for (uint32_t s = 0; s < count_; ++s) ramps_[s].advance(frames);
    }

    // [render] Current smoothed value (falls back to the raw atomic if unregistered).
    float smoothed(uint64_t address) const noexcept {
        if (address >= VK_ADDRESS_SPACE) return 0.f;
        const int16_t slot = slotForAddress_[address];
        return slot >= 0 ? ramps_[slot].current : values_[address].load(std::memory_order_relaxed);
    }

    // [render] Snap every ramp to its target (used on reset / panic).
    void snapAll() noexcept {
        for (uint32_t s = 0; s < count_; ++s) ramps_[s].snap(lastSeen_[s]);
    }

    // [render] Smoothed value plus a per-voice matrix offset. `mod` is indexed
    // by registration slot; offsets are in normalised (taper) units, so ±1
    // sweeps the full range of any parameter, log-tapered ones in octaves.
    float modulated(uint64_t address, const float* mod) const noexcept {
        if (address >= VK_ADDRESS_SPACE) return 0.f;
        const int16_t slot = slotForAddress_[address];
        if (slot < 0) return values_[address].load(std::memory_order_relaxed);
        const float v = ramps_[slot].current;
        if (!mod) return v;
        const float m = mod[slot];
        if (m == 0.f) return v;
        const ParamInfo& i = infos_[slot];
        if (i.flags & kParamFlagLog) {
            const float span = std::log(i.maxValue / i.minValue);
            const float n = std::clamp(std::log(std::max(v, i.minValue) / i.minValue) / span + m, 0.f, 1.f);
            return i.minValue * std::exp(n * span);
        }
        return std::clamp(v + m * (i.maxValue - i.minValue), i.minValue, i.maxValue);
    }

    // [any]
    int32_t slotOf(uint64_t address) const noexcept {
        return address < VK_ADDRESS_SPACE ? slotForAddress_[address] : -1;
    }
    bool isModulatable(uint64_t address) const noexcept {
        const int32_t s = slotOf(address);
        return s >= 0 && (infos_[s].flags & kParamFlagModulatable);
    }
    // [control] Remove the modulatable flag (e.g. global input attenuators).
    void clearModulatable(uint64_t address) noexcept {
        const int32_t s = slotOf(address);
        if (s >= 0) infos_[s].flags &= ~kParamFlagModulatable;
    }

    uint32_t registeredCount() const noexcept { return count_; }

private:
    uint32_t defaultRampFrames(uint32_t slot) const noexcept {
        return static_cast<uint32_t>(std::lround(infos_[slot].smoothingMs * 0.001 * sampleRate_));
    }

    std::unique_ptr<std::atomic<float>[]> values_;
    std::unique_ptr<int16_t[]> slotForAddress_;
    std::array<ParamInfo, VK_MAX_REGISTERED_PARAMS> infos_{};
    std::array<float, VK_MAX_REGISTERED_PARAMS> lastSeen_{};
    std::array<ParamRamp, VK_MAX_REGISTERED_PARAMS> ramps_{};
    uint32_t count_ = 0;
    double sampleRate_ = 48000.0;
};

} // namespace vektur
