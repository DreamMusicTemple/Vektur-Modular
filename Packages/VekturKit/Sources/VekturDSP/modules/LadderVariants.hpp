// LadderVariants.hpp — two further 4-pole low-pass characters.
//
// DiodeLadder: four capacitors coupled through a resistive/diode ladder (each
//   stage loads the next), solved implicitly (trapezoidal, ZDF) as a 4×4
//   linear system with global feedback, plus input and state saturation. The
//   stage coupling gives the softer, less "squelchy" slope and the vocal,
//   rubbery resonance of diode-ladder synthesisers. Circuit-informed: an
//   equal-value RC ladder, not a component-exact model.
//
// TransistorLadder: per-stage tanh saturation (every transistor pair clips),
//   explicit integration run at 2× internally with a half-sample-averaged
//   feedback path (after Huovilainen's improved model). Grittier and more
//   level-dependent than Filter B, which saturates only at the ladder input.
#pragma once

#include "DSPCommon.hpp"

#include <array>
#include <complex>

namespace vektur {

class DiodeLadder {
public:
    static constexpr PortInfo kPorts[] = {
        {"in", PortKind::Audio, PortDir::In}, {"cutoff", PortKind::CV, PortDir::In}, {"out", PortKind::Audio, PortDir::Out},
    };

    // Normalised (ω = 1) state matrix of the coupled ladder and its constants,
    // computed once: -3 dB frequency (cutoff calibration) and the feedback gain
    // at which the loop self-oscillates.
    // Each stage is driven by the previous one and loaded by the next with
    // coupling c (0 = buffered cascade like a transistor ladder, 1 = passive RC
    // ladder). Diode ladders sit in between; c = 0.5 gives their characteristic
    // slightly-spread poles while keeping a steep slope.
    static constexpr double kCoupling = 0.5;
    struct Design {
        double A[4][4]{};
        double w180 = 1.0;      // frequency where the ladder's phase reaches −180° (the resonant peak)
        double kCrit = 1.0;     // feedback gain at the self-oscillation threshold
    };
    static const Design& design() noexcept {
        static const Design d = [] {
            Design r;
            const double c = kCoupling;
            const double A[4][4] = {{-(1 + c), c, 0, 0}, {1, -(1 + c), c, 0}, {0, 1, -(1 + c), c}, {0, 0, 1, -1}};
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.A[i][j] = A[i][j];
            auto H = [&](double w) { return transfer(r.A, std::complex<double>(0, w)); };
            // Find where the unwrapped phase first reaches −180°: scan, then bisect
            // locally (just below −180° the imaginary part is negative).
            double prevW = 1e-3, lo = 1e-3, hi = 50.0;
            double prevPhase = std::arg(H(prevW)), unwrap = 0.0;
            for (int i = 1; i <= 4000; ++i) {
                const double w = 1e-3 * std::pow(5e4, i / 4000.0);
                double ph = std::arg(H(w));
                if (ph - prevPhase > kPi) unwrap -= 2 * kPi;
                if (ph + unwrap <= -kPi) { lo = prevW; hi = w; break; }
                prevPhase = ph; prevW = w;
            }
            for (int it = 0; it < 80; ++it) { const double m = 0.5 * (lo + hi); (H(m).imag() < 0 ? lo : hi) = m; }
            r.w180 = 0.5 * (lo + hi);
            r.kCrit = 1.0 / std::abs(H(r.w180));
            return r;
        }();
        return d;
    }

    void reset() noexcept { y_ = {0, 0, 0, 0}; xPrev_ = 0.f; }

    // fc in Hz at sample rate fs; resonance [0,1]; drive linear ≥ 1.
    void setCoefficients(float fc, double fs, float resonance, float drive) noexcept {
        const Design& d = design();
        // Calibrated so the resonant peak (and self-oscillation) lands on fc,
        // the same convention as Filter B.
        const double g = std::tan(kPi * std::min(static_cast<double>(fc), 0.45 * fs) / fs) / d.w180;
        const double k = 1.12 * d.kCrit * clampf(resonance, 0.f, 1.f);
        double Ap[4][4];
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) Ap[i][j] = d.A[i][j];
        Ap[0][3] -= k;                                   // u = x − k·y4 enters stage 1
        double M[4][4], N[4][4];
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
            const double I = i == j ? 1.0 : 0.0;
            M[i][j] = I - g * Ap[i][j];
            N[i][j] = I + g * Ap[i][j];
        }
        invert4(M, Minv_);
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
            double s = 0; for (int m = 0; m < 4; ++m) s += Minv_[i][m] * N[m][j];
            P_[i][j] = static_cast<float>(s);
        }
        for (int i = 0; i < 4; ++i) q_[i] = static_cast<float>(Minv_[i][0] * g);   // input enters stage 1
        drive_ = drive < 1.f ? 1.f : drive;
        // Unity passband: DC gain of the loop is 1/(1+k).
        comp_ = static_cast<float>(1.0 + k) / std::sqrt(drive_);
    }

    float process(float xIn) noexcept {
        const float x = fastTanh(xIn * drive_);
        std::array<float, 4> n{};
        const float xs = x + xPrev_;
        for (int i = 0; i < 4; ++i) {
            float s = q_[i] * xs;
            for (int j = 0; j < 4; ++j) s += P_[i][j] * y_[static_cast<size_t>(j)];
            n[static_cast<size_t>(i)] = s;
        }
        for (auto& v : n) v = 2.f * fastTanh(0.5f * v);   // diode-like state limiting (bounded self-oscillation)
        y_ = n;
        xPrev_ = x;
        return y_[3] * comp_;
    }

private:
    static std::complex<double> transfer(const double (&A)[4][4], std::complex<double> s) {
        // H(s) = e4ᵀ (sI − A)⁻¹ e1
        std::complex<double> m[4][5];
        for (int i = 0; i < 4; ++i) { for (int j = 0; j < 4; ++j) m[i][j] = (i == j ? s : 0.0) - A[i][j]; m[i][4] = i == 0 ? 1.0 : 0.0; }
        for (int c = 0; c < 4; ++c) {
            int p = c; for (int r = c + 1; r < 4; ++r) if (std::abs(m[r][c]) > std::abs(m[p][c])) p = r;
            for (int j = 0; j < 5; ++j) std::swap(m[c][j], m[p][j]);
            for (int r = 0; r < 4; ++r) if (r != c) { const auto f = m[r][c] / m[c][c]; for (int j = c; j < 5; ++j) m[r][j] -= f * m[c][j]; }
        }
        return m[3][4] / m[3][3];
    }
    static void invert4(const double (&a)[4][4], double (&out)[4][4]) noexcept {
        double m[4][8];
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 8; ++j) m[i][j] = j < 4 ? a[i][j] : (j - 4 == i ? 1.0 : 0.0);
        for (int c = 0; c < 4; ++c) {
            int p = c; for (int r = c + 1; r < 4; ++r) if (std::fabs(m[r][c]) > std::fabs(m[p][c])) p = r;
            for (int j = 0; j < 8; ++j) std::swap(m[c][j], m[p][j]);
            const double piv = m[c][c];
            for (int j = 0; j < 8; ++j) m[c][j] /= piv;
            for (int r = 0; r < 4; ++r) if (r != c) { const double f = m[r][c]; for (int j = 0; j < 8; ++j) m[r][j] -= f * m[c][j]; }
        }
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out[i][j] = m[i][j + 4];
    }

    double Minv_[4][4]{};
    float P_[4][4]{};
    float q_[4]{};
    std::array<float, 4> y_{0, 0, 0, 0};
    float xPrev_ = 0.f, drive_ = 1.f, comp_ = 1.f;
};

class TransistorLadder {
public:
    static constexpr PortInfo kPorts[] = {
        {"in", PortKind::Audio, PortDir::In}, {"cutoff", PortKind::CV, PortDir::In}, {"out", PortKind::Audio, PortDir::Out},
    };

    void reset() noexcept { y_ = {0, 0, 0, 0}; w_ = {0, 0, 0}; delayed_ = 0.f; }

    void setCoefficients(float fc, double fs, float resonance, float drive) noexcept {
        // Internal rate is 2 × fs. Tuning polynomial compensates the explicit
        // integrator's cutoff error (Välimäki & Huovilainen, 2006).
        const double fsInt = 2.0 * fs;
        const double fcn = std::min(static_cast<double>(fc), 0.45 * fs) / fsInt;
        const double tune = 1.0 + fcn * (-0.0372 + fcn * (1.84 + fcn * (-0.9007 + fcn * 0.6175)));   // ≈ 1 at low fc
        g_ = static_cast<float>(1.0 - std::exp(-2.0 * kPi * fcn * tune));
        res_ = 4.6f * clampf(resonance, 0.f, 1.f);   // threshold ≈ 4 (res ≈ 0.87); tanh stages bound the oscillation
        drive_ = drive < 1.f ? 1.f : drive;
        comp_ = (1.f + 0.4f * res_) / std::sqrt(drive_);
    }

    float process(float x) noexcept {
        float out = 0.f;
        for (int os = 0; os < 2; ++os) {
            const float in = x * drive_ - res_ * delayed_;
            const float t0 = fastTanh(in * kInv2Vt);
            y_[0] += kTwoVt * g_ * (t0 - w_[0]);
            w_[0] = fastTanh(y_[0] * kInv2Vt);
            y_[1] += kTwoVt * g_ * (w_[0] - w_[1]);
            w_[1] = fastTanh(y_[1] * kInv2Vt);
            y_[2] += kTwoVt * g_ * (w_[1] - w_[2]);
            w_[2] = fastTanh(y_[2] * kInv2Vt);
            y_[3] += kTwoVt * g_ * (w_[2] - fastTanh(y_[3] * kInv2Vt));
            delayed_ = 0.5f * (y_[3] + last_);   // half-sample feedback delay compensation
            last_ = y_[3];
            out = delayed_;
        }
        return out * comp_;
    }

private:
    static constexpr float kTwoVt = 1.2f;            // thermal-voltage scaling (headroom of each stage)
    static constexpr float kInv2Vt = 1.f / 1.2f;
    std::array<float, 4> y_{0, 0, 0, 0};
    std::array<float, 3> w_{0, 0, 0};
    float g_ = 0.1f, res_ = 0.f, drive_ = 1.f, comp_ = 1.f, delayed_ = 0.f, last_ = 0.f;
};

} // namespace vektur
