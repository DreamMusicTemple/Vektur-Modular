// ModMatrix.hpp — LFOs, modulation sources and the 16-slot modulation matrix.
//
// The kernel decodes the matrix parameters and the global (channel-wide)
// sources once per control block into a ModFrame. Each voice then evaluates
// the slots with its own per-note sources (EGs, velocity, key, keyed LFOs,
// poly aftertouch, per-note random) and writes normalised offsets into a
// per-voice table indexed by parameter registration slot, which
// Module::P() applies through ParameterStore::modulated().
//
// Source ids are PERMANENT (mirrored in tools/params.py MOD_SOURCES and
// VekturCore/ModMatrix.swift). Real-time safe: no allocation, no locks.
#pragma once

#include "VekturDSP.h"
#include "DSPCommon.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vektur {

enum ModSource : uint8_t {
    MS_NONE = 0, MS_LFO1, MS_LFO2, MS_LFO3, MS_EG1, MS_EG2, MS_VELOCITY, MS_KEY,
    MS_MODWHEEL, MS_AFTERTOUCH, MS_POLY_AT, MS_BEND, MS_BREATH, MS_EXPRESSION,
    MS_MACRO1, MS_MACRO2, MS_MACRO3, MS_MACRO4, MS_RANDOM, MS_SPREAD, MS_CONSTANT,
    MS_TIMBRE, MS_ZONE,   // Phase 4B (appended)
    MS_SEQ_A, MS_SEQ_B, MS_SEQ_C,   // Phase 5 (appended): analogue sequencer rows, stepped, 0…1
    MS_COUNT
};
static_assert(MS_COUNT == VK_MOD_SOURCE_COUNT, "keep in step with tools/params.py MOD_SOURCES");

// Natural polarity of each source (true = bipolar −1…1, false = unipolar 0…1).
inline constexpr bool kSourceBipolar[MS_COUNT] = {
    false, true, true, true, false, false, false, true,
    false, false, false, true, false, false,
    false, false, false, false, true, true, false,
    false, false,
    false, false, false,
};

// Beats per LFO cycle for each tempo-sync division (index 0 = off).
inline constexpr float kSyncBeats[16] = {
    0.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f, 0.25f, 0.125f,
    3.f, 1.5f, 0.75f, 4.f / 3.f, 2.f / 3.f, 1.f / 3.f, 1.f / 6.f,
};

enum class ModDestKind : uint8_t { None, Param, Pitch, Amp, Pan, CutoffAll };

struct ModSlotCfg {
    uint8_t src = 0, via = 0, curve = 0, polarity = 0;
    bool invert = false;
    ModDestKind kind = ModDestKind::None;
    int32_t paramSlot = -1;   // registration slot when kind == Param
    float amount = 0.f;       // −1 … 1 (normalised range units)
    float offset = 0.f;       // −1 … 1
    float slewMs = 0.f;
    uint8_t index = 0;        // matrix slot number (0…15), for per-voice slew state
};

// Matrix scaling of the virtual destinations.
inline constexpr float kPitchModSemis = 12.f;   // amount 100 % = ±1 octave
inline constexpr uint32_t kMaxCutoffSlots = 8;

struct LfoCfg {
    uint8_t shape = 0;
    bool keyed = false;
    bool used = false;        // referenced by the matrix (rack modules add their own demand)
    float delayMs = 0.f;
    float phase = 0.f;        // 0 … 1
    uint64_t rateAddr = 0, levelAddr = 0;
    float syncedRate = 0.f;   // Hz when tempo-synced, 0 = use the rate parameter
};

// Built by the kernel once per control block; shared read-only by every voice.
struct ModFrame {
    ModSlotCfg slots[VK_MOD_SLOTS]{};
    uint32_t slotCount = 0;                         // active slots, packed
    float global[MS_COUNT]{};                       // channel-wide sources
    LfoCfg lfo[3]{};
    float lfoFree[3][VK_CONTROL_BLOCK]{};          // free-running LFO output (shared phase)
    int32_t cutoffSlots[kMaxCutoffSlots]{};
    uint32_t cutoffCount = 0;
};

// ---------------------------------------------------------------------------
// Low-frequency oscillator (control/audio rate, naive shapes; ≤ 50 Hz).
// ---------------------------------------------------------------------------
class Lfo {
public:
    void reset(float phase01, Rng& rng) noexcept {
        phase_ = phase01 - std::floor(phase01);
        from_ = rng.bipolar(); to_ = rng.bipolar(); held_ = from_;
    }
    void run(float* out, uint32_t n, float rateHz, int shape, double sr, Rng& rng) noexcept {
        const float inc = static_cast<float>(std::clamp(static_cast<double>(rateHz), 0.0, 1000.0) / sr);
        for (uint32_t i = 0; i < n; ++i) {
            out[i] = value(shape);
            phase_ += inc;
            if (phase_ >= 1.f) {
                phase_ -= std::floor(phase_);
                from_ = to_; to_ = rng.bipolar(); held_ = rng.bipolar();
            }
        }
    }
    float phase() const noexcept { return phase_; }
    void setPhase(float p) noexcept { phase_ = p - std::floor(p); }
private:
    float value(int shape) const noexcept {
        const float p = phase_;
        switch (shape) {
            case 0: return std::sin(2.f * kPiF * p);
            case 1: return p < 0.25f ? 4.f * p : (p < 0.75f ? 2.f - 4.f * p : 4.f * p - 4.f);   // 0 rising, like sine
            case 2: return 2.f * p - 1.f;
            case 3: return 1.f - 2.f * p;
            case 4: return p < 0.5f ? 1.f : -1.f;
            case 5: return held_;
            case 6: { const float t = 0.5f - 0.5f * std::cos(kPiF * p); return from_ + (to_ - from_) * t; }
            default: return 0.f;
        }
    }
    float phase_ = 0.f, from_ = 0.f, to_ = 0.f, held_ = 0.f;
};

// Shapes the raw source value of one slot (polarity → curve → invert).
inline float shapeModValue(float x, bool bipolarSource, const ModSlotCfg& c) noexcept {
    bool bip = bipolarSource;
    if (c.polarity == 1 && bip) { x = 0.5f * (x + 1.f); bip = false; }
    else if (c.polarity == 2 && !bip) { x = 2.f * x - 1.f; bip = true; }
    const float a = std::fabs(x), sg = x < 0.f ? -1.f : 1.f;
    switch (c.curve) {
        case 1: x = sg * a * a; break;                        // exponential
        case 2: x = sg * std::sqrt(a); break;                 // logarithmic
        case 3: {                                             // S-curve
            if (bip) { const float y = clampf(x, -1.f, 1.f); x = 0.5f * y * (3.f - y * y); }
            else { const float y = clampf(x, 0.f, 1.f); x = y * y * (3.f - 2.f * y); }
            break;
        }
        default: break;
    }
    if (c.invert) x = bip ? -x : 1.f - x;
    return x;
}

} // namespace vektur
