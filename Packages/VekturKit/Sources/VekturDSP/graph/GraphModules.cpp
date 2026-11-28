// GraphModules.cpp — module implementations. Parameter indices refer to the
// module's block in tools/params.py.
#include "GraphModules.hpp"
#include "../modules/LadderVariants.hpp"

namespace vektur {

namespace {

inline float semisToHz(float semis) noexcept { return 440.f * std::exp2((semis - 69.f) / 12.f); }

// Deterministic unit tolerance in [-1, 1) for (voice, salt).
inline float tolerance(uint32_t voice, uint32_t salt) noexcept {
    Rng r(hash32(0xA11CE5u + voice * 7919u + salt * 104729u));
    return r.bipolar();
}

// ===========================================================================
// Keyboard: pitch CV (with glide, bend, tune), gate, trigger, velocity.
// Pitch CV: 0 = middle C (note 60), 1.0 per 5 octaves (1 V/oct at 1.0 = 5 V).
// ===========================================================================
class KeyboardModule final : public Module {
public:
    void prepare(double sr, uint32_t v, uint32_t pm) override { Module::prepare(sr, v, pm); glide_.prepare(sr); }
    void noteStart(const VoiceContext& vc, const GlobalContext&, bool) override {
        glide_.reset(vc.glideFrom); lastTarget_ = vc.note;
        interval_ = std::fabs(static_cast<float>(vc.note) - vc.glideFrom);
    }
    void process(const Ports& p, uint32_t n, VoiceContext& v, const GlobalContext& g) override {
        // Constant-time glide, or constant-rate (glide time per octave).
        if (v.note != lastTarget_) {
            interval_ = std::fabs(static_cast<float>(v.note) - glide_.value());
            lastTarget_ = v.note;
        }
        float ms = v.glide ? g.glideMs : 0.f;
        if (!g.glideConstantTime) ms *= std::max(interval_, 0.f) / 12.f;
        glide_.set(ms, ms, Slew::Mode::Exponential);
        const float offset = g.pitchOffsetSemis + v.pitchModSemis + v.detuneSemis + v.noteBendSemis - 60.f;
        for (uint32_t s = 0; s < n; s += kSubBlock) {
            const uint32_t m = std::min(kSubBlock, n - s);
            const float semis = glide_.advance(static_cast<float>(v.note), m);
            const float cv = (semis + offset) / 60.f;
            for (uint32_t i = 0; i < m; ++i) p.out[0][s + i] = cv;
        }
        const float gate = v.gate ? 1.f : 0.f;
        for (uint32_t i = 0; i < n; ++i) { p.out[1][i] = gate; p.out[2][i] = 0.f; p.out[3][i] = v.velocity; }
        if (v.triggerPending) { p.out[2][0] = 1.f; v.triggerPending = false; }
    }
private:
    Slew glide_;
    int lastTarget_ = -1;
    float interval_ = 0.f;
};

// ===========================================================================
// VCO (instances 1–9). Inputs: pitch, pitchMod1 (per-sample, exp), pitchMod2
// (control rate), fm, pwm, sync. Outputs: out, sub, sync (1+fraction on wrap).
// ===========================================================================
class VcoModule final : public Module {
public:
    explicit VcoModule(uint32_t n) : n_(n) {}
    void prepare(double sr, uint32_t v, uint32_t pm) override {
        Module::prepare(sr, v, pm);
        tol_ = n_ < 3 ? tolerance(v, n_) : tolerance(v, 100 + n_);
        rng_ = Rng(hash32(0xD21F7u + v * 104729u + n_ * 7u));
        osc_.reset(0.f);
    }
    void reset() override { osc_.reset(0.f); drift_ = 0.f; lastPm2_ = 0.f; }
    void noteStart(const VoiceContext&, const GlobalContext& g, bool) override {
        if (B(g, 7)) osc_.setPhase(P(g, 8) / 360.f);
    }
    void process(const Ports& p, uint32_t n, VoiceContext& v, const GlobalContext& g) override {
        // Parameter indices that differ per instance (see tools/params.py).
        static constexpr uint32_t fmModeIdx[9] = {12, 11, 11, 10, 10, 10, 10, 10, 10};
        static constexpr uint32_t syncIdx[9] = {13, 9, 12, 11, 11, 11, 11, 11, 11};
        const Wave wave = static_cast<Wave>(std::clamp(I(g, 0), 0, 5));
        const float base = static_cast<float>((I(g, 1) - 3) * 12 + I(g, 2)) + P(g, 3) / 100.f;
        const float pw = P(g, 4) / 100.f;
        const bool lowFreq = B(g, 6);
        const bool fmLinear = I(g, fmModeIdx[n_]) == 1;
        const int syncMode = I(g, syncIdx[n_]);
        const int subOct = n_ == 0 ? I(g, 10) + 1 : 1;

        const float inst = g.instability;
        if (inst > 0.f) {   // Ornstein–Uhlenbeck drift, τ = 2 s
            const float dt = static_cast<float>(n / sr_);
            drift_ += -drift_ * dt / 2.f + std::sqrt(dt) * 1.732f * rng_.bipolar();
        }
        const float driftCents = (g.authentic ? 3.f : 0.4f) * inst * drift_
                               + (g.authentic ? 4.f : 0.3f) * inst * v.thermalCents
                               + (g.authentic ? 4.f : 0.5f) * inst * tol_;
        const float staticSemis = base + driftCents / 100.f;

        if (g.hqOscillators != hqOn_) { hqOn_ = g.hqOscillators; hq_.setFactor(hqOn_ ? 2 : 1); hq_.reset(); }
        for (uint32_t s = 0; s < n; s += kSubBlock) {
            const uint32_t m = std::min(kSubBlock, n - s);
            const float keySemis = p.in[0] ? p.in[0][s] * 60.f + 60.f : 60.f;
            const float pm2 = p.in[2] ? (s == 0 ? lastPm2_ : p.in[2][s - 1]) * 60.f : 0.f;   // value before this sub-block
            const double inc = pitchToInc(keySemis + staticSemis + pm2, lowFreq, g, inst);
            for (uint32_t i = s; i < s + m; ++i) {
                const double pm = p.in[1] ? static_cast<double>(std::exp2(p.in[1][i] * 5.f)) : 1.0;
                double ic = inc * pm;
                if (p.in[3]) {
                    const float f = 4.f * p.in[3][i];
                    ic *= fmLinear ? static_cast<double>(1.f + f) : static_cast<double>(std::exp2(f));
                }
                Vco::Wrap syncIn{}, wrap{};
                const Vco::Wrap* sp = nullptr;
                float since = 0.f;
                if (p.in[5] && syncMode != 0 && trig_.step(p.in[5][i], since)) { syncIn = {true, since}; sp = &syncIn; }
                const float pwNow = p.in[4] ? clampf(pw + p.in[4][i] * 0.5f, 0.02f, 0.98f) : pw;
                float sub = 0.f;
                if (g.hqOscillators) {
                    // HQ: two half-rate oscillator steps per sample, polyBLEP at the
                    // doubled rate, then the halfband decimator. A sync event lands in
                    // the half-sample it falls in; wraps are reported at base rate.
                    float subA = 0.f, subB = 0.f;
                    Vco::Wrap wA{}, wB{}, sA{}, sB{};
                    const Vco::Wrap *pA = nullptr, *pB = nullptr;
                    if (sp) { if (sp->since >= 0.5f) { sA = {true, (sp->since - 0.5f) * 2.f}; pA = &sA; } else { sB = {true, sp->since * 2.f}; pB = &sB; } }
                    int k = 0;
                    p.out[0][i] = hq_.process(0.f, [&](float) noexcept {
                        const bool first = k++ == 0;
                        return first ? osc_.process(ic * 0.5, wave, pwNow, pA, syncMode == 2, wA, &subA, subOct)
                                     : osc_.process(ic * 0.5, wave, pwNow, pB, syncMode == 2, wB, &subB, subOct);
                    });
                    sub = 0.5f * (subA + subB);
                    if (wA.happened) wrap = {true, 0.5f + wA.since * 0.5f};
                    else if (wB.happened) wrap = {true, wB.since * 0.5f};
                } else {
                    p.out[0][i] = osc_.process(ic, wave, pwNow, sp, syncMode == 2, wrap, &sub, subOct);
                }
                p.out[1][i] = sub;
                p.out[2][i] = wrap.happened ? 1.f + wrap.since : 0.f;
            }
        }
        if (p.in[2]) lastPm2_ = p.in[2][n - 1];
        if (p.in[5] && syncMode == 0) trig_.prev = p.in[5][n - 1];
    }

private:
    double pitchToInc(float semis, bool lowFreq, const GlobalContext& g, float inst) const noexcept {
        float hz = semisToHz(semis);
        if (lowFreq) hz *= (1.f / 256.f);
        else if (g.authentic && inst > 0.f) {
            const float r = hz * 1.0e-4f;                       // exponential-converter HF droop
            hz *= std::exp2(-inst * 12.f * r * r / 1200.f);
        }
        return static_cast<double>(hz) / sr_;
    }
    uint32_t n_;
    Vco osc_;
    Oversampler hq_;
    bool hqOn_ = false;
    Rng rng_;
    TriggerIn trig_;
    float tol_ = 0.f, drift_ = 0.f, lastPm2_ = 0.f;
};

// ===========================================================================
class NoiseModule final : public Module {
public:
    void prepare(double sr, uint32_t v, uint32_t pm) override { Module::prepare(sr, v, pm); noise_.seed(hash32(0x5EEDu + v * 31337u)); noise_.reset(); }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        const auto type = I(g, 0) == 1 ? Noise::Type::Pink : Noise::Type::White;
        const float colour = P(g, 1) / 100.f;
        for (uint32_t i = 0; i < n; ++i) p.out[0][i] = noise_.process(type, colour);
    }
private:
    Noise noise_;
};

class RingModule final : public Module {
public:
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        const float leak = P(g, 1) / 100.f;
        for (uint32_t i = 0; i < n; ++i) p.out[0][i] = RingMod::process(at(p.in[0], i), at(p.in[1], i), leak);
    }
};

// Voice mixer: inputs arrive attenuated by the level knobs. Output carries
// −6 dB of headroom into the filters (restored at the voice output) plus the
// Authentic noise floor.
class VoiceMixerModule final : public Module {
public:
    void prepare(double sr, uint32_t v, uint32_t pm) override { Module::prepare(sr, v, pm); rng_ = Rng(hash32(0xB15Eu + v * 2654435761u)); }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        const float hiss = g.authentic ? 3.0e-4f * g.instability : 0.f;
        for (uint32_t i = 0; i < n; ++i) {
            float s = 0.f;
            for (uint32_t k = 0; k < 6; ++k) if (p.in[k]) s += p.in[k][i];
            if (hiss > 0.f) s += rng_.bipolar() * hiss;
            p.out[0][i] = s * 0.5f;
        }
    }
private:
    Rng rng_;
};

class SumModule final : public Module {
public:
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext&) override {
        for (uint32_t i = 0; i < n; ++i) p.out[0][i] = at(p.in[0], i) + at(p.in[1], i);
    }
};

// ===========================================================================
// Filters A (SVF) and B (ladder). Inputs: in, env, key, cv, fm (audio rate).
// Cutoff in octaves = 5 × Σ(attenuated CV) + tolerance.
// ===========================================================================
template <bool Ladder>
class FilterModule final : public Module {
public:
    void reset() override { svf_.reset(); ladder_.reset(); os_.reset(); multi_ = {}; lastEnv_ = lastFm_ = 0.f; }
    void noteStart(const VoiceContext&, const GlobalContext&, bool fromIdle) override { if (fromIdle) reset(); }

    void process(const Ports& p, uint32_t n, VoiceContext& v, const GlobalContext& g) override {
        os_.setFactor(g.oversampling);
        const double fsInt = sr_ * os_.factor();
        const float nyq = static_cast<float>(0.45 * fsInt);
        const float tolCut = (g.authentic ? 0.09f : 0.015f) * g.instability * v.tolCutoff;
        const float cutoff = P(g, Ladder ? 0 : 1);
        const float res = P(g, Ladder ? 1 : 2) / 100.f;
        const float drive = dbToGain(P(g, Ladder ? 2 : 3));
        const auto mode = static_cast<SvfFilter::Mode>(Ladder ? 0 : std::clamp(I(g, 0), 0, 3));
        const float bassComp = g.authentic ? 0.25f : 0.6f;
        const bool audioRate = p.in[4] != nullptr;
        const bool multiOut = !Ladder && (p.outUsed[1] || p.outUsed[2] || p.outUsed[3] || p.outUsed[4]);

        auto setCoefs = [&](float env, float key, float cv, float fm) noexcept {
            const float oct = 5.f * (env + key + cv + fm) + tolCut;
            const float fc = clampf(cutoff * std::exp2(oct), 8.f, nyq);
            const float gg = std::tan(kPiF * fc / static_cast<float>(fsInt));
            if constexpr (Ladder) ladder_.setCoefficients(gg, res, drive, bassComp);
            else svf_.setCoefficients(gg, res, drive);
        };

        for (uint32_t s = 0; s < n; s += kSubBlock) {
            const uint32_t m = std::min(kSubBlock, n - s);
            if (!audioRate)   // control rate: values just before this sub-block
                setCoefs(s == 0 ? lastEnv_ : at(p.in[1], s - 1), at(p.in[2], s), at(p.in[3], s), 0.f);
            for (uint32_t i = s; i < s + m; ++i) {
                if (audioRate) setCoefs(at(p.in[1], i), at(p.in[2], i), at(p.in[3], i), p.in[4][i]);
                const float x = at(p.in[0], i);
                if constexpr (Ladder) {
                    p.out[0][i] = os_.process(x, [this](float u) noexcept { return ladder_.process(u); });
                } else if (!multiOut) {
                    p.out[0][i] = os_.process(x, [this, mode](float u) noexcept { return svf_.process(u, mode); });
                } else {
                    float y[4];
                    processMulti(x, y);
                    p.out[1][i] = y[0]; p.out[2][i] = y[1]; p.out[3][i] = y[2]; p.out[4][i] = y[3];
                    p.out[0][i] = y[static_cast<int>(mode)];
                }
            }
        }
        lastEnv_ = at(p.in[1], n - 1);
    }

private:
    // All four SVF outputs through one shared upsampler and four decimators.
    void processMulti(float x, float y[4]) noexcept {
        auto run = [this](float u, float o[4]) noexcept { const auto r = svf_.processAll(u); o[0] = r.lp; o[1] = r.bp; o[2] = r.hp; o[3] = r.notch; };
        const int f = os_.factor();
        if (f == 1) { run(x, y); return; }
        float u0, u1, a[4], b[4];
        multi_.upA.upsample(x, u0, u1);
        if (f == 2) {
            run(u0, a); run(u1, b);
            for (int k = 0; k < 4; ++k) y[k] = multi_.downA[k].downsample(a[k], b[k]);
            return;
        }
        float q[4], c[4], d[4];
        multi_.upB.upsample(u0, q[0], q[1]);
        multi_.upB.upsample(u1, q[2], q[3]);
        run(q[0], a); run(q[1], b); run(q[2], c); run(q[3], d);
        for (int k = 0; k < 4; ++k) {
            const float d0 = multi_.downB[k].downsample(a[k], b[k]);
            const float d1 = multi_.downB[k].downsample(c[k], d[k]);
            y[k] = multi_.downA[k].downsample(d0, d1);
        }
    }

    struct Multi { Halfband2x upA, upB; std::array<Halfband2x, 4> downA, downB; };
    SvfFilter svf_;
    LadderFilter ladder_;
    Oversampler os_;
    Multi multi_;
    float lastEnv_ = 0.f, lastFm_ = 0.f;
};

// ===========================================================================
class VcaModule final : public Module {
public:
    explicit VcaModule(bool primary) : primary_(primary) {}
    void process(const Ports& p, uint32_t n, VoiceContext& v, const GlobalContext& g) override {
        const auto resp = I(g, 0) == 0 ? Vca::Response::Linear : Vca::Response::Exponential;
        const float sat = P(g, 1) / 100.f;
        float velScale = 1.f, initial;
        if (primary_) { const float vel = P(g, 2) / 100.f; velScale = 1.f - vel + vel * v.velocity; initial = P(g, 3) / 100.f; }
        else initial = P(g, 2) / 100.f;
        const float bleed = g.authentic ? 1.0e-4f * g.instability : 0.f;
        for (uint32_t i = 0; i < n; ++i) {
            const float cv = at(p.in[1], i) * velScale + at(p.in[2], i) + initial;
            p.out[0][i] = Vca::process(at(p.in[0], i), cv, resp, sat, bleed);
        }
    }
private:
    bool primary_;
};

class EnvelopeModule final : public Module {
public:
    void prepare(double sr, uint32_t v, uint32_t pm) override { Module::prepare(sr, v, pm); eg_.prepare(sr); }
    void reset() override { eg_.reset(); gateHigh_ = false; trig_ = {}; }
    void noteStart(const VoiceContext&, const GlobalContext&, bool fromIdle) override { if (fromIdle) eg_.reset(); }
    void process(const Ports& p, uint32_t n, VoiceContext& v, const GlobalContext& g) override {
        Envelope::Params e;
        e.attackMs = P(g, 0); e.decayMs = P(g, 1); e.sustain = P(g, 2) / 100.f; e.releaseMs = P(g, 3);
        e.curve = static_cast<Envelope::Curve>(std::clamp(I(g, 4), 0, 3));
        e.retriggerFromZero = I(g, 6) == 1;
        e.loop = B(g, 7);
        e.timeScale = 1.f + 0.05f * v.tolTime * g.instability * (g.authentic ? 1.f : 0.15f);
        eg_.setParams(e);
        const float vel = P(g, 5) / 100.f;
        const float velScale = 1.f - vel + vel * v.velocity;
        for (uint32_t i = 0; i < n; ++i) {
            const float gv = at(p.in[0], i);
            const bool high = gv >= 0.5f;
            if (high && !gateHigh_) eg_.gateOn();
            else if (!high && gateHigh_) eg_.gateOff();
            gateHigh_ = high;
            float since;
            if (p.in[1] && trig_.step(p.in[1][i], since) && gateHigh_) eg_.gateOn();
            const float y = eg_.next() * velScale;
            p.out[0][i] = y;
            p.out[1][i] = -y;
        }
    }
private:
    Envelope eg_;
    bool gateHigh_ = false;
    TriggerIn trig_;
};

// ===========================================================================
// OTA multimode: SVF topology with both integrators saturating (OTA-style),
// giving a softer, more compressed resonance than Filter A.
// ===========================================================================
class OtaModule final : public Module {
public:
    void reset() override { ic1_ = ic2_ = 0.f; }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        const int mode = std::clamp(I(g, 0), 0, 2);
        const float cutoff = P(g, 1), res = P(g, 2) / 100.f, drive = dbToGain(P(g, 3));
        const float k = 2.f - 2.15f * res;
        const float nyq = static_cast<float>(0.45 * sr_);
        for (uint32_t s = 0; s < n; s += kSubBlock) {
            const uint32_t m = std::min(kSubBlock, n - s);
            const float oct = 5.f * (at(p.in[1], s) + at(p.in[2], s));
            const float gg = std::tan(kPiF * clampf(cutoff * std::exp2(oct), 8.f, nyq) / static_cast<float>(sr_));
            const float a1 = 1.f / (1.f + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
            for (uint32_t i = s; i < s + m; ++i) {
                const float v0 = fastTanh(at(p.in[0], i) * drive) / std::sqrt(drive) * 1.2f;
                const float v3 = v0 - ic2_;
                const float v1 = a1 * ic1_ + a2 * v3;
                const float v2 = ic2_ + a2 * ic1_ + a3 * v3;
                ic1_ = fastTanh(2.f * v1 - ic1_);
                ic2_ = fastTanh(2.f * v2 - ic2_);
                const float hp = v0 - k * v1 - v2;
                p.out[1][i] = v2; p.out[2][i] = v1; p.out[3][i] = hp;
                p.out[0][i] = mode == 0 ? v2 : (mode == 1 ? v1 : hp);
            }
        }
    }
private:
    float ic1_ = 0.f, ic2_ = 0.f;
};

// ===========================================================================
// Comb filter: fractional delay with damped, saturating feedback. Positive
// feedback → all harmonics; negative → odd harmonics (hollow, an octave down).
// ===========================================================================
class CombModule final : public Module {
public:
    void prepare(double sr, uint32_t v, uint32_t pm) override {
        Module::prepare(sr, v, pm);
        buf_.assign(static_cast<size_t>(sr / 20.0) + 8, 0.f);   // down to 20 Hz
        pos_ = 0; lp_ = 0.f;
    }
    void reset() override { std::fill(buf_.begin(), buf_.end(), 0.f); lp_ = 0.f; }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        const float fb = P(g, 1) / 100.f, damp = P(g, 2) / 100.f, mix = P(g, 3) / 100.f;
        const float dampCoef = 1.f - 0.95f * damp;
        const size_t len = buf_.size();
        const float norm = 1.f - 0.5f * std::fabs(fb);
        for (uint32_t i = 0; i < n; ++i) {
            const float hz = clampf(P(g, 0) * std::exp2(5.f * at(p.in[1], i)), 20.f, 0.4f * static_cast<float>(sr_));
            const float delay = clampf(static_cast<float>(sr_) / hz, 2.f, static_cast<float>(len - 3));
            float rp = static_cast<float>(pos_) - delay;
            if (rp < 0.f) rp += static_cast<float>(len);
            const size_t i0 = static_cast<size_t>(rp);
            const float frac = rp - static_cast<float>(i0);
            const float d = buf_[i0 % len] + (buf_[(i0 + 1) % len] - buf_[i0 % len]) * frac;
            lp_ += (d - lp_) * dampCoef;
            const float x = at(p.in[0], i);
            const float y = x + fb * lp_;
            buf_[pos_] = 4.f * fastTanh(y * 0.25f);   // bounded loop; linear at normal levels
            pos_ = (pos_ + 1) % len;
            p.out[0][i] = x * (1.f - mix) + y * mix * norm;
        }
    }
private:
    std::vector<float> buf_;
    size_t pos_ = 0;
    float lp_ = 0.f;
};

// SvfFilter maps resonance r → k = 2 − 2.1 r. This picks r for a target Q (k = 1/Q).
inline float resonanceForQ(float q) noexcept { return clampf((2.f - 1.f / q) / 2.1f, 0.f, 0.94f); }

// ===========================================================================
// Formant filter: three resonant band-passes morphing through A-E-I-O-U.
// ===========================================================================
class FormantModule final : public Module {
public:
    void reset() override { for (auto& f : bp_) f.reset(); }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        static constexpr float F[5][3] = {{600, 1040, 2250}, {400, 1620, 2400}, {250, 1750, 2600}, {400, 750, 2400}, {350, 600, 2400}};
        static constexpr float A[5][3] = {{1.f, 0.447f, 0.355f}, {1.f, 0.251f, 0.355f}, {1.f, 0.0316f, 0.158f}, {1.f, 0.282f, 0.089f}, {1.f, 0.1f, 0.025f}};
        static constexpr float BW[3] = {60.f, 70.f, 110.f};
        const float resScale = 0.5f + 1.5f * P(g, 1) / 100.f;
        const float shift = std::exp2(P(g, 2) / 12.f);
        const float mix = P(g, 3) / 100.f;
        const float nyq = static_cast<float>(0.45 * sr_);
        for (uint32_t s = 0; s < n; s += kSubBlock) {
            const uint32_t m = std::min(kSubBlock, n - s);
            const float pos = clampf(P(g, 0) / 100.f + at(p.in[1], s), 0.f, 1.f) * 4.f;
            const int a = std::min(static_cast<int>(pos), 3);
            const float t = pos - static_cast<float>(a);
            float amp[3];
            for (int k = 0; k < 3; ++k) {
                const float f = std::exp((1.f - t) * std::log(F[a][k]) + t * std::log(F[a + 1][k])) * shift;
                amp[k] = (1.f - t) * A[a][k] + t * A[a + 1][k];
                const float r = resonanceForQ(clampf(f / BW[k] * resScale, 0.5f, 60.f));
                bp_[k].setCoefficients(std::tan(kPiF * clampf(f, 20.f, nyq) / static_cast<float>(sr_)), r, 1.f);
                k_[k] = 2.f - 2.1f * r;                    // band-pass peak gain is 1/k → normalise
            }
            for (uint32_t i = s; i < s + m; ++i) {
                const float x = at(p.in[0], i);
                float y = 0.f;
                // Input scaled by k (= 1/Q): unity gain at each formant and the
                // integrators stay at signal level, below the SVF's soft clipper.
                for (int k = 0; k < 3; ++k) y += bp_[k].process(x * k_[k], SvfFilter::Mode::BandPass) * amp[k];
                p.out[0][i] = x * (1.f - mix) + y * mix * 1.5f;
            }
        }
    }
private:
    std::array<SvfFilter, 3> bp_{};
    float k_[3]{1.f, 1.f, 1.f};
};

class VcbpfModule final : public Module {
public:
    void reset() override { bp_.reset(); }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        const float q = P(g, 1), level = P(g, 4) / 100.f;
        const float r = resonanceForQ(q), k = 2.f - 2.1f * r;
        const float nyq = static_cast<float>(0.45 * sr_);
        for (uint32_t s = 0; s < n; s += kSubBlock) {
            const uint32_t m = std::min(kSubBlock, n - s);
            const float fc = clampf(P(g, 0) * std::exp2(5.f * (at(p.in[1], s) + at(p.in[2], s))), 8.f, nyq);
            bp_.setCoefficients(std::tan(kPiF * fc / static_cast<float>(sr_)), r, 1.f);
            for (uint32_t i = s; i < s + m; ++i) p.out[0][i] = bp_.process(at(p.in[0], i) * k, SvfFilter::Mode::BandPass) * level;
        }
    }
private:
    SvfFilter bp_;
};

class FilterBankModule final : public Module {
public:
    void prepare(double sr, uint32_t v, uint32_t pm) override {
        Module::prepare(sr, v, pm);
        static constexpr float centres[8] = {125, 250, 500, 1000, 2000, 4000, 8000, 12000};
        const float r = resonanceForQ(2.2f);
        k_ = 2.f - 2.1f * r;
        for (int b = 0; b < 8; ++b)
            bands_[static_cast<size_t>(b)].setCoefficients(std::tan(kPiF * std::min(centres[b], static_cast<float>(0.45 * sr)) / static_cast<float>(sr)), r, 1.f);
    }
    void reset() override { for (auto& b : bands_) b.reset(); }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        float gain[8];
        for (uint32_t b = 0; b < 8; ++b) gain[b] = P(g, b) / 100.f;
        const float level = P(g, 8) / 100.f;
        for (uint32_t i = 0; i < n; ++i) {
            const float x = at(p.in[0], i);
            float y = 0.f;
            for (size_t b = 0; b < 8; ++b) if (gain[b] != 0.f) y += bands_[b].process(x * k_, SvfFilter::Mode::BandPass) * gain[b];
            p.out[0][i] = y * level;
        }
    }
private:
    std::array<SvfFilter, 8> bands_{};
    float k_ = 1.f;
};

// ===========================================================================
// Analogue switch: manual / CV-selected / sequential (stepped by clock), with
// a 1 ms crossfade between positions so switching never clicks.
// ===========================================================================
class SwitchModule final : public Module {
public:
    void reset() override { current_ = target_ = 0; fade_ = 1.f; step_ = 0; }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        const int mode = I(g, 0), steps = std::clamp(I(g, 2) + 2, 2, 4);
        const float fadeStep = static_cast<float>(1.0 / (0.001 * sr_));
        for (uint32_t i = 0; i < n; ++i) {
            int want = current_;
            if (mode == 0) want = std::clamp(I(g, 1), 0, steps - 1);
            else if (mode == 1) want = std::clamp(static_cast<int>(clampf(at(p.in[4], i), 0.f, 0.9999f) * static_cast<float>(steps)), 0, steps - 1);
            else { float since; if (p.in[5] && clock_.step(p.in[5][i], since)) step_ = (step_ + 1) % steps; want = step_; }
            if (want != target_) { previous_ = current_; target_ = want; current_ = want; fade_ = 0.f; }
            const float now = at(p.in[static_cast<size_t>(current_)], i);
            if (fade_ < 1.f) {
                fade_ = std::min(1.f, fade_ + fadeStep);
                p.out[0][i] = now * fade_ + at(p.in[static_cast<size_t>(previous_)], i) * (1.f - fade_);
            } else p.out[0][i] = now;
        }
    }
private:
    int current_ = 0, target_ = 0, previous_ = 0, step_ = 0;
    float fade_ = 1.f;
    TriggerIn clock_;
};

class MultModule final : public Module {
public:
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext&) override {
        for (uint32_t o = 0; o < 4; ++o) for (uint32_t i = 0; i < n; ++i) p.out[o][i] = at(p.in[0], i);
    }
};

// Gate delay / pulse stretcher. Length 0 = keep the input gate's length.
class GateDelayModule final : public Module {
public:
    void reset() override { head_ = tail_ = 0; level_ = 0.f; high_ = false; t_ = 0; }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        const uint64_t delay = static_cast<uint64_t>(P(g, 0) * 0.001f * static_cast<float>(sr_));
        const float lengthMs = P(g, 1);
        const uint64_t length = static_cast<uint64_t>(lengthMs * 0.001f * static_cast<float>(sr_));
        for (uint32_t i = 0; i < n; ++i, ++t_) {
            const bool high = at(p.in[0], i) >= 0.5f;
            if (high && !high_) { push(t_ + delay, 1.f); if (lengthMs > 0.f) push(t_ + delay + std::max<uint64_t>(length, 1), 0.f); }
            if (!high && high_ && lengthMs <= 0.f) push(t_ + delay, 0.f);
            high_ = high;
            while (head_ != tail_ && events_[head_].time <= t_) { level_ = events_[head_].level; head_ = (head_ + 1) % kEvents; }
            p.out[0][i] = level_;
        }
    }
private:
    static constexpr uint32_t kEvents = 32;
    struct Ev { uint64_t time; float level; };
    void push(uint64_t time, float level) noexcept {
        const uint32_t next = (tail_ + 1) % kEvents;
        if (next == head_) return;   // queue full: drop (only with absurd gate rates)
        events_[tail_] = {time, level}; tail_ = next;
    }
    std::array<Ev, kEvents> events_{};
    uint32_t head_ = 0, tail_ = 0;
    uint64_t t_ = 0;
    float level_ = 0.f;
    bool high_ = false;
};

class Mixer4Module final : public Module {
public:
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        float gn[4];
        const float level = P(g, 4) / 100.f;
        for (uint32_t c = 0; c < 4; ++c) gn[c] = P(g, c) / 100.f * level;
        for (uint32_t i = 0; i < n; ++i) {
            float s = 0.f;
            for (uint32_t c = 0; c < 4; ++c) if (p.in[c]) s += p.in[c][i] * gn[c];
            p.out[0][i] = s; p.out[1][i] = -s;
        }
    }
};

class VoltageProcModule final : public Module {
public:
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        VoltageProcessor vp;
        vp.gain1 = P(g, 0) / 100.f; vp.gain2 = P(g, 1) / 100.f; vp.offset = P(g, 2); vp.invert = B(g, 3); vp.limit = P(g, 4) / 100.f;
        for (uint32_t i = 0; i < n; ++i) p.out[0][i] = vp.process(at(p.in[0], i), at(p.in[1], i));
    }
};

class SlewModule final : public Module {
public:
    void prepare(double sr, uint32_t v, uint32_t pm) override { Module::prepare(sr, v, pm); slew_.prepare(sr); }
    void reset() override { slew_.reset(0.f); }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        slew_.set(P(g, 0), P(g, 1), I(g, 2) == 1 ? Slew::Mode::Linear : Slew::Mode::Exponential);
        for (uint32_t i = 0; i < n; ++i) p.out[0][i] = slew_.process(at(p.in[0], i));
    }
private:
    Slew slew_;
};

class FollowerModule final : public Module {
public:
    void prepare(double sr, uint32_t v, uint32_t pm) override { Module::prepare(sr, v, pm); f_.prepare(sr); }
    void reset() override { f_.reset(); }   // follower
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        f_.set(P(g, 0), P(g, 1), I(g, 2) == 1 ? EnvelopeFollower::Detector::Rms : EnvelopeFollower::Detector::Peak, P(g, 3) / 100.f, P(g, 4) / 100.f);
        for (uint32_t i = 0; i < n; ++i) p.out[0][i] = f_.process(at(p.in[0], i));
    }
private:
    EnvelopeFollower f_;
};

// Sample & hold. Unpatched: follows the global S&H (one shared random stream,
// as on the hardware). Patched: samples this voice's signal on the global
// clock or on its own trigger input, with per-voice slew.
class SampleHoldModule final : public Module {
public:
    void prepare(double sr, uint32_t v, uint32_t pm) override { Module::prepare(sr, v, pm); slew_.prepare(sr); }
    void reset() override { held_ = 0.f; slew_.reset(0.f); }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        if (!p.in[0] && !p.in[1]) { for (uint32_t i = 0; i < n; ++i) p.out[0][i] = g.shGlobalValue; return; }
        slew_.set(P(g, 1), P(g, 1), Slew::Mode::Exponential);
        for (uint32_t i = 0; i < n; ++i) {
            float since;
            const bool clock = static_cast<int32_t>(i) == g.shClockOffset;
            const bool trig = p.in[1] && trig_.step(p.in[1][i], since);
            if (clock || trig) held_ = p.in[0] ? p.in[0][i] : g.shNoise;
            p.out[0][i] = slew_.process(held_);
        }
    }
private:
    Slew slew_;
    TriggerIn trig_;
    float held_ = 0.f;
};

// Voice output: equal-power pan from the stereo-spread pattern (+ pan CV),
// and the +6 dB that restores the mixer's headroom.
class OutputModule final : public Module {
public:
    void process(const Ports& p, uint32_t n, VoiceContext& v, const GlobalContext& g) override {
        static constexpr float pattern[4] = {-1.f, 1.f, -0.5f, 0.5f};
        const float basePos = clampf(g.stereoSpread * pattern[v.index % 4] + v.panMod, -1.f, 1.f);
        // Matrix amplitude: ramp across the block to avoid zipper noise.
        const float ampTarget = clampf(v.ampMod, 0.f, 2.f) * 2.f;
        const float ampStep = (ampTarget - amp_) / static_cast<float>(std::max(n, 1u));
        if (!p.in[1]) {
            if (basePos != lastPos_) { const float a = (basePos + 1.f) * 0.25f * kPiF; l_ = std::cos(a); r_ = std::sin(a); lastPos_ = basePos; }
            for (uint32_t i = 0; i < n; ++i) { amp_ += ampStep; const float s = amp_ * at(p.in[0], i); v.outL[i] = s * l_; v.outR[i] = s * r_; }
            amp_ = ampTarget;
            return;
        }
        for (uint32_t i = 0; i < n; ++i) {
            const float a = (clampf(basePos + p.in[1][i], -1.f, 1.f) + 1.f) * 0.25f * kPiF;
            amp_ += ampStep;
            const float s = amp_ * at(p.in[0], i);
            v.outL[i] = s * std::cos(a); v.outR[i] = s * std::sin(a);
        }
        amp_ = ampTarget;
    }
    void reset() override { amp_ = 2.f; }
private:
    float lastPos_ = -2.f, l_ = 0.7071f, r_ = 0.7071f, amp_ = 2.f;
};

// LFO rack module (instances LFO 1–3): exposes the voice's LFO output — the
// same signal the modulation matrix uses — on patchable jacks.
class LfoModule final : public Module {
public:
    explicit LfoModule(uint32_t k) : k_(k) {}
    void process(const Ports& p, uint32_t n, VoiceContext& v, const GlobalContext&) override {
        const float* src = v.lfo[k_] ? v.lfo[k_] + v.lfoOffset : nullptr;
        for (uint32_t i = 0; i < n; ++i) {
            const float x = src ? src[i] : 0.f;
            p.out[0][i] = x; p.out[1][i] = -x;
        }
    }
private:
    uint32_t k_;
};

// Diode / transistor ladder modules: in, cv1, cv2 → out. Cutoff per 8 samples.
template <typename Filter, bool Oversample>
class LadderVariantModule final : public Module {
public:
    void reset() override { f_.reset(); os_.reset(); }
    void noteStart(const VoiceContext&, const GlobalContext&, bool fromIdle) override { if (fromIdle) reset(); }
    void process(const Ports& p, uint32_t n, VoiceContext&, const GlobalContext& g) override {
        if constexpr (Oversample) os_.setFactor(g.oversampling);
        const double fsInt = sr_ * (Oversample ? os_.factor() : 1);
        const float cutoff = P(g, 0), res = P(g, 1) / 100.f, drive = dbToGain(P(g, 2));
        for (uint32_t s = 0; s < n; s += kSubBlock) {
            const uint32_t m = std::min(kSubBlock, n - s);
            const float fc = clampf(cutoff * std::exp2(5.f * (at(p.in[1], s) + at(p.in[2], s))), 8.f, static_cast<float>(0.45 * sr_));
            f_.setCoefficients(fc, fsInt, res, drive);
            for (uint32_t i = s; i < s + m; ++i) {
                const float x = at(p.in[0], i);
                if constexpr (Oversample) p.out[0][i] = os_.process(x, [this](float u) noexcept { return f_.process(u); });
                else p.out[0][i] = f_.process(x);
            }
        }
    }
private:
    Filter f_;
    Oversampler os_;
};

} // namespace

std::unique_ptr<Module> makeModule(ModType t, uint32_t instance) {
    switch (t) {
        case ModType::Keyboard:     return std::make_unique<KeyboardModule>();
        case ModType::Vco:          return std::make_unique<VcoModule>(instance - I_VCO1);
        case ModType::Noise:        return std::make_unique<NoiseModule>();
        case ModType::Ring:         return std::make_unique<RingModule>();
        case ModType::VoiceMixer:   return std::make_unique<VoiceMixerModule>();
        case ModType::Sum:          return std::make_unique<SumModule>();
        case ModType::FilterSvf:    return std::make_unique<FilterModule<false>>();
        case ModType::FilterLadder: return std::make_unique<FilterModule<true>>();
        case ModType::Vca:          return std::make_unique<VcaModule>(instance == I_VCA1);
        case ModType::Envelope:     return std::make_unique<EnvelopeModule>();
        case ModType::Ota:          return std::make_unique<OtaModule>();
        case ModType::Comb:         return std::make_unique<CombModule>();
        case ModType::Formant:      return std::make_unique<FormantModule>();
        case ModType::Vcbpf:        return std::make_unique<VcbpfModule>();
        case ModType::FilterBank:   return std::make_unique<FilterBankModule>();
        case ModType::Switch:       return std::make_unique<SwitchModule>();
        case ModType::Mult:         return std::make_unique<MultModule>();
        case ModType::GateDelay:    return std::make_unique<GateDelayModule>();
        case ModType::Mixer4:       return std::make_unique<Mixer4Module>();
        case ModType::VoltageProc:  return std::make_unique<VoltageProcModule>();
        case ModType::Slew:         return std::make_unique<SlewModule>();
        case ModType::Follower:     return std::make_unique<FollowerModule>();
        case ModType::SampleHold:   return std::make_unique<SampleHoldModule>();
        case ModType::Output:       return std::make_unique<OutputModule>();
        case ModType::DiodeLadder:  return std::make_unique<LadderVariantModule<DiodeLadder, true>>();
        case ModType::TransistorLadder: return std::make_unique<LadderVariantModule<TransistorLadder, false>>();   // runs 2× internally
        case ModType::Lfo:          return std::make_unique<LfoModule>(instance - I_LFO1);
    }
    return nullptr;
}

} // namespace vektur
