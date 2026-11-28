// Stutter.cpp — tempo-synced beat repeat. At each grid line of the shared
// clock it decides (seeded by the grid index, so it repeats identically on
// every playback) whether to stutter: the slice that starts there repeats
// for Length % of the grid period, optionally reversed, pitch-dropping or
// fading per repeat, or the period becomes a tape stop.
#include "EffectsInternal.hpp"
#include "../seq/SeqCommon.hpp"

namespace vektur::fx {
namespace {

class Stutter final : public Effect {
public:
    explicit Stutter(float sr) : sr_(sr) { buf_[0].allocate(static_cast<uint32_t>(6.f * sr)); buf_[1].allocate(static_cast<uint32_t>(6.f * sr)); }
    void reset() noexcept override { buf_[0].reset(); buf_[1].reset(); active_ = false; lastGrid_ = INT64_MIN; }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext& c) noexcept override {
        static constexpr double kSliceBeats[6] = {1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0};
                const double sliceBeats = kSliceBeats[std::clamp(static_cast<int>(p.v[0]), 0, 5)];
        const double kGridBeats[4] = {1.0, 2.0, c.beatsPerBar, 2.0 * c.beatsPerBar};   // 1/4, 1/2, 1 bar, 2 bars
        const double gridBeats = kGridBeats[std::clamp(static_cast<int>(p.v[2]), 0, 3)];
        const double beatsPerSample = c.tempo / 60.0 / sr_;
        const auto seed = static_cast<uint64_t>(p.v[8]);
        const float fade = 0.002f * sr_;   // 2 ms edges
        for (uint32_t i = 0; i < n; ++i) {
            const double beat = c.beat + static_cast<double>(i) * beatsPerSample;
            const auto grid = static_cast<int64_t>(std::floor(beat / gridBeats + 1e-9));
            buf_[0].write(L[i]); buf_[1].write(R[i]);
            if (grid != lastGrid_) {
                lastGrid_ = grid;
                const auto g = static_cast<uint64_t>(grid);
                active_ = seq::rand01(seed, 0x57, g) < p.v[1] * 0.01;
                tapeStop_ = active_ && seq::rand01(seed, 0x58, g) < p.v[7] * 0.01;
                elapsed_ = 0.0;
                slice_ = std::max(16.0, sliceBeats / beatsPerSample);
                period_ = gridBeats / beatsPerSample * p.v[3] * 0.01;
                readPos_ = 0.0;
                revSeed_ = g;
            }
            if (!active_ || elapsed_ >= period_) { elapsed_ += 1.0; continue; }   // live
            float yl, yr, env = 1.f;
            if (tapeStop_) {
                // Speed falls linearly to zero across the period.
                const double rate = std::max(0.0, 1.0 - elapsed_ / period_);
                readPos_ += rate;
                const float d = static_cast<float>(elapsed_ - readPos_) + 1.f;
                yl = buf_[0].readLinear(d); yr = buf_[1].readLinear(d);
            } else {
                const auto k = static_cast<uint64_t>(elapsed_ / slice_);
                const double j = elapsed_ - static_cast<double>(k) * slice_;
                if (k == 0) { yl = L[i]; yr = R[i]; }
                else {
                    const bool rev = seq::rand01(seed, 0x59, revSeed_, k) < p.v[4] * 0.01;
                    const double rate = std::pow(2.0, -p.v[5] * static_cast<double>(k) / 12.0);
                    double pos = j * rate;
                    if (rev) pos = slice_ - 1.0 - pos;
                    const float d = static_cast<float>(elapsed_ - std::clamp(pos, 0.0, slice_ - 1.0)) + 1.f;
                    yl = buf_[0].readLinear(d); yr = buf_[1].readLinear(d);
                    env = std::pow(1.f - p.v[6] * 0.01f, static_cast<float>(k));
                    const float edge = static_cast<float>(std::min(j, slice_ - j));
                    env *= std::min(1.f, edge / fade);
                }
            }
            // Smooth the exit back to live audio.
            const float tail = static_cast<float>(period_ - elapsed_);
            const float w = std::min(1.f, tail / fade);
            L[i] = lerp(L[i], yl * env, w); R[i] = lerp(R[i], yr * env, w);
            elapsed_ += 1.0;
        }
    }
private:
    float sr_;
    DelayLine buf_[2];
    bool active_ = false, tapeStop_ = false;
    int64_t lastGrid_ = INT64_MIN;
    double elapsed_ = 0.0, slice_ = 1.0, period_ = 0.0, readPos_ = 0.0;
    uint64_t revSeed_ = 0;
};

} // namespace

std::unique_ptr<Effect> makeStutter(float sr) { return std::make_unique<Stutter>(sr); }

} // namespace vektur::fx
