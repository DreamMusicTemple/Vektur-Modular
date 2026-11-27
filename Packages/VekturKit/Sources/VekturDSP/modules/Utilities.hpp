// Utilities.hpp — VCA, noise, ring modulator, sample & hold, slew limiter,
// envelope follower, voltage processor and mixer modules.
#pragma once

#include "DSPCommon.hpp"

#include <array>

namespace vektur {

// ---------------------------------------------------------------------------
// VCA — linear or exponential gain law, optional saturation, CV bleed.
// Exponential law spans 60 dB (2^10) and is normalised so CV 0 → 0, 1 → 1.
// ---------------------------------------------------------------------------
class Vca {
public:
    enum class Response : uint8_t { Linear = 0, Exponential };
    static constexpr PortInfo kPorts[] = {
        {"in", PortKind::Audio, PortDir::In}, {"cv1", PortKind::CV, PortDir::In},
        {"cv2", PortKind::CV, PortDir::In}, {"out", PortKind::Audio, PortDir::Out},
    };

    static float gainLaw(float cv, Response r) noexcept {
        cv = clampf(cv, 0.f, 1.f);
        if (r == Response::Linear) return cv;
        return (std::exp2(10.f * cv) - 1.f) * (1.f / 1023.f);
    }

    // saturation [0,1]: 0 = clean, 1 = strong tanh. bleed: linear leakage (Authentic).
    static float process(float x, float cv, Response r, float saturation, float bleed) noexcept {
        float y = x * gainLaw(cv, r) + x * bleed;
        if (saturation > 0.f) {
            const float d = 1.f + 4.f * saturation;
            y = fastTanh(y * d) / fastTanh(d);
        }
        return y;
    }
};

// ---------------------------------------------------------------------------
// Noise — white or pink (Kellet economy filter), with a spectral tilt.
// colour ∈ [-1, 1]: negative = darker (low-pass blend), positive = brighter.
// ---------------------------------------------------------------------------
class Noise {
public:
    enum class Type : uint8_t { White = 0, Pink };
    static constexpr PortInfo kPorts[] = {
        {"out", PortKind::Audio, PortDir::Out}, {"random", PortKind::CV, PortDir::Out},
    };

    explicit Noise(uint32_t seed = 0x1234567u) noexcept : rng_(seed) {}
    void seed(uint32_t s) noexcept { rng_ = Rng(s); }
    void reset() noexcept { b0_ = b1_ = b2_ = lp_ = 0.f; }

    float white() noexcept { return rng_.bipolar(); }

    float pink() noexcept {
        const float w = rng_.bipolar();
        b0_ = 0.99765f * b0_ + w * 0.0990460f;
        b1_ = 0.96300f * b1_ + w * 0.2965164f;
        b2_ = 0.57000f * b2_ + w * 1.0526913f;
        return (b0_ + b1_ + b2_ + w * 0.1848f) * 0.22f;
    }

    float process(Type t, float colour) noexcept {
        const float x = t == Type::Pink ? pink() : white();
        if (colour == 0.f) return x;
        lp_ += 0.08f * (x - lp_);
        if (colour < 0.f) return x + (lp_ * 3.f - x) * (-colour);   // ×3 keeps level roughly constant
        return x * (1.f - colour) + (x - lp_) * colour * 1.1f;            // blend toward high-passed
    }

private:
    Rng rng_;
    float b0_ = 0.f, b1_ = 0.f, b2_ = 0.f, lp_ = 0.f;
};

// ---------------------------------------------------------------------------
// Ring modulator — four-quadrant multiplier. carrierLeak models the imperfect
// carrier suppression of analogue multipliers (0 = perfectly balanced).
// ---------------------------------------------------------------------------
struct RingMod {
    static constexpr PortInfo kPorts[] = {
        {"carrier", PortKind::Audio, PortDir::In}, {"modulator", PortKind::Audio, PortDir::In},
        {"out", PortKind::Audio, PortDir::Out},
    };
    static float process(float carrier, float modulator, float carrierLeak) noexcept {
        return carrier * modulator + carrier * carrierLeak;
    }
};

// ---------------------------------------------------------------------------
// Slew limiter / integrator / portamento.
// Exponential (RC lag, time ≈ 99 % settle) or linear (constant rate,
// time = full-scale 0→1 travel). Separate rise and fall times.
// ---------------------------------------------------------------------------
class Slew {
public:
    enum class Mode : uint8_t { Exponential = 0, Linear };
    static constexpr PortInfo kPorts[] = { {"in", PortKind::CV, PortDir::In}, {"out", PortKind::CV, PortDir::Out} };

    void prepare(double sr) noexcept { sr_ = sr; }
    void set(float riseMs, float fallMs, Mode m) noexcept {
        mode_ = m;
        if (m == Mode::Exponential) { rise_ = onePoleCoef(riseMs, sr_); fall_ = onePoleCoef(fallMs, sr_); }
        else {
            rise_ = riseMs <= 0.f ? 1e9f : static_cast<float>(1.0 / (static_cast<double>(riseMs) * 0.001 * sr_));
            fall_ = fallMs <= 0.f ? 1e9f : static_cast<float>(1.0 / (static_cast<double>(fallMs) * 0.001 * sr_));
        }
    }
    void reset(float v = 0.f) noexcept { y_ = v; }
    float value() const noexcept { return y_; }

    float process(float x) noexcept { return advance(x, 1); }

    // Advance n samples toward x (control-rate use).
    float advance(float x, uint32_t n) noexcept {
        const float d = x - y_;
        if (mode_ == Mode::Exponential) {
            const float c = d > 0.f ? rise_ : fall_;
            if (c >= 1.f) { y_ = x; return y_; }   // instant: land exactly on target
            const float cn = n == 1 ? c : 1.f - std::pow(1.f - c, static_cast<float>(n));
            y_ += d * cn;
        } else {
            const float step = (d > 0.f ? rise_ : fall_) * static_cast<float>(n);
            y_ += d > 0.f ? (d < step ? d : step) : (-d < step ? d : -step);
        }
        return y_;
    }

private:
    double sr_ = 48000.0;
    float rise_ = 1.f, fall_ = 1.f, y_ = 0.f;
    Mode mode_ = Mode::Exponential;
};

// ---------------------------------------------------------------------------
// Sample & hold — internal clock and/or external trigger, output slew.
// ---------------------------------------------------------------------------
class SampleHold {
public:
    static constexpr PortInfo kPorts[] = {
        {"in", PortKind::Audio, PortDir::In}, {"trigger", PortKind::Trigger, PortDir::In},
        {"out", PortKind::CV, PortDir::Out},
    };

    void prepare(double sr) noexcept { sr_ = sr; slew_.prepare(sr); }
    void reset() noexcept { phase_ = 0.0; held_ = 0.f; slew_.reset(0.f); lastTrig_ = false; }
    void setRate(float hz) noexcept { inc_ = static_cast<double>(hz) / sr_; }
    void setSlew(float ms) noexcept { slew_.set(ms, ms, Slew::Mode::Exponential); }

    // Advance by n frames; `input` is the signal value to sample. Returns true if it sampled.
    bool advance(uint32_t n, float input, bool externalTrigger = false) noexcept {
        bool sampled = false;
        phase_ += inc_ * n;
        if (phase_ >= 1.0) { phase_ -= static_cast<double>(static_cast<long>(phase_)); sampled = true; }
        if (externalTrigger && !lastTrig_) sampled = true;
        lastTrig_ = externalTrigger;
        if (sampled) held_ = input;
        slew_.advance(held_, n);
        return sampled;
    }
    // Frame within the next `n` at which the internal clock will fire, or -1.
    int32_t wrapOffset(uint32_t n) const noexcept {
        if (inc_ <= 0.0) return -1;
        const double need = std::ceil((1.0 - phase_) / inc_);
        if (need > static_cast<double>(n)) return -1;
        return static_cast<int32_t>(std::max(0.0, need - 1.0));
    }
    float held() const noexcept { return held_; }
    float value() const noexcept { return slew_.value(); }

private:
    double sr_ = 48000.0, phase_ = 0.0, inc_ = 0.0;
    float held_ = 0.f;
    bool lastTrig_ = false;
    Slew slew_;
};

// ---------------------------------------------------------------------------
// Envelope follower — peak or RMS detection, attack/release, threshold, gain.
// ---------------------------------------------------------------------------
class EnvelopeFollower {
public:
    enum class Detector : uint8_t { Peak = 0, Rms };
    static constexpr PortInfo kPorts[] = { {"in", PortKind::Audio, PortDir::In}, {"out", PortKind::CV, PortDir::Out} };

    void prepare(double sr) noexcept { sr_ = sr; }
    void set(float attackMs, float releaseMs, Detector d, float threshold, float gain) noexcept {
        att_ = onePoleCoef(attackMs, sr_); rel_ = onePoleCoef(releaseMs, sr_);
        det_ = d; thr_ = threshold; gain_ = gain;
    }
    void reset() noexcept { env_ = 0.f; }

    float process(float x) noexcept {
        const float in = det_ == Detector::Peak ? std::fabs(x) : x * x;
        env_ += (in - env_) * (in > env_ ? att_ : rel_);
        const float level = det_ == Detector::Peak ? env_ : std::sqrt(env_);
        const float above = level - thr_;
        return above > 0.f ? above * gain_ : 0.f;
    }

private:
    double sr_ = 48000.0;
    float att_ = 1.f, rel_ = 1.f, env_ = 0.f, thr_ = 0.f, gain_ = 1.f;
    Detector det_ = Detector::Peak;
};

// ---------------------------------------------------------------------------
// Voltage processor — two summed inputs, attenuverter, offset, inversion,
// symmetric limiting.
// ---------------------------------------------------------------------------
struct VoltageProcessor {
    static constexpr PortInfo kPorts[] = {
        {"in1", PortKind::CV, PortDir::In}, {"in2", PortKind::CV, PortDir::In}, {"out", PortKind::CV, PortDir::Out},
    };
    float gain1 = 1.f, gain2 = 0.f;   // attenuverters, -1 … +1 (or beyond for precision scaling)
    float offset = 0.f;
    bool invert = false;
    float limit = 2.f;                // ± ceiling (2.0 = ±10 V)

    float process(float a, float b = 0.f) const noexcept {
        float y = a * gain1 + b * gain2 + offset;
        if (invert) y = -y;
        return clampf(y, -limit, limit);
    }
};

// ---------------------------------------------------------------------------
// Mixer — N channels, per-channel gain, phase inversion and equal-power pan,
// summing to stereo. DC-coupled, so it mixes CV as well as audio.
// ---------------------------------------------------------------------------
template <int N>
class Mixer {
public:
    struct Channel { float gain = 0.f; float pan = 0.f; bool invert = false; };
    std::array<Channel, N> ch{};

    void process(const float* in, float& outL, float& outR) const noexcept {
        float l = 0.f, r = 0.f;
        for (int i = 0; i < N; ++i) {
            const float g = ch[i].invert ? -ch[i].gain : ch[i].gain;
            const float a = (clampf(ch[i].pan, -1.f, 1.f) + 1.f) * 0.25f * kPiF;   // 0 … π/2
            l += in[i] * g * std::cos(a);
            r += in[i] * g * std::sin(a);
        }
        outL = l; outR = r;
    }
    float processMono(const float* in) const noexcept {
        float s = 0.f;
        for (int i = 0; i < N; ++i) s += in[i] * (ch[i].invert ? -ch[i].gain : ch[i].gain);
        return s;
    }
};

} // namespace vektur
