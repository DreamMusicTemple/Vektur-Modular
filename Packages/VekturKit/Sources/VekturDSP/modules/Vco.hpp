// Vco.hpp — band-limited voltage-controlled oscillator.
//
// Anti-aliasing: every discontinuity (step) and slope change (corner) is
// located at its exact sub-sample time and corrected with 2-point polyBLEP /
// polyBLAMP residuals. This handles waveform wraps, pulse edges, hard/soft-sync
// resets and the sub-oscillator uniformly. The price is one sample of latency
// (corrections are written into the previous output sample).
//
// Analogue behaviour (drift, tolerance, exponential-converter tracking droop)
// is applied by the caller to the increment so the oscillator itself stays a
// pure, testable phase engine.
#pragma once

#include "DSPCommon.hpp"

namespace vektur {

enum class Wave : uint8_t { Sine = 0, Triangle, Saw, ReverseSaw, Square, Pulse };

class Vco {
public:
    static constexpr double kSoftWindow = 0.25;
    static constexpr PortInfo kPorts[] = {
        {"pitch", PortKind::CV, PortDir::In}, {"fm", PortKind::Audio, PortDir::In},
        {"pwm", PortKind::CV, PortDir::In},   {"sync", PortKind::Trigger, PortDir::In},
        {"out", PortKind::Audio, PortDir::Out}, {"sub", PortKind::Audio, PortDir::Out},
    };

    // Reported to a sync slave: did this oscillator wrap during the last sample,
    // and how long ago (in samples, [0,1)) relative to the current sample instant.
    struct Wrap { bool happened = false; float since = 0.f; };

    void reset(float startPhase01 = 0.f) noexcept {
        phase_ = static_cast<double>(clampf(startPhase01, 0.f, 0.999999f));
        prevNaive_ = 0.f; corrPrev_ = corrCur_ = 0.f; primed_ = false;
        sub1_ = sub2_ = 1.f; subPrev_ = 1.f; subCorrPrev_ = subCorrCur_ = 0.f;
    }

    void setPhase(float p01) noexcept { phase_ = static_cast<double>(clampf(p01, 0.f, 0.999999f)); primed_ = false; }
    double phase() const noexcept { return phase_; }

    // inc: cycles per sample. sync: master wrap info (nullptr = none).
    // softSync: reset only when this oscillator is within kSoftWindow of its own wrap.
    // subOctaves: 1 or 2 (sub-oscillator square one or two octaves down).
    float process(double inc, Wave w, float pw, const Wrap* sync, bool softSync, Wrap& wrapOut,
                  float* subOut = nullptr, int subOctaves = 1) noexcept {
        inc = inc < 0.0 ? 0.0 : (inc > 0.49 ? 0.49 : inc);
        pw = clampf(pw, 0.02f, 0.98f);
        wrapOut = {};
        wave_ = w; pw_ = pw;

        if (!primed_) { prevNaive_ = naive(phase_); primed_ = true; }

        if (sync && sync->happened && inc > 0.0) {
            const double d = static_cast<double>(clampf(sync->since, 0.f, 0.999999f));
            advance(inc, 1.0 - d, d, wrapOut, subOut != nullptr, subOctaves);
            // Soft sync = window sync: only snap when the slave is already near its
            // own cycle boundary, so near-harmonic ratios lock and others pass.
            if (!softSync || phase_ < kSoftWindow || phase_ > 1.0 - kSoftWindow) {
                const float before = naive(phase_);
                const float slopeBefore = slope(phase_, inc);
                phase_ = 0.0;
                addStep(naive(0.0) - before, d);
                addRamp(slope(0.0, inc) - slopeBefore, d);
            }
            advance(inc, d, 0.0, wrapOut, subOut != nullptr, subOctaves);
        } else {
            advance(inc, 1.0, 0.0, wrapOut, subOut != nullptr, subOctaves);
        }

        const float cur = naive(phase_);
        const float out = prevNaive_ + corrPrev_;
        prevNaive_ = cur; corrPrev_ = corrCur_; corrCur_ = 0.f;

        if (subOut) {
            const float subNow = subOctaves >= 2 ? sub2_ : sub1_;
            *subOut = subPrev_ + subCorrPrev_;
            subPrev_ = subNow; subCorrPrev_ = subCorrCur_; subCorrCur_ = 0.f;
        }
        return out;
    }

    // Naive (aliased) waveform value — exposed for tests that compare against it.
    static float naiveValue(double p, Wave w, float pw) noexcept {
        switch (w) {
            case Wave::Sine:       return static_cast<float>(std::sin(2.0 * kPi * p));
            case Wave::Triangle:   return p < 0.5 ? static_cast<float>(-1.0 + 4.0 * p) : static_cast<float>(3.0 - 4.0 * p);
            case Wave::Saw:        return static_cast<float>(2.0 * p - 1.0);
            case Wave::ReverseSaw: return static_cast<float>(1.0 - 2.0 * p);
            case Wave::Square:     return p < 0.5 ? 1.f : -1.f;
            case Wave::Pulse:      return p < static_cast<double>(pw) ? 1.f : -1.f;
        }
        return 0.f;
    }

private:
    float naive(double p) const noexcept { return naiveValue(p, wave_, pw_); }

    // Slope in amplitude per sample (only needed where BLAMP applies).
    float slope(double p, double inc) const noexcept {
        if (wave_ == Wave::Triangle) return static_cast<float>((p < 0.5 ? 4.0 : -4.0) * inc);
        return 0.f;
    }

    // d = time from the discontinuity to the current sample instant, in [0,1).
    void addStep(float h, double d) noexcept {
        if (h == 0.f) return;
        const float df = static_cast<float>(d);
        corrPrev_ += h * 0.5f * df * df;
        corrCur_  -= h * 0.5f * (1.f - df) * (1.f - df);
    }
    void addRamp(float m, double d) noexcept {
        if (m == 0.f) return;
        const float df = static_cast<float>(d);
        const float e = 1.f - df;
        corrPrev_ += m * df * df * df / 6.f;
        corrCur_  += m * e * e * e / 6.f;
    }
    void addSubStep(float h, double d) noexcept {
        const float df = static_cast<float>(d);
        subCorrPrev_ += h * 0.5f * df * df;
        subCorrCur_  -= h * 0.5f * (1.f - df) * (1.f - df);
    }

    // Advance by `len` samples; the segment ends `offsetToNow` samples before
    // the current sample instant.
    void advance(double inc, double len, double offsetToNow, Wrap& wrapOut, bool doSub, int subOct) noexcept {
        if (len <= 0.0) return;
        const double pa = phase_;
        const double pb = pa + inc * len;
        auto at = [&](double e) { return clampd((pb - e) / inc + offsetToNow); };

        switch (wave_) {
            case Wave::Saw:        if (pb >= 1.0) addStep(-2.f, at(1.0)); break;
            case Wave::ReverseSaw: if (pb >= 1.0) addStep(2.f, at(1.0)); break;
            case Wave::Square:
            case Wave::Pulse: {
                const double wdt = wave_ == Wave::Square ? 0.5 : static_cast<double>(pw_);
                if (pa < wdt && pb >= wdt) addStep(-2.f, at(wdt));
                if (pb >= 1.0) addStep(2.f, at(1.0));
                if (pb >= 1.0 + wdt) addStep(-2.f, at(1.0 + wdt));
                break;
            }
            case Wave::Triangle: {
                const float m = static_cast<float>(8.0 * inc);
                if (pa < 0.5 && pb >= 0.5) addRamp(-m, at(0.5));
                if (pb >= 1.0) addRamp(m, at(1.0));
                if (pb >= 1.5) addRamp(-m, at(1.5));
                break;
            }
            case Wave::Sine: break;
        }

        if (pb >= 1.0) {
            const double since = at(1.0);
            wrapOut.happened = true;
            wrapOut.since = static_cast<float>(since);
            if (doSub) {
                const float old1 = sub1_;
                sub1_ = -sub1_;
                if (subOct >= 2) {
                    if (old1 > 0.f) { const float old2 = sub2_; sub2_ = -sub2_; addSubStep(sub2_ - old2, since); }
                } else {
                    addSubStep(sub1_ - old1, since);
                }
            }
            phase_ = pb - 1.0;
        } else {
            phase_ = pb;
        }
    }

    static double clampd(double d) noexcept { return d < 0.0 ? 0.0 : (d > 0.999999 ? 0.999999 : d); }

    double phase_ = 0.0;
    float prevNaive_ = 0.f, corrPrev_ = 0.f, corrCur_ = 0.f;
    float sub1_ = 1.f, sub2_ = 1.f, subPrev_ = 1.f, subCorrPrev_ = 0.f, subCorrCur_ = 0.f;
    Wave wave_ = Wave::Saw;
    float pw_ = 0.5f;
    bool primed_ = false;
};

} // namespace vektur
