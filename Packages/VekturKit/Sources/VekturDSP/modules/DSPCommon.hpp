// DSPCommon.hpp — shared maths, random sources and the module port vocabulary.
#pragma once

#include <cmath>
#include <cstdint>

namespace vektur {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr float kPiF = 3.14159265f;

// ---------------------------------------------------------------------------
// Port vocabulary. Every module declares its ports with these so the Phase 3
// graph compiler can validate connections (type checking, direction).
// CV convention: 1.0 = 5 V. Pitch CV: 0.2 per octave (1 V/oct). Gates: 0/1.
// ---------------------------------------------------------------------------
enum class PortKind : uint8_t { Audio, CV, Gate, Trigger };
enum class PortDir : uint8_t { In, Out };
struct PortInfo { const char* name; PortKind kind; PortDir dir; };

// ---------------------------------------------------------------------------
// Fast, bounded tanh approximation (Padé 3/2, exact at 0, clamps to ±1 by |x|=3).
// Max error ≈ 2 % around |x|≈1.5, monotonic — used only inside saturators.
// ---------------------------------------------------------------------------
inline float fastTanh(float x) noexcept {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float dbToGain(float db) noexcept { return std::pow(10.f, db / 20.f); }

inline float clampf(float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }

// Coefficient for a one-pole smoother that reaches ~99 % in `ms`.
inline float onePoleCoef(float ms, double sampleRate) noexcept {
    if (ms <= 0.f) return 1.f;
    return 1.f - static_cast<float>(std::exp(-4.6 / (static_cast<double>(ms) * 0.001 * sampleRate)));
}

// ---------------------------------------------------------------------------
// Deterministic RNG (xorshift32). Seeded per voice/oscillator so behaviour is
// reproducible across renders, sessions and devices.
// ---------------------------------------------------------------------------
struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) noexcept : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    // Uniform in [-1, 1).
    float bipolar() noexcept { return static_cast<float>(static_cast<int32_t>(next())) * (1.f / 2147483648.f); }
    // Uniform in [0, 1).
    float unipolar() noexcept { return static_cast<float>(next() >> 8) * (1.f / 16777216.f); }
};

// Hash for deterministic per-voice tolerances.
inline uint32_t hash32(uint32_t x) noexcept {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}

} // namespace vektur
