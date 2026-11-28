// Dynamics.cpp — Compressor (soft knee, peak/RMS, sidechain HPF, stereo
// link, auto makeup), Limiter (3 ms look-ahead brick wall: the output never
// exceeds the ceiling), Gate/Expander (hysteresis, hold, range) and Stereo
// Width (M/S width, mid/side gain, bass mono, pan, Haas).
#include "EffectsInternal.hpp"

#include <atomic>

namespace vektur::fx {
namespace {

// Static gain computer with a soft knee (dB in → dB out).
inline float compressCurve(float x, float t, float ratio, float knee) noexcept {
    const float over = x - t;
    if (knee > 0.f && 2.f * std::fabs(over) <= knee) {
        const float k = over + 0.5f * knee;
        return x + (1.f / ratio - 1.f) * k * k / (2.f * knee);
    }
    return over <= 0.f ? x : t + over / ratio;
}

class Compressor final : public Effect {
public:
    explicit Compressor(float sr) : sr_(sr) {}
    void reset() noexcept override { for (int c = 0; c < 2; ++c) { hp_[c].reset(); rms_[c] = 0.f; gr_[c] = 0.f; } }
    float meter() const noexcept override { return meter_.load(std::memory_order_relaxed); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float t = p.v[0], ratio = std::max(1.f, p.v[1]), knee = p.v[4];
        const float att = tauCoef(p.v[2], sr_), rel = tauCoef(p.v[3], sr_);
        float makeup = p.v[5];
        if (p.v[6] >= 0.5f) makeup += -0.5f * t * (1.f - 1.f / ratio);   // half the gain reduction at 0 dBFS
        const float mk = dbToGain(makeup);
        const bool rms = p.v[7] >= 0.5f, hpOn = p.v[8] > 21.f;
        const float link = p.v[9] * 0.01f;
        const bool vintage = p.v[10] >= 0.5f;
        const float rmsCoef = tauCoef(10.f, sr_);
        for (int c = 0; c < 2; ++c) hp_[c].set(Biquad::HighPass, p.v[8], 0.707f, 0.f, sr_);
        float* io[2] = {L, R};
        float worst = 0.f;
        for (uint32_t i = 0; i < n; ++i) {
            float lvl[2];
            for (int c = 0; c < 2; ++c) {
                float s = io[c][i];
                if (hpOn) s = hp_[c].process(s);
                if (rms) { rms_[c] = rmsCoef * rms_[c] + (1.f - rmsCoef) * s * s; lvl[c] = std::sqrt(rms_[c]) * 1.41421f; }
                else lvl[c] = std::fabs(s);
            }
            const float linked = std::max(lvl[0], lvl[1]);
            for (int c = 0; c < 2; ++c) {
                const float x = gainToDb(lerp(lvl[c], linked, link));
                const float target = compressCurve(x, t, ratio, knee) - x;    // ≤ 0
                gr_[c] = target < gr_[c] ? att * gr_[c] + (1.f - att) * target : rel * gr_[c] + (1.f - rel) * target;
                float y = io[c][i] * dbToGain(gr_[c]) * mk;
                if (vintage) y = softClip(y * 0.9f) / 0.9f;
                io[c][i] = y;
                worst = std::min(worst, gr_[c]);
            }
        }
        meter_.store(worst, std::memory_order_relaxed);
    }
private:
    float sr_;
    Biquad hp_[2];
    float rms_[2]{}, gr_[2]{};
    std::atomic<float> meter_{0.f};
};

// -------------------------------------------------------------------------
class Limiter final : public Effect {
public:
    explicit Limiter(float sr) : sr_(sr), la_(std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(0.003f * sr)))) {
        for (int c = 0; c < 2; ++c) audio_[c].allocate(la_ + 4);
        hold_.assign(la_ + 1, 1.f);
        dqVal_.assign(la_ + 2, 1.f); dqIdx_.assign(la_ + 2, 0);
    }
    uint32_t latency() const noexcept override { return la_; }
    float meter() const noexcept override { return meter_.load(std::memory_order_relaxed); }
    void reset() noexcept override {
        audio_[0].reset(); audio_[1].reset();
        std::fill(hold_.begin(), hold_.end(), 1.f);
        dqHead_ = dqTail_ = 0; t_ = 0; boxSum_ = static_cast<double>(la_ + 1); env_ = 1.f;
    }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float in = dbToGain(p.v[0]), ceil = dbToGain(p.v[1]), rel = tauCoef(p.v[2], sr_);
        const float link = p.v[3] * 0.01f;
        const bool soft = p.v[4] >= 0.5f;
        float worst = 1.f;
        for (uint32_t i = 0; i < n; ++i) {
            const float x[2] = {L[i] * in, R[i] * in};
            float r[2];
            for (int c = 0; c < 2; ++c) { const float a = std::fabs(x[c]); r[c] = a > ceil ? ceil / a : 1.f; }
            const float rl = std::min(r[0], r[1]);
            const float need = std::min(lerp(r[0], rl, link), lerp(r[1], rl, link));   // one gain for both channels: never exceeds either
            // Sliding minimum over the last la_+1 requirements (monotonic deque).
            const uint64_t t = t_++;
            const uint32_t cap = la_ + 2;
            while (dqTail_ != dqHead_ && dqVal_[(dqTail_ + cap - 1) % cap] >= need) dqTail_ = (dqTail_ + cap - 1) % cap;
            dqVal_[dqTail_] = need; dqIdx_[dqTail_] = t; dqTail_ = (dqTail_ + 1) % cap;
            while (dqIdx_[dqHead_] + la_ < t) dqHead_ = (dqHead_ + 1) % cap;
            const float h = dqVal_[dqHead_];
            // Box-average the held minimum over la_+1 samples: the ramp reaches
            // the required gain exactly when the peak leaves the delay line.
            const uint32_t slot = static_cast<uint32_t>(t % (la_ + 1));
            boxSum_ += static_cast<double>(h) - hold_[slot];
            hold_[slot] = h;
            const float b = static_cast<float>(boxSum_ / static_cast<double>(la_ + 1));
            env_ = b < env_ ? b : rel * env_ + (1.f - rel) * b;
            const float g = std::min(env_, b);
            for (int c = 0; c < 2; ++c) audio_[c].write(x[c]);
            float yl = audio_[0].tap(la_ + 1) * g, yr = audio_[1].tap(la_ + 1) * g;
            if (soft) { yl = softClip(yl / ceil) * ceil; yr = softClip(yr / ceil) * ceil; }
            // Hard safety at the ceiling (guards float rounding of the ramp).
            L[i] = clampf(yl, -ceil, ceil); R[i] = clampf(yr, -ceil, ceil);
            worst = std::min(worst, g);
        }
        meter_.store(gainToDb(worst), std::memory_order_relaxed);
    }
private:
    float sr_;
    uint32_t la_;
    DelayLine audio_[2];
    std::vector<float> hold_, dqVal_;
    std::vector<uint64_t> dqIdx_;
    uint32_t dqHead_ = 0, dqTail_ = 0;
    uint64_t t_ = 0;
    double boxSum_ = 0.0;
    float env_ = 1.f;
    std::atomic<float> meter_{0.f};
public:
    void init() noexcept { reset(); }
};

// -------------------------------------------------------------------------
class Gate final : public Effect {
public:
    explicit Gate(float sr) : sr_(sr) {}
    void reset() noexcept override { hp_[0].reset(); hp_[1].reset(); env_ = 0.f; gain_ = 0.f; open_ = false; holdLeft_ = 0; }
    float meter() const noexcept override { return meter_.load(std::memory_order_relaxed); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float thr = p.v[0], range = p.v[1], hyst = p.v[5], ratio = std::max(1.f, p.v[7]);
        const float att = tauCoef(p.v[2], sr_), rel = tauCoef(p.v[4], sr_);
        const auto hold = static_cast<uint32_t>(p.v[3] * 0.001f * sr_);
        const bool hpOn = p.v[6] > 21.f;
        hp_[0].set(Biquad::HighPass, p.v[6], 0.707f, 0.f, sr_); hp_[1].set(Biquad::HighPass, p.v[6], 0.707f, 0.f, sr_);
        const float envAtt = tauCoef(0.1f, sr_), envRel = tauCoef(20.f, sr_);
        float worst = 0.f;
        for (uint32_t i = 0; i < n; ++i) {
            float s0 = L[i], s1 = R[i];
            if (hpOn) { s0 = hp_[0].process(s0); s1 = hp_[1].process(s1); }
            const float a = std::max(std::fabs(s0), std::fabs(s1));
            env_ = a > env_ ? envAtt * env_ + (1.f - envAtt) * a : envRel * env_ + (1.f - envRel) * a;
            const float db = gainToDb(env_);
            if (db > thr) { open_ = true; holdLeft_ = hold; }
            else if (db < thr - hyst) { if (holdLeft_ > 0) --holdLeft_; else open_ = false; }
            const float target = open_ ? 0.f : std::max(range, (db - thr) * (ratio - 1.f));
            gain_ = target > gain_ ? att * gain_ + (1.f - att) * target : rel * gain_ + (1.f - rel) * target;
            const float g = dbToGain(gain_);
            L[i] *= g; R[i] *= g;
            worst = std::min(worst, gain_);
        }
        meter_.store(worst, std::memory_order_relaxed);
    }
private:
    float sr_;
    Biquad hp_[2];
    float env_ = 0.f, gain_ = 0.f;
    bool open_ = false;
    uint32_t holdLeft_ = 0;
    std::atomic<float> meter_{0.f};
};

// -------------------------------------------------------------------------
class Width final : public Effect {
public:
    explicit Width(float sr) : sr_(sr) { haas_.allocate(static_cast<uint32_t>(0.032f * sr)); }
    void reset() noexcept override { haas_.reset(); bass_[0].reset(); bass_[1].reset(); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float width = p.v[0] * 0.01f, mg = dbToGain(p.v[1]), sg = dbToGain(p.v[2]);
        const bool bassOn = p.v[3] > 21.f;
        bass_[0].set(Biquad::HighPass, p.v[3], 0.5412f, 0.f, sr_); bass_[1].set(Biquad::HighPass, p.v[3], 1.3066f, 0.f, sr_);   // 4th-order Butterworth
        const float pan = p.v[4] * 0.01f;
        const float pl = std::cos((pan + 1.f) * kPi * 0.25f) * 1.41421f, pr = std::sin((pan + 1.f) * kPi * 0.25f) * 1.41421f;
        const float haas = p.v[5] * 0.001f * sr_;
        for (uint32_t i = 0; i < n; ++i) {
            const float m = 0.5f * (L[i] + R[i]) * mg;
            float s = 0.5f * (L[i] - R[i]) * sg * width;
            if (bassOn) s = bass_[1].process(bass_[0].process(s));   // keep only the side signal above the frequency
            float l = (m + s) * pl, r = (m - s) * pr;
            haas_.write(r);
            if (haas >= 1.f) r = haas_.readLinear(haas);
            L[i] = l; R[i] = r;
        }
    }
private:
    float sr_;
    DelayLine haas_;
    Biquad bass_[2];
};

} // namespace

std::unique_ptr<Effect> makeDynamicsFamily(uint32_t type, float sr) {
    switch (type) {
        case FxCompressor: return std::make_unique<Compressor>(sr);
        case FxLimiter: { auto l = std::make_unique<Limiter>(sr); l->init(); return l; }
        case FxGate: return std::make_unique<Gate>(sr);
        case FxWidth: return std::make_unique<Width>(sr);
        default: return nullptr;
    }
}

} // namespace vektur::fx
