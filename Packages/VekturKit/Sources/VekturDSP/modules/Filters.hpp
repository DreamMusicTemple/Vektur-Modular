// Filters.hpp — Filter A (2-pole state-variable multimode) and Filter B
// (4-pole ladder-inspired low-pass), plus the halfband oversampler used to
// run their nonlinear stages above the base sample rate.
//
// Both filters are zero-delay-feedback (topology-preserving transform)
// designs, so cutoff tracks accurately right up to Nyquist/2 of the internal
// rate and they stay stable under fast (audio-rate) modulation.
// They are circuit-INFORMED, not component-level recreations: the
// nonlinearities model the qualitative behaviour (soft input saturation,
// bounded self-oscillation, resonance bass loss) rather than a specific
// schematic. See docs/dsp/FILTERS.md.
#pragma once

#include "DSPCommon.hpp"

#include <array>

namespace vektur {

// ---------------------------------------------------------------------------
// Filter A — 2-pole SVF (Simper/Zavalishin trapezoidal integrators).
// Outputs LP/BP/HP/Notch simultaneously. Resonance reaches self-oscillation;
// the band-pass integrator state is soft-clipped so oscillation is bounded.
// ---------------------------------------------------------------------------
class SvfFilter {
public:
    enum class Mode : uint8_t { LowPass = 0, BandPass, HighPass, Notch };
    static constexpr PortInfo kPorts[] = {
        {"in", PortKind::Audio, PortDir::In}, {"cutoff", PortKind::CV, PortDir::In},
        {"lp", PortKind::Audio, PortDir::Out}, {"bp", PortKind::Audio, PortDir::Out},
        {"hp", PortKind::Audio, PortDir::Out}, {"notch", PortKind::Audio, PortDir::Out},
    };
    struct Outputs { float lp, bp, hp, notch; };

    void reset() noexcept { ic1_ = ic2_ = 0.f; }

    // g = tan(pi * fc / fsInternal); resonance in [0,1]; drive = linear input gain (≥1).
    void setCoefficients(float g, float resonance, float drive) noexcept {
        g_ = g;
        // r ≥ 0.95 → k ≤ 0: oscillation builds (fast enough to be playable) and is bounded by the clipper.
        k_ = 2.f - 2.1f * clampf(resonance, 0.f, 1.f);
        a1_ = 1.f / (1.f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
        drive_ = drive < 1.f ? 1.f : drive;
        makeup_ = 1.f / std::sqrt(drive_);
    }

    Outputs processAll(float x) noexcept {
        const float v0 = drive_ > 1.0001f ? fastTanh(x * drive_) * makeup_ * 1.25f : x;
        const float v3 = v0 - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.f * v1 - ic1_;
        ic2_ = 2.f * v2 - ic2_;
        // Bound the resonant loop (only engages at high levels / self-oscillation).
        ic1_ = 1.6f * fastTanh(ic1_ * 0.625f);
        const float hp = v0 - k_ * v1 - v2;
        return {v2, v1, hp, v2 + hp};
    }

    float process(float x, Mode m) noexcept {
        const Outputs o = processAll(x);
        switch (m) {
            case Mode::LowPass: return o.lp;
            case Mode::BandPass: return o.bp;
            case Mode::HighPass: return o.hp;
            case Mode::Notch: return o.notch;
        }
        return o.lp;
    }

private:
    float g_ = 0.1f, k_ = 2.f, a1_ = 1.f, a2_ = 0.f, a3_ = 0.f;
    float ic1_ = 0.f, ic2_ = 0.f, drive_ = 1.f, makeup_ = 1.f;
};

// ---------------------------------------------------------------------------
// Filter B — 4-pole (24 dB/oct) ladder-inspired low-pass.
// Four TPT one-pole stages with global feedback solved implicitly (ZDF); the
// ladder input is saturated (tanh), giving the characteristic level-dependent
// resonance and soft, bounded self-oscillation. Passband loss with resonance
// is only partly compensated in Authentic mode, as on the hardware.
// ---------------------------------------------------------------------------
class LadderFilter {
public:
    static constexpr PortInfo kPorts[] = {
        {"in", PortKind::Audio, PortDir::In}, {"cutoff", PortKind::CV, PortDir::In},
        {"out", PortKind::Audio, PortDir::Out},
    };

    void reset() noexcept { s_ = {0.f, 0.f, 0.f, 0.f}; }

    // g = tan(pi * fc / fsInternal); resonance [0,1]; drive linear ≥ 1;
    // bassCompensation [0,1]: share of resonance-induced passband loss to restore.
    void setCoefficients(float g, float resonance, float drive, float bassCompensation) noexcept {
        G_ = g / (1.f + g);
        beta_ = 1.f - G_;
        k_ = 4.6f * clampf(resonance, 0.f, 1.f);   // oscillation threshold k = 4 at resonance ≈ 0.87
        const float G2 = G_ * G_;
        G4_ = G2 * G2;
        drive_ = drive < 1.f ? 1.f : drive;
        comp_ = (1.f + k_ * clampf(bassCompensation, 0.f, 1.f)) / std::sqrt(drive_);
    }

    float process(float x) noexcept {
        const float G = G_, b = beta_;
        const float S = G * G * G * b * s_[0] + G * G * b * s_[1] + G * b * s_[2] + b * s_[3];
        float u = (x * drive_ - k_ * S) / (1.f + k_ * G4_);
        u = fastTanh(u);
        float y = u;
        for (int i = 0; i < 4; ++i) {
            const float v = (y - s_[i]) * G;
            const float out = v + s_[i];
            s_[i] = out + v;
            y = out;
        }
        return y * comp_;
    }

private:
    std::array<float, 4> s_{0.f, 0.f, 0.f, 0.f};
    float G_ = 0.1f, beta_ = 0.9f, k_ = 0.f, G4_ = 0.f, drive_ = 1.f, comp_ = 1.f;
};

// ---------------------------------------------------------------------------
// 2× halfband stage (31-tap windowed-sinc, polyphase). ~70 dB stopband.
// Upsample produces 2 samples per input; downsample consumes 2 per output.
// ---------------------------------------------------------------------------
class Halfband2x {
public:
    static constexpr int kTaps = 31;
    static constexpr int kHalf = 16;   // nonzero even-index taps

    Halfband2x() noexcept { reset(); }

    void reset() noexcept { up_.fill(0.f); odd_.fill(0.f); even_.fill(0.f); upPos_ = oddPos_ = evenPos_ = 0; }

    // Histories are stored twice (mirror) so the newest-first window is always
    // contiguous: no modulo in the inner loops.
    void upsample(float x, float& out0, float& out1) noexcept {
        upPos_ = (upPos_ + kHalf - 1) % kHalf;
        up_[static_cast<size_t>(upPos_)] = up_[static_cast<size_t>(upPos_ + kHalf)] = x;
        const float* h = &up_[static_cast<size_t>(upPos_)];
        const auto& c = coef();
        float acc = 0.f;
        for (int j = 0; j < kHalf; ++j) acc += c[static_cast<size_t>(j)] * h[j];
        out0 = 2.f * acc;
        out1 = h[7];                                   // centre tap (0.5 × 2)
    }

    float downsample(float w0, float w1) noexcept {
        evenPos_ = (evenPos_ + 7) % 8;
        even_[static_cast<size_t>(evenPos_)] = even_[static_cast<size_t>(evenPos_ + 8)] = w0;
        oddPos_ = (oddPos_ + kHalf - 1) % kHalf;
        odd_[static_cast<size_t>(oddPos_)] = odd_[static_cast<size_t>(oddPos_ + kHalf)] = w1;
        const float* h = &odd_[static_cast<size_t>(oddPos_)];
        const auto& c = coef();
        float acc = 0.f;
        for (int j = 0; j < kHalf; ++j) acc += c[static_cast<size_t>(j)] * h[j];
        return acc + 0.5f * even_[static_cast<size_t>(evenPos_ + 7)];
    }

    // Even taps h[0], h[2], … h[30] of a 31-tap halfband (centre h[15] = 0.5).
    static const std::array<float, kHalf>& coef() noexcept {
        static const std::array<float, kHalf> c = [] {
            std::array<double, kTaps> h{};
            for (int n = 0; n < kTaps; ++n) {
                const int m = n - 15;
                const double sinc = m == 0 ? 1.0 : std::sin(kPi * m / 2.0) / (kPi * m / 2.0);
                const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * (n + 1) / (kTaps + 1))
                               + 0.08 * std::cos(4.0 * kPi * (n + 1) / (kTaps + 1));   // Blackman
                h[static_cast<size_t>(n)] = 0.5 * sinc * w;
            }
            // Exact halfband: even taps sum to 0.5, centre is 0.5 → unity DC gain.
            double sumEven = 0.0;
            for (int j = 0; j < kHalf; ++j) sumEven += h[static_cast<size_t>(2 * j)];
            std::array<float, kHalf> even{};
            for (int j = 0; j < kHalf; ++j) even[static_cast<size_t>(j)] = static_cast<float>(h[static_cast<size_t>(2 * j)] * 0.5 / sumEven);
            return even;
        }();
        return c;
    }

private:
    std::array<float, 2 * kHalf> up_{}, odd_{};
    std::array<float, 16> even_{};
    int upPos_ = 0, oddPos_ = 0, evenPos_ = 0;
};

// 1× / 2× / 4× wrapper. `f` is called factor times per input sample.
class Oversampler {
public:
    void setFactor(int f) noexcept {
        const int nf = f >= 4 ? 4 : (f >= 2 ? 2 : 1);
        if (nf != factor_) { factor_ = nf; a_.reset(); b1_.reset(); b2_.reset(); }
    }
    int factor() const noexcept { return factor_; }
    void reset() noexcept { a_.reset(); b1_.reset(); b2_.reset(); }

    template <typename F>
    float process(float x, F&& f) noexcept {
        if (factor_ == 1) return f(x);
        float u0, u1;
        a_.upsample(x, u0, u1);
        // f is stateful (a filter or an oscillator), so every call is sequenced
        // explicitly: the evaluation order of function arguments is unspecified.
        if (factor_ == 2) { const float y0 = f(u0); const float y1 = f(u1); return a_.downsample(y0, y1); }
        float q0, q1, q2, q3;
        b1_.upsample(u0, q0, q1);
        b1_.upsample(u1, q2, q3);
        const float r0 = f(q0), r1 = f(q1), r2 = f(q2), r3 = f(q3);   // declarators are sequenced in order
        const float d0 = b2_.downsample(r0, r1);
        const float d1 = b2_.downsample(r2, r3);
        return a_.downsample(d0, d1);
    }

private:
    // a_ handles base↔2×. b1_ (up) and b2_ (down) handle 2×↔4× with separate
    // state because both directions run in the same 2× sample stream.
    Halfband2x a_, b1_, b2_;
    int factor_ = 1;
};

} // namespace vektur
