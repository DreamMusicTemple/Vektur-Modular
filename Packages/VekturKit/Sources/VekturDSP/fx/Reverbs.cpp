// Reverbs.cpp — Plate (Dattorro figure-eight tank), Hall and Room (8-line
// feedback delay network with Householder mixing, three-band decay, early
// reflections), Spring (dispersive all-pass chains in a feedback loop).
#include "EffectsInternal.hpp"

namespace vektur::fx {
namespace {

struct OutputTone {
    Biquad lo[2], hi[2];
    void set(float lowCut, float highCut, float sr) noexcept {
        for (int c = 0; c < 2; ++c) { lo[c].set(Biquad::HighPass, lowCut, 0.707f, 0.f, sr); hi[c].set(Biquad::LowPass, highCut, 0.707f, 0.f, sr); }
    }
    void process(float& l, float& r, bool lowOn, bool highOn) noexcept {
        if (lowOn) { l = lo[0].process(l); r = lo[1].process(r); }
        if (highOn) { l = hi[0].process(l); r = hi[1].process(r); }
    }
    void reset() noexcept { for (int c = 0; c < 2; ++c) { lo[c].reset(); hi[c].reset(); } }
};

// -------------------------------------------------------------------------
class Plate final : public Effect {
public:
    explicit Plate(float sr) : sr_(sr), k_(sr / 29761.f) {
        pre_.allocate(static_cast<uint32_t>(0.26f * sr));
        const float diff[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; ++i) { diffLen_[i] = diff[i] * k_; inAp_[i].allocate(static_cast<uint32_t>(diffLen_[i]) + 4); }
        const float tank[8] = {672, 4453, 1800, 3720, 908, 4217, 2656, 3163};
        for (int i = 0; i < 8; ++i) base_[i] = tank[i] * k_;
        apA1_.allocate(static_cast<uint32_t>((672 + 40) * k_) + 8); apB1_.allocate(static_cast<uint32_t>((908 + 40) * k_) + 8);
        dA1_.allocate(static_cast<uint32_t>(4453 * k_) + 8); apA2_.allocate(static_cast<uint32_t>(1800 * k_) + 8); dA2_.allocate(static_cast<uint32_t>(3720 * k_) + 8);
        dB1_.allocate(static_cast<uint32_t>(4217 * k_) + 8); apB2_.allocate(static_cast<uint32_t>(2656 * k_) + 8); dB2_.allocate(static_cast<uint32_t>(3163 * k_) + 8);
    }
    void reset() noexcept override {
        pre_.reset(); for (auto& a : inAp_) a.reset();
        apA1_.reset(); apB1_.reset(); dA1_.reset(); apA2_.reset(); dA2_.reset(); dB1_.reset(); apB2_.reset(); dB2_.reset();
        bwState_ = dampA_ = dampB_ = fbA_ = fbB_ = 0.f; tone_.reset();
    }
    float tailSeconds(const FxParams& p) const noexcept override {
        const float decay = 0.1f + 0.88f * p.v[1] * 0.01f;
        return feedbackTail((4453.f + 3720.f + 4217.f + 3163.f) * 0.5f * k_ / sr_, decay * decay);
    }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float preD = std::max(1.f, p.v[0] * 0.001f * sr_);
        const float decay = 0.1f + 0.88f * p.v[1] * 0.01f;
        const float size = 0.3f + 0.7f * p.v[2] * 0.01f;
        const float damp = 0.05f + 0.85f * p.v[3] * 0.01f;
        const float bw = 0.1f + 0.8999f * p.v[4] * 0.01f;
        const float diff = p.v[5] * 0.01f;
        const float modD = p.v[6] * 0.01f * 16.f * k_;
        const float width = p.v[9] * 0.01f;
        tone_.set(p.v[7], p.v[8], sr_);
        const bool lowOn = p.v[7] > 21.f, highOn = p.v[8] < 19900.f;
        const float id1 = 0.75f * diff, id2 = 0.625f * diff, dd1 = 0.7f * diff, dd2 = std::clamp(decay + 0.15f, 0.25f, 0.5f) * diff;
        for (uint32_t i = 0; i < n; ++i) {
            const float in = 0.5f * (L[i] + R[i]);
            pre_.write(in);
            float x = pre_.readLinear(preD);
            bwState_ = flushDenormal(bwState_ + bw * (x - bwState_)); x = bwState_;
            x = inAp_[0].process(x, diffLen_[0], id1); x = inAp_[1].process(x, diffLen_[1], id1);
            x = inAp_[2].process(x, diffLen_[2], id2); x = inAp_[3].process(x, diffLen_[3], id2);
            const float m = modD * lfo_.next(1.f, sr_), m2 = modD * lfo_.value(0.25f);
            // Branch A
            float a = apA1_.process(x + fbB_, base_[0] * size + m + 2.f, -dd1);
            dA1_.write(a); a = dA1_.readLinear(base_[1] * size);
            dampA_ = flushDenormal(a * (1.f - damp) + dampA_ * damp); a = dampA_ * decay;
            a = apA2_.process(a, base_[2] * size, dd2);
            dA2_.write(a); fbA_ = dA2_.readLinear(base_[3] * size) * decay;
            // Branch B
            float b = apB1_.process(x + fbA_, base_[4] * size + m2 + 2.f, -dd1);
            dB1_.write(b); b = dB1_.readLinear(base_[5] * size);
            dampB_ = flushDenormal(b * (1.f - damp) + dampB_ * damp); b = dampB_ * decay;
            b = apB2_.process(b, base_[6] * size, dd2);
            dB2_.write(b); fbB_ = dB2_.readLinear(base_[7] * size) * decay;
            // Output taps (Dattorro), scaled by size.
            auto t = [&](float v) { return std::max(1.f, v * k_ * size); };
            float l = dB1_.readLinear(t(266)) + dB1_.readLinear(t(2974)) - apB2_.line.readLinear(t(1913)) + dB2_.readLinear(t(1996))
                    - dA1_.readLinear(t(1990)) - apA2_.line.readLinear(t(187)) - dA2_.readLinear(t(1066));
            float r = dA1_.readLinear(t(353)) + dA1_.readLinear(t(3627)) - apA2_.line.readLinear(t(1228)) + dA2_.readLinear(t(2673))
                    - dB1_.readLinear(t(2111)) - apB2_.line.readLinear(t(335)) - dB2_.readLinear(t(121));
            l *= 0.6f; r *= 0.6f;
            tone_.process(l, r, lowOn, highOn);
            applyWidth(l, r, width);
            L[i] = l; R[i] = r;
        }
    }
private:
    float sr_, k_;
    DelayLine pre_;
    AllPassLine inAp_[4];
    float diffLen_[4]{}, base_[8]{};
    AllPassLine apA1_, apB1_, apA2_, apB2_;
    DelayLine dA1_, dA2_, dB1_, dB2_;
    float bwState_ = 0.f, dampA_ = 0.f, dampB_ = 0.f, fbA_ = 0.f, fbB_ = 0.f;
    SineLfo lfo_;
    OutputTone tone_;
};

// -------------------------------------------------------------------------
// FDN: 8 lines, Householder feedback, per-line three-band decay for the
// requested RT60, modulated lines, input diffusion, early reflections.
class FdnReverb final : public Effect {
public:
    FdnReverb(float sr, bool hall) : sr_(sr), hall_(hall) {
        static constexpr float kHall[8] = {47.3f, 53.9f, 61.3f, 67.1f, 73.7f, 79.9f, 89.3f, 97.1f};   // ms at size 100 %
        static constexpr float kRoom[8] = {11.3f, 13.7f, 15.1f, 17.9f, 19.3f, 21.1f, 23.9f, 26.3f};
        for (int i = 0; i < 8; ++i) { baseMs_[i] = hall ? kHall[i] : kRoom[i]; lines_[i].allocate(static_cast<uint32_t>(baseMs_[i] * 0.0021f * sr) + 64); }
        static constexpr float kDiff[4] = {4.7f, 3.6f, 12.7f, 9.3f};
        for (int i = 0; i < 4; ++i) { diffLen_[i] = kDiff[i] * 0.001f * sr; diff_[i][0].allocate(static_cast<uint32_t>(diffLen_[i]) + 4); diff_[i][1].allocate(static_cast<uint32_t>(diffLen_[i] * 1.07f) + 4); }
        pre_.allocate(static_cast<uint32_t>(0.26f * sr)); preR_.allocate(static_cast<uint32_t>(0.26f * sr));
        er_.allocate(static_cast<uint32_t>(0.12f * sr));
    }
    void reset() noexcept override {
        for (auto& l : lines_) l.reset();
        for (auto& d : diff_) { d[0].reset(); d[1].reset(); }
        pre_.reset(); preR_.reset(); er_.reset(); tone_.reset();
        for (int i = 0; i < 8; ++i) { lpLo_[i].reset(); lpHi_[i].reset(); }
    }
    float tailSeconds(const FxParams& p) const noexcept override { return p.v[1] * std::max(1.f, hall_ ? p.v[4] : 1.f) * 1.2f + 0.3f; }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float preD = std::max(1.f, p.v[0] * 0.001f * sr_);
        const float rt = std::max(0.05f, p.v[1]);
        const float size = 0.35f + 0.65f * p.v[2] * 0.01f;
        float highMul, lowMul, diffusion, modDepth, modRate, early;
        if (hall_) { highMul = p.v[3]; lowMul = p.v[4]; diffusion = p.v[5] * 0.01f; modDepth = p.v[6] * 0.01f; modRate = p.v[7]; early = p.v[8] * 0.01f; }
        else { highMul = 1.f - 0.85f * p.v[3] * 0.01f; lowMul = 1.f; diffusion = p.v[5] * 0.01f; modDepth = 0.15f; modRate = 0.7f; early = p.v[4] * 0.01f; }
        const float lowCut = hall_ ? p.v[9] : p.v[6], highCut = hall_ ? p.v[10] : p.v[7], width = (hall_ ? p.v[11] : p.v[8]) * 0.01f;
        tone_.set(lowCut, highCut, sr_);
        const bool lowOn = lowCut > 21.f, highOn = highCut < 19900.f;
        float len[8], gLo[8], gMid[8], gHi[8];
        for (int i = 0; i < 8; ++i) {
            len[i] = baseMs_[i] * 0.001f * sr_ * size;
            const float secs = len[i] / sr_;
            gMid[i] = std::pow(10.f, -3.f * secs / rt);
            gLo[i] = std::pow(10.f, -3.f * secs / (rt * lowMul));
            gHi[i] = std::pow(10.f, -3.f * secs / (rt * highMul));
            lpLo_[i].set(250.f, sr_); lpHi_[i].set(hall_ ? 3500.f : 2500.f, sr_);
        }
        const float gd = 0.45f + 0.3f * diffusion;
        const float mod = modDepth * 0.0008f * sr_;
        static constexpr float kErMs[12] = {4.3f, 7.9f, 11.2f, 15.7f, 19.1f, 24.8f, 31.3f, 37.9f, 46.1f, 55.3f, 66.7f, 79.4f};
        for (uint32_t i = 0; i < n; ++i) {
            pre_.write(L[i]); preR_.write(R[i]);
            float in[2] = {pre_.readLinear(preD), preR_.readLinear(preD)};
            const float preOut = 0.5f * (in[0] + in[1]);
            // Early reflections from the pre-delayed mono signal.
            er_.write(preOut);
            float erL = 0.f, erR = 0.f;
            for (int t = 0; t < 12; ++t) {
                const float v = er_.readLinear(std::max(1.f, kErMs[t] * size * 0.001f * sr_)) * (1.f - static_cast<float>(t) / 14.f);
                if (t & 1) erR += v; else erL += v;
            }
            // Input diffusion per channel (pre-delayed).
            for (int ch = 0; ch < 2; ++ch) {
                float x = in[ch];
                for (int k = 0; k < 4; ++k) x = diff_[k][ch].process(x, diffLen_[k] * (ch ? 1.07f : 1.f), gd);
                in[ch] = x;
            }
            // FDN
            float y[8];
            const float lfo = lfo_.next(modRate, sr_);
            for (int k = 0; k < 8; ++k) {
                const float m = k < 4 ? mod * (k & 1 ? lfo : lfo_.value(0.25f * static_cast<float>(k))) : 0.f;
                y[k] = lines_[k].readLinear(std::max(1.f, len[k] + m));
            }
            float sum = 0.f;
            for (int k = 0; k < 8; ++k) {
                // Three-band decay: low (<250 Hz), mid, high (>3.5 kHz).
                const float lo = lpLo_[k].lp(y[k]);
                const float midHi = y[k] - lo;
                const float mid = lpHi_[k].lp(midHi);
                const float hi = midHi - mid;
                y[k] = lo * gLo[k] + mid * gMid[k] + hi * gHi[k];
                sum += y[k];
            }
            const float h = sum * 0.25f;   // Householder: I − (2/N)·1·1ᵀ
            for (int k = 0; k < 8; ++k) {
                const float feed = y[k] - h + ((k & 1) ? in[1] : in[0]) * 0.5f;
                lines_[k].write(flushDenormal(feed));
            }
            float l = 0.35f * (y[0] - y[2] + y[4] - y[6] + y[1] * 0.5f - y[5] * 0.5f);
            float r = 0.35f * (y[1] - y[3] + y[5] - y[7] + y[2] * 0.5f - y[6] * 0.5f);
            l += early * erL * 0.25f; r += early * erR * 0.25f;
            tone_.process(l, r, lowOn, highOn);
            applyWidth(l, r, width);
            L[i] = l; R[i] = r;
        }
    }
private:
    float sr_;
    bool hall_;
    float baseMs_[8]{}, diffLen_[4]{};
    DelayLine lines_[8], pre_, preR_, er_;
    AllPassLine diff_[4][2];
    OnePole lpLo_[8], lpHi_[8];
    SineLfo lfo_;
    OutputTone tone_;
};

// -------------------------------------------------------------------------
// Spring tank: each spring is a feedback loop of a transit delay and a long
// cascade of first-order all-passes (dispersion → the characteristic chirp).
class Spring final : public Effect {
public:
    explicit Spring(float sr) : sr_(sr) {
        for (int s = 0; s < 3; ++s) line_[s].allocate(static_cast<uint32_t>(0.08f * sr) + 8);
    }
    void reset() noexcept override {
        for (int s = 0; s < 3; ++s) { line_[s].reset(); for (auto& a : ap_[s]) a.reset(); tone_[s].reset(); fb_[s] = 0.f; }
        low_[0].reset(); low_[1].reset(); drip_.reset();
    }
    float tailSeconds(const FxParams& p) const noexcept override { return p.v[0] * 1.2f + 0.1f; }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float decay = std::max(0.2f, p.v[0]);
        const float tension = p.v[1] * 0.01f;
        const int springs = std::clamp(static_cast<int>(p.v[2]) + 1, 1, 3);
        const float dripAmt = p.v[3] * 0.01f;
        static constexpr float kTransitMs[3] = {33.f, 41.f, 37.f};
        const float a = -(0.55f + 0.2f * tension);     // all-pass coefficient: stronger dispersion with tension
        float g[3], len[3];
        for (int s = 0; s < 3; ++s) {
            len[s] = kTransitMs[s] * 0.001f * sr_;
            g[s] = std::pow(10.f, -3.f * (kTransitMs[s] * 0.001f) / decay);
            tone_[s].set(1800.f + 4000.f * p.v[4] * 0.01f, sr_);
        }
        low_[0].set(p.v[5], sr_); low_[1].set(p.v[5], sr_);
        drip_.set(1200.f, sr_);
        const float width = p.v[6] * 0.01f;
        for (uint32_t i = 0; i < n; ++i) {
            const float in = 0.5f * (L[i] + R[i]);
            const float transient = drip_.hp(in);
            const float x = in + dripAmt * 3.f * softClip(transient * 4.f) * 0.25f;
            float out[3] = {0.f, 0.f, 0.f};
            for (int s = 0; s < springs; ++s) {
                float v = x * 0.6f + fb_[s];
                for (auto& ap : ap_[s]) v = ap.process(v, a + 0.01f * static_cast<float>(s));
                line_[s].write(flushDenormal(v));
                const float y = line_[s].readLinear(len[s]);
                fb_[s] = tone_[s].lp(y) * g[s];
                out[s] = y;
            }
            float l = out[0] + 0.5f * out[2], r = (springs > 1 ? out[1] : out[0]) + 0.5f * out[2];
            l = low_[0].hp(l) * 0.5f; r = low_[1].hp(r) * 0.5f;
            applyWidth(l, r, width);
            L[i] = l; R[i] = r;
        }
    }
private:
    float sr_;
    DelayLine line_[3];
    AllPass1 ap_[3][40];
    OnePole tone_[3], low_[2], drip_;
    float fb_[3]{};
};

} // namespace

std::unique_ptr<Effect> makeReverbFamily(uint32_t type, float sr) {
    switch (type) {
        case FxPlate: return std::make_unique<Plate>(sr);
        case FxHall: return std::make_unique<FdnReverb>(sr, true);
        case FxRoom: return std::make_unique<FdnReverb>(sr, false);
        case FxSpring: return std::make_unique<Spring>(sr);
        default: return nullptr;
    }
}

} // namespace vektur::fx
