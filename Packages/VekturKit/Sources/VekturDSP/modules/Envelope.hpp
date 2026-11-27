// Envelope.hpp — ADSR envelope generator with vintage response curves.
//
// Each stage moves from its start level to its target along a normalised
// progress x ∈ [0,1] at rate 1/(time·sr), shaped by the selected curve:
//   Linear       straight segments
//   Exponential  fast start, slow finish (RC discharge shape)
//   Logarithmic  slow start, fast finish
//   Analogue     RC charge toward an overshoot target (≈ 1.3 × peak) for the
//                attack — the classic slightly-convex attack that "arrives"
//                with energy — and RC discharge for decay/release.
// Starting every stage from the current level makes retriggering and release
// during attack click-free. Times are 0.5 ms … 20 s.
#pragma once

#include "DSPCommon.hpp"

namespace vektur {

class Envelope {
public:
    enum class Curve : uint8_t { Linear = 0, Exponential, Logarithmic, Analogue };
    enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };
    static constexpr PortInfo kPorts[] = {
        {"gate", PortKind::Gate, PortDir::In}, {"trigger", PortKind::Trigger, PortDir::In},
        {"out", PortKind::CV, PortDir::Out}, {"inverted", PortKind::CV, PortDir::Out},
    };

    struct Params {
        float attackMs = 5.f, decayMs = 300.f, sustain = 1.f, releaseMs = 300.f;
        Curve curve = Curve::Analogue;
        bool loop = false;           // A→D→A… while the gate is held
        bool retriggerFromZero = false;
        float timeScale = 1.f;       // component tolerance (≈ 0.95 … 1.05)
    };

    void prepare(double sampleRate) noexcept { sr_ = sampleRate; reset(); }
    void reset() noexcept { stage_ = Stage::Idle; level_ = 0.f; x_ = 0.f; start_ = 0.f; }

    void setParams(const Params& p) noexcept {
        p_ = p;
        p_.sustain = clampf(p.sustain, 0.f, 1.f);
        aRate_ = rate(p.attackMs); dRate_ = rate(p.decayMs); rRate_ = rate(p.releaseMs);
        // Recursive (one-multiply) forms of the RC curves — no exp() per sample.
        // Exponential/Analogue fall: 1 - e^(-4.6 x) reaches 99 % at x = 1, then snaps.
        rcFall_ = p_.curve == Curve::Exponential || p_.curve == Curve::Analogue;
        dCoef_ = 1.f - std::exp(-4.6f * dRate_);
        rCoef_ = 1.f - std::exp(-4.6f * rRate_);
        rcAttack_ = p_.curve == Curve::Analogue || p_.curve == Curve::Exponential;
        aCoef_ = 1.f - std::exp(-(p_.curve == Curve::Analogue ? 1.4663f : 4.6f) * aRate_);
        susCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sr_)));
        if (stage_ == Stage::Attack) aTarget_ = attackTarget();
    }

    void gateOn() noexcept {
        if (p_.retriggerFromZero) level_ = 0.f;
        begin(Stage::Attack);
    }
    void gateOff() noexcept { if (stage_ != Stage::Idle) begin(Stage::Release); }

    float next() noexcept {
        switch (stage_) {
            case Stage::Idle: return 0.f;
            case Stage::Attack:
                x_ += aRate_;
                if (x_ >= 1.f) { level_ = 1.f; begin(Stage::Decay); break; }
                if (rcAttack_) level_ += (aTarget_ - level_) * aCoef_;     // RC toward overshoot target
                else level_ = start_ + (1.f - start_) * shapeRise(x_);
                break;
            case Stage::Decay:
                x_ += dRate_;
                if (x_ >= 1.f) {
                    level_ = p_.sustain;
                    if (p_.loop) begin(Stage::Attack); else stage_ = Stage::Sustain;
                    break;
                }
                if (rcFall_) level_ += (p_.sustain - level_) * dCoef_;
                else level_ = start_ + (p_.sustain - start_) * shapeFall(x_);
                break;
            case Stage::Sustain:
                level_ += (p_.sustain - level_) * susCoef_;                 // follow sustain changes (≈ 5 ms)
                break;
            case Stage::Release:
                x_ += rRate_;
                if (x_ >= 1.f) { level_ = 0.f; stage_ = Stage::Idle; break; }
                if (rcFall_) level_ -= level_ * rCoef_;
                else level_ = start_ * (1.f - shapeFall(x_));
                break;
        }
        return level_;
    }

    Stage stage() const noexcept { return stage_; }
    bool isIdle() const noexcept { return stage_ == Stage::Idle; }
    float level() const noexcept { return level_; }

private:
    void begin(Stage s) noexcept {
        stage_ = s; start_ = level_; x_ = 0.f;
        if (s == Stage::Attack) aTarget_ = attackTarget();
    }
    // Analogue: charge toward start + 1.3·(1 − start), which crosses 1.0 exactly at x = 1.
    // Exponential: toward start + (1 − start)/0.99, crossing 1.0 at x = 1.
    float attackTarget() const noexcept {
        return p_.curve == Curve::Analogue ? start_ + 1.3f * (1.f - start_) : start_ + (1.f - start_) * kExpNorm;
    }

    float rate(float ms) const noexcept {
        const double t = static_cast<double>(clampf(ms, 0.5f, 20000.f) * p_.timeScale) * 0.001 * sr_;
        return static_cast<float>(1.0 / (t < 1.0 ? 1.0 : t));
    }

    // Rising shape: 0→1.
    float shapeRise(float x) const noexcept {
        switch (p_.curve) {
            case Curve::Linear: return x;
            case Curve::Exponential: return (1.f - std::exp(-4.6f * x)) * kExpNorm;
            case Curve::Logarithmic: return x * x * x;
            case Curve::Analogue: return 1.3f * (1.f - std::exp(-1.4663f * x));   // reaches 1.0 at x = 1
        }
        return x;
    }
    // Falling progress: 0→1 (applied as start→target).
    float shapeFall(float x) const noexcept {
        switch (p_.curve) {
            case Curve::Linear: return x;
            case Curve::Logarithmic: { const float y = 1.f - x; return 1.f - y * y * y; }
            case Curve::Exponential:
            case Curve::Analogue: return (1.f - std::exp(-4.6f * x)) * kExpNorm;
        }
        return x;
    }

    static constexpr float kExpNorm = 1.f / (1.f - 0.010051836f);   // 1/(1-e^-4.6)

    Params p_{};
    double sr_ = 48000.0;
    float aRate_ = 0.f, dRate_ = 0.f, rRate_ = 0.f;
    float aCoef_ = 1.f, dCoef_ = 1.f, rCoef_ = 1.f, susCoef_ = 0.f, aTarget_ = 1.3f;
    bool rcAttack_ = true, rcFall_ = true;
    float level_ = 0.f, start_ = 0.f, x_ = 0.f;
    Stage stage_ = Stage::Idle;
};

} // namespace vektur
