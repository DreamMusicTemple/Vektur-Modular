// EQ.cpp — Parametric EQ (HPF, low shelf, two peaks, high shelf, LPF) and a
// 10-band graphic EQ (constant- or proportional-Q peaks). Zero latency.
#include "EffectsInternal.hpp"

namespace vektur::fx {
namespace {

class ParametricEQ final : public Effect {
public:
    explicit ParametricEQ(float sr) : sr_(sr) {}
    void reset() noexcept override { for (auto& ch : f_) for (auto& b : ch) b.reset(); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const bool hpOn = p.v[0] > 10.5f, lpOn = p.v[11] < 21900.f;
        for (int c = 0; c < 2; ++c) {
            f_[c][0].set(Biquad::HighPass, p.v[0], 0.707f, 0.f, sr_);
            f_[c][1].set(Biquad::LowShelf, p.v[1], 0.707f, p.v[2], sr_);
            f_[c][2].set(Biquad::Peak, p.v[3], p.v[5], p.v[4], sr_);
            f_[c][3].set(Biquad::Peak, p.v[6], p.v[8], p.v[7], sr_);
            f_[c][4].set(Biquad::HighShelf, p.v[9], 0.707f, p.v[10], sr_);
            f_[c][5].set(Biquad::LowPass, p.v[11], 0.707f, 0.f, sr_);
        }
        const bool on[6] = {hpOn, p.v[2] != 0.f, p.v[4] != 0.f, p.v[7] != 0.f, p.v[10] != 0.f, lpOn};
        float* io[2] = {L, R};
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < 6; ++b) {
                if (!on[b]) continue;
                Biquad& q = f_[c][b];
                for (uint32_t i = 0; i < n; ++i) io[c][i] = q.process(io[c][i]);
            }
    }
    // For tests and UI curves: combined response of the current settings.
    float responseDb(float hz) const noexcept { float s = 0.f; for (auto& b : f_[0]) s += b.magnitudeDb(hz, sr_); return s; }
private:
    float sr_;
    Biquad f_[2][6];
};

class GraphicEQ final : public Effect {
public:
    explicit GraphicEQ(float sr) : sr_(sr) {}
    void reset() noexcept override { for (auto& ch : f_) for (auto& b : ch) b.reset(); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        static constexpr float kHz[10] = {31.25f, 62.5f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f};
        const bool proportional = p.v[11] >= 0.5f;
        bool on[10];
        for (int b = 0; b < 10; ++b) {
            const float g = p.v[b];
            on[b] = g != 0.f;
            // Proportional Q: narrower for large boosts/cuts (like analogue graphics).
            const float q = proportional ? p.v[10] * (0.6f + 0.4f * std::fabs(g) / 12.f) * 1.4f : p.v[10];
            const float hz = std::min(kHz[b], 0.45f * sr_);
            for (int c = 0; c < 2; ++c) f_[c][b].set(Biquad::Peak, hz, q, g, sr_);
        }
        float* io[2] = {L, R};
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < 10; ++b) {
                if (!on[b]) continue;
                Biquad& q = f_[c][b];
                for (uint32_t i = 0; i < n; ++i) io[c][i] = q.process(io[c][i]);
            }
    }
private:
    float sr_;
    Biquad f_[2][10];
};

} // namespace

std::unique_ptr<Effect> makeEqFamily(uint32_t type, float sr) {
    if (type == FxParamEQ) return std::make_unique<ParametricEQ>(sr);
    if (type == FxGraphicEQ) return std::make_unique<GraphicEQ>(sr);
    return nullptr;
}

} // namespace vektur::fx
