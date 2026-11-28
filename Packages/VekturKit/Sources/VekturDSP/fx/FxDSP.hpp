// FxDSP.hpp — Phase 6 effect building blocks: filters, delay lines,
// oscillators, envelope followers, saturators. Buffers are sized in
// prepare-style constructors on the control thread; processing never
// allocates.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vektur::fx {

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain(float db) noexcept { return db <= -120.f ? 0.f : std::pow(10.f, db * 0.05f); }
inline float gainToDb(float g) noexcept { return g <= 1e-9f ? -180.f : 20.f * std::log10(g); }
inline float clampf(float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
// Per-sample one-pole coefficient for a time constant in ms.
inline float tauCoef(float ms, float sr) noexcept { return ms <= 0.f ? 0.f : std::exp(-1.f / (0.001f * ms * sr)); }
inline float flushDenormal(float x) noexcept { return std::fabs(x) < 1e-20f ? 0.f : x; }

// Cheap smooth saturator (rational tanh approximation, exact at 0, |y| < 1).
inline float softClip(float x) noexcept {
    x = clampf(x, -3.f, 3.f);
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

// tanh with first-order antiderivative anti-aliasing (ADAA).
struct AdaaTanh {
    float prev = 0.f;
    static float F(float x) noexcept { return std::log(std::cosh(clampf(x, -30.f, 30.f))); }
    float process(float x) noexcept {
        const float d = x - prev;
        float y;
        if (std::fabs(d) < 1e-4f) y = std::tanh(0.5f * (x + prev));
        else y = (F(x) - F(prev)) / d;
        prev = x;
        return y;
    }
    void reset() noexcept { prev = 0.f; }
};

struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return static_cast<float>(next() >> 8) * (1.f / 16777216.f); }   // 0…1
    float bip() noexcept { return 2.f * uni() - 1.f; }
};

// --------------------------------------------------------------------------
// Filters
// --------------------------------------------------------------------------
struct OnePole {   // TPT one-pole: low-pass and high-pass outputs
    float g = 0.f, s = 0.f;
    void set(float hz, float sr) noexcept { const float w = std::tan(kPi * clampf(hz, 1.f, 0.49f * sr) / sr); g = w / (1.f + w); }
    float lp(float x) noexcept { const float v = (x - s) * g; const float y = v + s; s = flushDenormal(y + v); return y; }
    float hp(float x) noexcept { return x - lp(x); }
    void reset() noexcept { s = 0.f; }
};

struct Biquad {
    enum Type : uint8_t { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf, Notch, AllPass };
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;
    float lastF = -1.f, lastQ = -1.f, lastG = -999.f; uint8_t lastT = 255;

    // RBJ cookbook. q: Q (shelves: slope-ish, 0.707 = Butterworth).
    void set(Type t, float hz, float q, float gainDb, float sr) noexcept {
        if (hz == lastF && q == lastQ && gainDb == lastG && t == lastT) return;
        lastF = hz; lastQ = q; lastG = gainDb; lastT = t;
        const float f = clampf(hz, 5.f, 0.495f * sr);
        const float w = kTwoPi * f / sr, cw = std::cos(w), sw = std::sin(w);
        const float Q = std::max(q, 0.05f);
        const float alpha = sw / (2.f * Q);
        const float A = std::pow(10.f, gainDb / 40.f);
        float B0, B1, B2, A0, A1, A2;
        switch (t) {
            case LowPass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case HighPass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case BandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Notch:    B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case AllPass:  B0 = 1 - alpha; B1 = -2 * cw; B2 = 1 + alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case LowShelf: {
                const float sq = 2.f * std::sqrt(A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + sq); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sq);
                A0 = (A + 1) + (A - 1) * cw + sq; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sq; break;
            }
            default: {   // HighShelf
                const float sq = 2.f * std::sqrt(A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + sq); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sq);
                A0 = (A + 1) - (A - 1) * cw + sq; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - sq; break;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    float process(float x) noexcept {
        const float y = b0 * x + z1;
        z1 = flushDenormal(b1 * x - a1 * y + z2);
        z2 = flushDenormal(b2 * x - a2 * y);
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.f; }
    // |H(e^jw)| in dB (tests, UI curves).
    float magnitudeDb(float hz, float sr) const noexcept {
        const double w = 2.0 * 3.141592653589793 * hz / sr;
        const double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        return static_cast<float>(10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di)));
    }
};

// --------------------------------------------------------------------------
// Delay line (power-of-two ring, fractional reads)
// --------------------------------------------------------------------------
class DelayLine {
public:
    void allocate(uint32_t maxSamples) {
        uint32_t n = 1; while (n < maxSamples + 4) n <<= 1;
        buf_.assign(n, 0.f); mask_ = n - 1; w_ = 0;
    }
    void reset() noexcept { std::fill(buf_.begin(), buf_.end(), 0.f); w_ = 0; }
    uint32_t capacity() const noexcept { return mask_ ? mask_ - 3 : 0; }
    void write(float x) noexcept { buf_[w_] = x; w_ = (w_ + 1) & mask_; }
    // Sample written `d` samples ago (d ≥ 1 is the previous write).
    float tap(uint32_t d) const noexcept { return buf_[(w_ - std::min(d, capacity())) & mask_]; }
    float readLinear(float d) const noexcept {
        d = clampf(d, 1.f, static_cast<float>(capacity()));
        const auto i = static_cast<uint32_t>(d); const float f = d - static_cast<float>(i);
        return lerp(tap(i), tap(i + 1), f);
    }
    float readCubic(float d) const noexcept {   // Hermite, d ≥ 2
        d = clampf(d, 2.f, static_cast<float>(capacity()) - 2.f);
        const auto i = static_cast<uint32_t>(d); const float f = d - static_cast<float>(i);
        const float xm1 = tap(i - 1), x0 = tap(i), x1 = tap(i + 1), x2 = tap(i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
private:
    std::vector<float> buf_;
    uint32_t mask_ = 0, w_ = 0;
};

// Fixed integer delay for latency compensation.
class CompDelay {
public:
    void allocate(uint32_t maxSamples) { line_.allocate(maxSamples + 1); }
    void reset() noexcept { line_.reset(); }
    float process(float x, uint32_t d) noexcept { line_.write(x); return d == 0 ? x : line_.tap(d + 1); }
private:
    DelayLine line_;
};

// Schroeder all-pass on its own line (reverbs, phasers, springs).
struct AllPassLine {
    DelayLine line;
    void allocate(uint32_t n) { line.allocate(n); }
    float process(float x, float delay, float g) noexcept {
        const float d = line.readLinear(delay);
        const float v = x + g * d;
        line.write(flushDenormal(v));
        return d - g * v;
    }
    void reset() noexcept { line.reset(); }
};

// First-order all-pass section (phasers, dispersion).
struct AllPass1 {
    float z = 0.f;
    float process(float x, float a) noexcept { const float y = a * x + z; z = flushDenormal(x - a * y); return y; }
    void reset() noexcept { z = 0.f; }
};

// --------------------------------------------------------------------------
// Modulation & dynamics helpers
// --------------------------------------------------------------------------
struct SineLfo {
    double phase = 0.0;
    float next(float hz, float sr, float offset01 = 0.f) noexcept {
        const float v = std::sin(kTwoPi * static_cast<float>(phase + offset01));
        phase += static_cast<double>(hz) / sr; phase -= std::floor(phase);
        return v;
    }
    float value(float offset01) const noexcept { return std::sin(kTwoPi * static_cast<float>(phase + offset01)); }
    void advance(float hz, float sr, uint32_t n) noexcept { phase += static_cast<double>(hz) * n / sr; phase -= std::floor(phase); }
};

struct EnvFollower {
    float env = 0.f;
    float process(float x, float att, float rel) noexcept {   // att/rel: per-sample coefficients
        const float a = std::fabs(x);
        env = a > env ? att * env + (1.f - att) * a : rel * env + (1.f - rel) * a;
        env = flushDenormal(env);
        return env;
    }
};

// Seconds of a tempo-sync choice (0 = free → returns `freeSecs`).
// Beats per choice of FxSpecs kSync: Free, 4/2/1 bars, 1/2 … 1/32, dotted, triplets.
inline constexpr double kSyncBeats[16] = {0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                          3.0, 1.5, 0.75, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0};
inline float syncSeconds(int choice, double tempo, float freeSecs) noexcept {
    if (choice <= 0 || choice > 15) return freeSecs;
    return static_cast<float>(kSyncBeats[choice] * 60.0 / std::max(tempo, 1.0));
}

} // namespace vektur::fx
