// Character.cpp — Tape (saturation, bias, head bump, HF loss, wow/flutter,
// hiss), Lo-Fi (bit depth, sample-rate reduction with jitter), Amp
// (1–3 anti-aliased gain stages, tone stack, presence, sag) and Cabinet
// (speaker/box/microphone response as a filter model, zero latency).
#include "EffectsInternal.hpp"

namespace vektur::fx {
namespace {

// -------------------------------------------------------------------------
class Tape final : public Effect {
public:
    explicit Tape(float sr) : sr_(sr), base_(std::round(0.005f * sr)) {
        for (int c = 0; c < 2; ++c) dl_[c].allocate(static_cast<uint32_t>(base_ * 2.f) + 8);
    }
    uint32_t latency() const noexcept override { return static_cast<uint32_t>(base_); }
    void reset() noexcept override { for (int c = 0; c < 2; ++c) { dl_[c].reset(); bump_[c].reset(); hf_[c].reset(); hf2_[c].reset(); dc_[c].reset(); sat_[c].reset(); } }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        static constexpr float kBumpHz[3] = {55.f, 85.f, 130.f}, kHfHz[3] = {9000.f, 15000.f, 19000.f};
        const int speed = std::clamp(static_cast<int>(p.v[2]), 0, 2);
        const float g = dbToGain(p.v[0]), bias = p.v[1] * 0.01f * 0.5f;
        const float wow = p.v[3] * 0.01f, flutter = p.v[4] * 0.01f, hiss = p.v[5] * 0.01f;
        const float hfHz = kHfHz[speed] * (1.f - 0.7f * p.v[7] * 0.01f);
        const float comp = p.v[8] >= 0.5f ? 1.f / std::pow(g, 0.7f) : 1.f;
        for (int c = 0; c < 2; ++c) {
            bump_[c].set(Biquad::Peak, kBumpHz[speed], 1.2f, 4.f * p.v[6] * 0.01f, sr_);
            hf_[c].set(Biquad::LowPass, hfHz, 0.6f, 0.f, sr_); hf2_[c].set(Biquad::LowPass, std::min(hfHz * 1.3f, 0.45f * sr_), 0.6f, 0.f, sr_);
            dc_[c].set(12.f, sr_);
        }
        const float offset = std::tanh(bias);
        float* io[2] = {L, R};
        for (uint32_t i = 0; i < n; ++i) {
            const float mod = base_ * (0.35f * wow * (0.7f * lfoW_.next(0.7f, sr_) + 0.3f * lfoW2_.next(1.9f, sr_)) + 0.04f * flutter * lfoF_.next(11.f, sr_));
            for (int c = 0; c < 2; ++c) {
                float x = io[c][i] * g;
                x = sat_[c].process(x + bias) - offset;     // asymmetric (bias) saturation, ADAA
                x = dc_[c].hp(x);
                x = bump_[c].process(x);
                x = hf2_[c].process(hf_[c].process(x));
                dl_[c].write(x);
                float y = dl_[c].readCubic(base_ + mod);
                y += hiss * 0.004f * rng_.bip();
                io[c][i] = y * comp;
            }
        }
    }
private:
    float sr_, base_;
    DelayLine dl_[2];
    Biquad bump_[2], hf_[2], hf2_[2];
    OnePole dc_[2];
    AdaaTanh sat_[2];
    SineLfo lfoW_, lfoW2_, lfoF_;
    Rng rng_{0x7A9Fu};
};

// -------------------------------------------------------------------------
class LoFi final : public Effect {
public:
    explicit LoFi(float sr) : sr_(sr) {}
    void reset() noexcept override { phase_ = 1.f; held_[0] = held_[1] = 0.f; for (int c = 0; c < 2; ++c) { aa_[c][0].reset(); aa_[c][1].reset(); } }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float levels = std::pow(2.f, std::max(1.f, p.v[0]) - 1.f);
        const float target = std::min(p.v[1], sr_);
        const float jitter = p.v[2] * 0.01f, noise = p.v[3] * 0.01f;
        const bool aa = p.v[4] >= 0.5f && target < 0.45f * sr_;
        const float drive = 1.f + 4.f * p.v[5] * 0.01f;
        for (int c = 0; c < 2; ++c) { aa_[c][0].set(Biquad::LowPass, 0.45f * target, 0.54f, 0.f, sr_); aa_[c][1].set(Biquad::LowPass, 0.45f * target, 1.31f, 0.f, sr_); }
        float* io[2] = {L, R};
        const float step = target / sr_;
        for (uint32_t i = 0; i < n; ++i) {
            float x[2] = {io[0][i], io[1][i]};
            for (int c = 0; c < 2; ++c) {
                if (drive > 1.f) x[c] = softClip(x[c] * drive) / softClip(drive);
                if (aa) x[c] = aa_[c][1].process(aa_[c][0].process(x[c]));
            }
            phase_ += step * (1.f + jitter * 0.5f * rng_.bip());
            if (phase_ >= 1.f) {
                phase_ -= std::floor(phase_);
                for (int c = 0; c < 2; ++c) held_[c] = std::round(clampf(x[c], -1.f, 1.f) * levels) / levels;
            }
            for (int c = 0; c < 2; ++c) io[c][i] = held_[c] + noise * 0.02f * rng_.bip();
        }
    }
private:
    float sr_;
    float phase_ = 1.f, held_[2]{};
    Biquad aa_[2][2];
    Rng rng_{0x10F1u};
};

// -------------------------------------------------------------------------
class Amp final : public Effect {
public:
    explicit Amp(float sr) : sr_(sr) { sagAtt_ = tauCoef(5.f, sr); sagRel_ = tauCoef(200.f, sr); }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) { for (auto& s : stage_[c]) s.reset(); for (auto& d : dc_[c]) d.reset(); for (auto& b : tone_[c]) b.reset(); bright_[c].reset(); inter_[c].reset(); }
        sag_.env = 0.f;
    }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        struct Ch { int stages; float maxGainDb; float asym; float hard; };
        static constexpr Ch kCh[4] = {{1, 18.f, 0.05f, 0.f}, {2, 36.f, 0.15f, 0.f}, {3, 52.f, 0.2f, 0.f}, {2, 60.f, 0.35f, 1.f}};
        const Ch& ch = kCh[std::clamp(static_cast<int>(p.v[0]), 0, 3)];
        const float gain = dbToGain(ch.maxGainDb * p.v[1] * 0.01f);
        const float perStage = std::pow(gain, 1.f / static_cast<float>(ch.stages));
        const float master = dbToGain(p.v[6]);
        const float sagAmt = p.v[7] * 0.01f;
        for (int c = 0; c < 2; ++c) {
            bright_[c].set(Biquad::HighShelf, 2000.f, 0.707f, p.v[8] >= 0.5f ? 6.f : 0.f, sr_);
            inter_[c].set(Biquad::HighPass, 120.f, 0.707f, 0.f, sr_);
            tone_[c][0].set(Biquad::LowShelf, 120.f, 0.707f, p.v[2], sr_);
            tone_[c][1].set(Biquad::Peak, 700.f, 0.8f, p.v[3] - 3.f, sr_);   // the classic mid dip at noon
            tone_[c][2].set(Biquad::HighShelf, 3000.f, 0.707f, p.v[4], sr_);
            tone_[c][3].set(Biquad::Peak, 5000.f, 0.9f, p.v[5], sr_);
            tone_[c][4].set(Biquad::LowPass, 7500.f, 0.707f, 0.f, sr_);
            for (auto& d : dc_[c]) d.set(10.f, sr_);
        }
        float* io[2] = {L, R};
        for (uint32_t i = 0; i < n; ++i) {
            const float load = sag_.process(0.5f * (std::fabs(io[0][i]) + std::fabs(io[1][i])) * gain, sagAtt_, sagRel_);
            const float sag = 1.f / (1.f + sagAmt * 0.5f * load);
            for (int c = 0; c < 2; ++c) {
                float x = bright_[c].process(io[c][i]);
                for (int s = 0; s < ch.stages; ++s) {
                    x *= perStage * sag;
                    if (ch.hard > 0.f) x = clampf(x * 1.5f, -1.f, 1.f) * 0.8f + 0.2f * stage_[c][s].process(x + ch.asym);
                    else x = stage_[c][s].process(x + ch.asym) - std::tanh(ch.asym);
                    x = dc_[c][s].hp(x);
                    if (s == 0 && ch.stages > 1) x = inter_[c].process(x);
                }
                for (auto& b : tone_[c]) x = b.process(x);
                io[c][i] = x * master;
            }
        }
    }
private:
    float sr_;
    AdaaTanh stage_[2][3];
    OnePole dc_[2][3];
    Biquad tone_[2][5], bright_[2], inter_[2];
    EnvFollower sag_;
    float sagAtt_ = 0.f, sagRel_ = 0.f;
};

// -------------------------------------------------------------------------
class Cabinet final : public Effect {
public:
    explicit Cabinet(float sr) : sr_(sr) { for (int c = 0; c < 2; ++c) room_[c].allocate(static_cast<uint32_t>(0.004f * sr) + 8); }
    void reset() noexcept override { for (int c = 0; c < 2; ++c) { for (auto& b : f_[c]) b.reset(); room_[c].reset(); } }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        struct Model { float resHz, resQ, scoopHz, presHz, lpHz; };
        static constexpr Model kModels[4] = {{95.f, 1.6f, 500.f, 2400.f, 5500.f}, {85.f, 1.8f, 450.f, 2600.f, 5200.f},
                                             {105.f, 2.4f, 400.f, 2900.f, 4800.f}, {160.f, 1.4f, 700.f, 3200.f, 6000.f}};
        const Model& m = kModels[std::clamp(static_cast<int>(p.v[0]), 0, 3)];
        const int mic = std::clamp(static_cast<int>(p.v[1]), 0, 2);
        const float pos = p.v[2] * 0.01f, dist = p.v[3] * 0.01f, res = p.v[6] * 0.01f;
        const float lp = std::min(p.v[5], m.lpHz * (1.f - 0.45f * pos) * (mic == 1 ? 1.3f : (mic == 2 ? 0.75f : 1.f)));
        for (int c = 0; c < 2; ++c) {
            f_[c][0].set(Biquad::HighPass, std::max(p.v[4], m.resHz * 0.6f), 0.707f, 0.f, sr_);
            f_[c][1].set(Biquad::Peak, m.resHz, m.resQ, 2.f + 6.f * res - 3.f * dist, sr_);
            f_[c][2].set(Biquad::Peak, m.scoopHz, 1.f, -4.f, sr_);
            f_[c][3].set(Biquad::Peak, m.presHz, 1.6f, mic == 0 ? 5.f : (mic == 1 ? 3.f : 1.f), sr_);
            f_[c][4].set(Biquad::LowPass, lp, 0.9f, 0.f, sr_);
            f_[c][5].set(Biquad::LowPass, std::min(lp * 1.25f, 0.45f * sr_), 0.6f, 0.f, sr_);
            f_[c][6].set(mic == 2 ? Biquad::LowShelf : Biquad::HighShelf, mic == 2 ? 220.f : 8000.f, 0.707f, mic == 2 ? 2.f : (mic == 1 ? 3.f : 0.f), sr_);
        }
        const float refl = (0.6f + 2.4f * dist) * 0.001f * sr_;
        float* io[2] = {L, R};
        for (int c = 0; c < 2; ++c)
            for (uint32_t i = 0; i < n; ++i) {
                float x = io[c][i];
                for (auto& b : f_[c]) x = b.process(x);
                room_[c].write(x);
                io[c][i] = x + 0.35f * dist * room_[c].readLinear(refl * (c ? 1.13f : 1.f));
            }
    }
private:
    float sr_;
    Biquad f_[2][7];
    DelayLine room_[2];
};

} // namespace

std::unique_ptr<Effect> makeCharacterFamily(uint32_t type, float sr) {
    switch (type) {
        case FxTape: return std::make_unique<Tape>(sr);
        case FxLoFi: return std::make_unique<LoFi>(sr);
        case FxAmp: return std::make_unique<Amp>(sr);
        case FxCabinet: return std::make_unique<Cabinet>(sr);
        default: return nullptr;
    }
}

} // namespace vektur::fx
