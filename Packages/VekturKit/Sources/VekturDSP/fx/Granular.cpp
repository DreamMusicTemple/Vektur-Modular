// Granular.cpp — grain cloud over a 2.5 s stereo capture buffer: size,
// density, pitch (with random spread and optional octave/fifth quantise),
// position and spray, reverse probability, freeze, feedback, stereo spread
// and three grain envelopes. Up to 64 overlapping grains.
#include "EffectsInternal.hpp"

namespace vektur::fx {
namespace {

class Granular final : public Effect {
public:
    explicit Granular(float sr) : sr_(sr) {
        const auto n = static_cast<uint32_t>(2.6f * sr);
        buf_[0].allocate(n); buf_[1].allocate(n);
    }
    void reset() noexcept override { buf_[0].reset(); buf_[1].reset(); for (auto& g : grains_) g.active = false; countdown_ = 0.f; fbL_ = fbR_ = 0.f; }
    float tailSeconds(const FxParams& p) const noexcept override {
        if (p.v[7] >= 0.5f) return -1.f;
        return (p.v[0] + p.v[4] + p.v[5]) * 0.001f + feedbackTail(p.v[0] * 0.001f, p.v[8] * 0.01f) + 0.1f;
    }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float size = std::max(10.f, p.v[0]) * 0.001f * sr_;
        const float density = std::max(0.5f, p.v[1]);
        const bool freeze = p.v[7] >= 0.5f;
        const float fb = p.v[8] * 0.01f;
        const float overlap = std::max(1.f, density * size / sr_);
        const float norm = 1.f / std::sqrt(overlap);
        for (uint32_t i = 0; i < n; ++i) {
            // Freeze stops the write head: grains keep reading the captured audio.
            if (!freeze) { buf_[0].write(flushDenormal(L[i] + fb * fbL_)); buf_[1].write(flushDenormal(R[i] + fb * fbR_)); }
            frozen_ = freeze;
            countdown_ -= 1.f;
            if (countdown_ <= 0.f) { spawn(p, size); countdown_ += sr_ / density * (0.75f + 0.5f * rng_.uni()); }
            float l = 0.f, r = 0.f;
            for (auto& g : grains_) {
                if (!g.active) continue;
                const float t = g.age / g.length;
                float env;
                switch (envShape_) {
                    case 1: env = 1.f - std::fabs(2.f * t - 1.f); break;
                    case 2: env = std::min(1.f, std::min(t, 1.f - t) * 5.f); break;
                    default: env = 0.5f - 0.5f * std::cos(kTwoPi * t); break;
                }
                // Read position: delay behind the write head (grows as the grain plays slower than real time).
                const float d = g.delay - g.age * (freeze ? g.rate : g.rate - 1.f);
                const float sl = buf_[0].readLinear(std::max(1.f, d)), sr = buf_[1].readLinear(std::max(1.f, d));
                l += sl * env * std::min(1.f, 2.f * (1.f - g.pan));
                r += sr * env * std::min(1.f, 2.f * g.pan);
                g.age += 1.f;
                if (g.age >= g.length) g.active = false;
            }
            fbL_ = l * norm; fbR_ = r * norm;
            L[i] = fbL_; R[i] = fbR_;
        }
    }
private:
    struct Grain { bool active = false; float age = 0.f, length = 1.f, delay = 1.f, rate = 1.f, pan = 0.5f; };
    void spawn(const FxParams& p, float size) noexcept {
        envShape_ = static_cast<int>(p.v[10]);
        for (auto& g : grains_) {
            if (g.active) continue;
            float semis = p.v[2] + p.v[3] * rng_.bip();
            const int q = static_cast<int>(p.v[11]);
            if (q == 1) semis = 12.f * std::round(semis / 12.f);
            else if (q == 2) { const float c[] = {-24, -19, -17, -12, -7, -5, 0, 5, 7, 12, 17, 19, 24}; float best = 0.f, bd = 99.f; for (float v : c) if (std::fabs(v - semis) < bd) { bd = std::fabs(v - semis); best = v; } semis = best; }
            float rate = std::pow(2.f, semis / 12.f);
            if (rng_.uni() < p.v[6] * 0.01f) rate = -rate;
            g.active = true; g.age = 0.f; g.length = size; g.rate = rate;
            const float spray = p.v[5] * 0.001f * sr_ * rng_.uni();
            float delay = p.v[4] * 0.001f * sr_ + spray + 2.f;
            // Keep the whole grain behind the write head and inside the buffer.
            const float ahead = size * (frozen_ ? rate : rate - 1.f);
            delay = std::max(delay, ahead + 4.f);
            delay = std::min(delay, 2.5f * sr_ - std::max(0.f, -ahead) - 4.f);
            g.delay = delay;
            g.pan = 0.5f + 0.5f * p.v[9] * 0.01f * rng_.bip();
            return;
        }
    }
    float sr_;
    DelayLine buf_[2];
    Grain grains_[64];
    Rng rng_{0x6A41u};
    float countdown_ = 0.f, fbL_ = 0.f, fbR_ = 0.f;
    int envShape_ = 0;
    bool frozen_ = false;
};

} // namespace

std::unique_ptr<Effect> makeGranular(float sr) { return std::make_unique<Granular>(sr); }

} // namespace vektur::fx
