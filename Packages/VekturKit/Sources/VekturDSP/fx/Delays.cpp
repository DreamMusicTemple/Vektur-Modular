// Delays.cpp — Stereo Delay, Ping-Pong Delay, Tape Echo, BBD Delay.
#include "EffectsInternal.hpp"

namespace vektur::fx {
namespace {

constexpr float kMaxDelaySecs = 2.6f;

// Loop filter + drive shared by the digital delays.
struct LoopTone {
    OnePole lo, hi;
    void set(float lowCut, float highCut, float sr) noexcept { lo.set(lowCut, sr); hi.set(highCut, sr); }
    float process(float x, float drive) noexcept {
        float y = hi.lp(lo.hp(x));
        if (drive > 0.f) { const float k = 1.f + 4.f * drive; y = softClip(y * k) / softClip(k) ; }
        return y;
    }
    void reset() noexcept { lo.reset(); hi.reset(); }
};

struct Ducker {
    EnvFollower env;
    float att = 0.f, rel = 0.f;
    void prepare(float sr) noexcept { att = tauCoef(5.f, sr); rel = tauCoef(250.f, sr); }
    float gain(float in, float amount) noexcept {
        const float e = env.process(in, att, rel);
        return 1.f - amount * clampf(e * 4.f, 0.f, 1.f);
    }
};

// -------------------------------------------------------------------------
class StereoDelay final : public Effect {
public:
    explicit StereoDelay(float sr) : sr_(sr) {
        const auto n = static_cast<uint32_t>(kMaxDelaySecs * sr);
        dl_[0].allocate(n); dl_[1].allocate(n);
        duck_.prepare(sr);
    }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) { dl_[c].reset(); tone_[c].reset(); fb_[c] = 0.f; }
        primed_ = false;
    }
    float tailSeconds(const FxParams& p) const noexcept override {
        return feedbackTail(std::max(cur_[0], cur_[1]) / sr_, p.v[4] * 0.01f);
    }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext& c) noexcept override {
        const float target[2] = {syncSeconds(static_cast<int>(p.v[0]), c.tempo, p.v[2] * 0.001f) * sr_,
                                 syncSeconds(static_cast<int>(p.v[1]), c.tempo, p.v[3] * 0.001f) * sr_};
        if (!primed_) { cur_[0] = target[0]; cur_[1] = target[1]; primed_ = true; }
        const float fb = p.v[4] * 0.01f, x = p.v[5] * 0.01f, depth = p.v[8] * 0.01f * 0.003f * sr_;
        const float drive = p.v[10] * 0.01f, duck = p.v[11] * 0.01f;
        for (int ch = 0; ch < 2; ++ch) tone_[ch].set(p.v[6], p.v[7], sr_);
        const float smooth = 1.f - tauCoef(60.f, sr_);
        for (uint32_t i = 0; i < n; ++i) {
            const float mod = lfo_.next(p.v[9], sr_);
            float y[2];
            for (int ch = 0; ch < 2; ++ch) {
                cur_[ch] += (target[ch] - cur_[ch]) * smooth;
                const float m = depth * (ch == 0 ? mod : lfo_.value(0.25f));
                y[ch] = dl_[ch].readCubic(std::max(2.f, cur_[ch] + m));
            }
            const float in[2] = {L[i], R[i]};
            const float g = duck > 0.f ? duck_.gain(0.5f * (in[0] + in[1]), duck) : 1.f;
            for (int ch = 0; ch < 2; ++ch) {
                const float loop = (1.f - x) * y[ch] + x * y[1 - ch];
                fb_[ch] = tone_[ch].process(loop, drive);
                dl_[ch].write(flushDenormal(in[ch] + fb * fb_[ch]));
            }
            L[i] = y[0] * g; R[i] = y[1] * g;
        }
    }
private:
    float sr_;
    DelayLine dl_[2];
    LoopTone tone_[2];
    SineLfo lfo_;
    Ducker duck_;
    float cur_[2]{}, fb_[2]{};
    bool primed_ = false;
};

// -------------------------------------------------------------------------
class PingPong final : public Effect {
public:
    explicit PingPong(float sr) : sr_(sr) {
        const auto n = static_cast<uint32_t>(kMaxDelaySecs * sr);
        dl_[0].allocate(n); dl_[1].allocate(n); duck_.prepare(sr);
    }
    void reset() noexcept override { dl_[0].reset(); dl_[1].reset(); tone_[0].reset(); tone_[1].reset(); primed_ = false; }
    float tailSeconds(const FxParams& p) const noexcept override { return feedbackTail(cur_ / sr_, p.v[2] * 0.01f); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext& c) noexcept override {
        const float target = syncSeconds(static_cast<int>(p.v[0]), c.tempo, p.v[1] * 0.001f) * sr_;
        if (!primed_) { cur_ = target; primed_ = true; }
        const float fb = p.v[2] * 0.01f, spread = p.v[3] * 0.01f, depth = p.v[6] * 0.01f * 0.003f * sr_;
        const float drive = p.v[8] * 0.01f, duck = p.v[9] * 0.01f, pan = p.v[10] * 0.01f, width = p.v[11] * 0.01f;
        const float panL = std::sqrt(0.5f * (1.f - pan)), panR = std::sqrt(0.5f * (1.f + pan));
        tone_[0].set(p.v[4], p.v[5], sr_); tone_[1].set(p.v[4], p.v[5], sr_);
        const float smooth = 1.f - tauCoef(60.f, sr_);
        for (uint32_t i = 0; i < n; ++i) {
            cur_ += (target - cur_) * smooth;
            const float m = depth * lfo_.next(p.v[7], sr_);
            const float yl = dl_[0].readCubic(std::max(2.f, cur_ + m)), yr = dl_[1].readCubic(std::max(2.f, cur_ - m));
            const float mono = 0.5f * (L[i] + R[i]);
            const float g = duck > 0.f ? duck_.gain(mono, duck) : 1.f;
            dl_[0].write(flushDenormal(mono * panL * 1.41421f + fb * tone_[0].process(yr, drive)));
            dl_[1].write(flushDenormal(mono * panR * 1.41421f + fb * tone_[1].process(yl, drive)));
            float l = lerp(0.5f * (yl + yr), yl, spread), r = lerp(0.5f * (yl + yr), yr, spread);
            applyWidth(l, r, width);
            L[i] = l * g; R[i] = r * g;
        }
    }
private:
    float sr_;
    DelayLine dl_[2];
    LoopTone tone_[2];
    SineLfo lfo_;
    Ducker duck_;
    float cur_ = 0.f;
    bool primed_ = false;
};

// -------------------------------------------------------------------------
// Tape echo: one tape loop, three playback heads at 1×, 2×, 3× the repeat
// time, wow (slow wandering) and flutter (fast) on the tape speed, record-
// head saturation, high-frequency loss growing with tape age, hiss.
class TapeEcho final : public Effect {
public:
    explicit TapeEcho(float sr) : sr_(sr) {
        line_.allocate(static_cast<uint32_t>(4.6f * sr));   // head 3 at 1.5 s
        duck_.prepare(sr);
    }
    void reset() noexcept override { line_.reset(); tone_.reset(); low_.reset(); wowTarget_ = wow_ = 0.f; primed_ = false; }
    float tailSeconds(const FxParams& p) const noexcept override { return feedbackTail(3.f * cur_ / sr_, p.v[2] * 0.01f); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext& c) noexcept override {
        static constexpr uint8_t kHeads[7] = {1, 2, 4, 3, 6, 5, 7};   // bit 0 = head 1 …
        const float target = syncSeconds(static_cast<int>(p.v[0]), c.tempo, p.v[1] * 0.001f) * sr_;
        if (!primed_) { cur_ = target; primed_ = true; }
        const float fb = p.v[2] * 0.01f, wowAmt = p.v[4] * 0.01f, flutAmt = p.v[5] * 0.01f, sat = p.v[6] * 0.01f;
        const float age = p.v[8] * 0.01f, width = p.v[10] * 0.01f, duck = p.v[11] * 0.01f;
        const uint8_t heads = kHeads[std::clamp(static_cast<int>(p.v[3]), 0, 6)];
        const int nh = (heads & 1) + ((heads >> 1) & 1) + ((heads >> 2) & 1);
        tone_.set(1500.f + 10000.f * (p.v[7] * 0.01f) * (1.f - 0.6f * age), sr_);
        low_.set(p.v[9], sr_);
        const float smooth = 1.f - tauCoef(120.f, sr_);
        const float drive = 1.f + 5.f * sat;
        for (uint32_t i = 0; i < n; ++i) {
            cur_ += (target - cur_) * smooth;
            if ((++wowCount_ & 2047u) == 0) wowTarget_ = rng_.bip();
            wow_ += (wowTarget_ - wow_) * 0.0005f;
            const float speedMod = 1.f + (0.004f * wowAmt * (1.f + age)) * (0.6f * wow_ + 0.4f * lfoW_.next(0.6f, sr_))
                                       + 0.0012f * flutAmt * lfoF_.next(9.f, sr_);
            float sum = 0.f, outL = 0.f, outR = 0.f;
            for (int h = 0; h < 3; ++h) {
                if (!(heads & (1 << h))) continue;
                const float y = line_.readCubic(std::max(4.f, cur_ * static_cast<float>(h + 1) * speedMod));
                sum += y;
                const float panH = h == 0 ? -0.4f : (h == 1 ? 0.4f : 0.f);
                outL += y * (1.f - panH); outR += y * (1.f + panH);
            }
            sum /= static_cast<float>(std::max(nh, 1));
            const float in = 0.5f * (L[i] + R[i]);
            const float g = duck > 0.f ? duck_.gain(in, duck) : 1.f;
            float rec = in + fb * tone_.lp(low_.hp(sum));
            rec = softClip(rec * drive) / softClip(drive);
            line_.write(flushDenormal(rec));
            const float hiss = age * 0.003f * rng_.bip();
            float l = outL / static_cast<float>(std::max(nh, 1)) + hiss, r = outR / static_cast<float>(std::max(nh, 1)) + hiss;
            applyWidth(l, r, width);
            L[i] = l * g; R[i] = r * g;
        }
    }
private:
    float sr_;
    DelayLine line_;
    OnePole tone_, low_;
    SineLfo lfoW_, lfoF_;
    Ducker duck_;
    Rng rng_{0x7A9Eu};
    uint32_t wowCount_ = 0;
    float cur_ = 0.f, wow_ = 0.f, wowTarget_ = 0.f;
    bool primed_ = false;
};

// -------------------------------------------------------------------------
// Bucket-brigade delay: bandwidth falls as the delay gets longer (fixed
// 4096-stage clock model), companded noise and soft saturation ("grit").
class BBDDelay final : public Effect {
public:
    explicit BBDDelay(float sr) : sr_(sr) {
        dl_[0].allocate(static_cast<uint32_t>(0.7f * sr)); dl_[1].allocate(static_cast<uint32_t>(0.7f * sr));
    }
    void reset() noexcept override { for (int c = 0; c < 2; ++c) { dl_[c].reset(); pre_[c].reset(); post_[c].reset(); low_[c].reset(); } primed_ = false; }
    float tailSeconds(const FxParams& p) const noexcept override { return feedbackTail(cur_ / sr_, p.v[2] * 0.01f); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext& c) noexcept override {
        const float secs = std::clamp(syncSeconds(static_cast<int>(p.v[0]), c.tempo, p.v[1] * 0.001f), 0.02f, 0.65f);
        const float target = secs * sr_;
        if (!primed_) { cur_ = target; primed_ = true; }
        const float fb = p.v[2] * 0.01f, tone = p.v[3] * 0.01f, depth = p.v[4] * 0.01f * 0.004f * sr_;
        const float grit = p.v[6] * 0.01f, drive = 1.f + 3.f * p.v[7] * 0.01f, width = p.v[8] * 0.01f;
        // Clock = 4096 stages / (2·delay); usable bandwidth ≈ 0.3 · clock.
        const float bw = clampf(0.3f * 4096.f / (2.f * secs) * (0.5f + tone), 800.f, 0.45f * sr_);
        for (int ch = 0; ch < 2; ++ch) { pre_[ch].set(Biquad::LowPass, bw, 0.707f, 0.f, sr_); post_[ch].set(Biquad::LowPass, bw, 0.707f, 0.f, sr_); low_[ch].set(p.v[9], sr_); }
        const float smooth = 1.f - tauCoef(80.f, sr_);
        for (uint32_t i = 0; i < n; ++i) {
            cur_ += (target - cur_) * smooth;
            const float m = depth * lfo_.next(p.v[5], sr_);
            const float in[2] = {L[i], R[i]};
            float y[2];
            for (int ch = 0; ch < 2; ++ch) {
                const float raw = dl_[ch].readCubic(std::max(2.f, cur_ + (ch ? -m : m)));
                const float noise = grit * 0.002f * rng_.bip() * (0.3f + std::fabs(raw));   // companded: noise follows the signal
                y[ch] = post_[ch].process(raw + noise);
                float w = pre_[ch].process(in[ch] + fb * low_[ch].hp(y[1 - ch] * 0.3f + y[ch] * 0.7f));
                w = softClip(w * drive) / softClip(drive);
                dl_[ch].write(flushDenormal(w));
            }
            float l = y[0], r = y[1];
            applyWidth(l, r, width);
            L[i] = l; R[i] = r;
        }
    }
private:
    float sr_;
    DelayLine dl_[2];
    Biquad pre_[2], post_[2];
    OnePole low_[2];
    SineLfo lfo_;
    Rng rng_{0xBBDu};
    float cur_ = 0.f;
    bool primed_ = false;
};

} // namespace

std::unique_ptr<Effect> makeDelayFamily(uint32_t type, float sr) {
    switch (type) {
        case FxStereoDelay: return std::make_unique<StereoDelay>(sr);
        case FxPingPong: return std::make_unique<PingPong>(sr);
        case FxTapeEcho: return std::make_unique<TapeEcho>(sr);
        case FxBBD: return std::make_unique<BBDDelay>(sr);
        default: return nullptr;
    }
}

} // namespace vektur::fx
