// Modulation.cpp — Chorus (1–3 voices), Flanger, Phaser (2–12 stages),
// Ensemble (three-phase string ensemble), Tremolo / Auto-Pan / Harmonic
// tremolo. Rates can lock to the clock (phase follows the song position
// while the host plays).
#include "EffectsInternal.hpp"

namespace vektur::fx {
namespace {

// Synced or free LFO phase source.
struct ClockedLfo {
    double phase = 0.0;
    // Returns the phase increment per sample; snaps to the song position when synced and playing.
    double begin(float hz, int sync, const FxContext& c, float sr) noexcept {
        if (sync > 0) {
            const double beats = kSyncBeats[std::clamp(sync, 1, 15)];
            if (c.playing) { phase = c.beat / beats; phase -= std::floor(phase); }
            return c.tempo / 60.0 / beats / sr;
        }
        return static_cast<double>(hz) / sr;
    }
    float sine(double offset) const noexcept { return std::sin(kTwoPi * static_cast<float>(phase + offset)); }
    void step(double inc) noexcept { phase += inc; phase -= std::floor(phase); }
};

// -------------------------------------------------------------------------
class Chorus final : public Effect {
public:
    explicit Chorus(float sr) : sr_(sr) { dl_[0].allocate(static_cast<uint32_t>(0.05f * sr)); dl_[1].allocate(static_cast<uint32_t>(0.05f * sr)); }
    void reset() noexcept override { dl_[0].reset(); dl_[1].reset(); tone_[0].reset(); tone_[1].reset(); fb_[0] = fb_[1] = 0.f; }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const int voices = std::clamp(static_cast<int>(p.v[0]) + 1, 1, 3);
        const float base = p.v[3] * 0.001f * sr_, depth = p.v[2] * 0.01f * base * 0.6f, fb = p.v[4] * 0.01f, width = p.v[5] * 0.01f;
        tone_[0].set(2000.f + 14000.f * p.v[6] * 0.01f, sr_); tone_[1].set(2000.f + 14000.f * p.v[6] * 0.01f, sr_);
        const double inc = static_cast<double>(p.v[1]) / sr_;
        float* io[2] = {L, R};
        for (uint32_t i = 0; i < n; ++i) {
            float y[2] = {0.f, 0.f};
            for (int c = 0; c < 2; ++c) {
                for (int v = 0; v < voices; ++v) {
                    const double off = static_cast<double>(v) / voices + (c ? 0.25 : 0.0);
                    const float m = std::sin(kTwoPi * static_cast<float>(phase_ + off));
                    y[c] += dl_[c].readCubic(std::max(2.f, base + depth * m));
                }
                y[c] = tone_[c].lp(y[c] / static_cast<float>(voices));
                dl_[c].write(flushDenormal(io[c][i] + fb * fb_[c]));
                fb_[c] = y[c];
            }
            phase_ += inc; phase_ -= std::floor(phase_);
            applyWidth(y[0], y[1], width);
            L[i] = y[0]; R[i] = y[1];
        }
    }
private:
    float sr_;
    DelayLine dl_[2];
    OnePole tone_[2];
    double phase_ = 0.0;
    float fb_[2]{};
};

// -------------------------------------------------------------------------
class Flanger final : public Effect {
public:
    explicit Flanger(float sr) : sr_(sr) { dl_[0].allocate(static_cast<uint32_t>(0.025f * sr)); dl_[1].allocate(static_cast<uint32_t>(0.025f * sr)); }
    void reset() noexcept override { dl_[0].reset(); dl_[1].reset(); fb_[0] = fb_[1] = 0.f; }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext& c) noexcept override {
        const double inc = lfo_.begin(p.v[0], static_cast<int>(p.v[1]), c, sr_);
        const float manual = p.v[3] * 0.001f * sr_, depth = p.v[2] * 0.01f, fb = p.v[4] * 0.01f;
        const double stereo = p.v[5] / 360.0;
        float* io[2] = {L, R};
        for (uint32_t i = 0; i < n; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                const float m = 0.5f + 0.5f * lfo_.sine(ch ? stereo : 0.0);   // 0…1
                const float d = 2.f + manual * (1.f - depth * 0.95f * m);   // sweeps down from Manual
                const float y = dl_[ch].readCubic(d);
                dl_[ch].write(flushDenormal(io[ch][i] + fb * fb_[ch]));
                fb_[ch] = y;
                io[ch][i] = y;
            }
            lfo_.step(inc);
        }
    }
private:
    float sr_;
    DelayLine dl_[2];
    ClockedLfo lfo_;
    float fb_[2]{};
};

// -------------------------------------------------------------------------
class Phaser final : public Effect {
public:
    explicit Phaser(float sr) : sr_(sr) {}
    void reset() noexcept override { for (auto& c : ap_) for (auto& a : c) a.reset(); fb_[0] = fb_[1] = 0.f; }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext& c) noexcept override {
        static constexpr int kStages[5] = {2, 4, 6, 8, 12};
        const int stages = kStages[std::clamp(static_cast<int>(p.v[0]), 0, 4)];
        const double inc = lfo_.begin(p.v[1], static_cast<int>(p.v[2]), c, sr_);
        const float depth = p.v[3] * 0.01f, centre = p.v[4], fb = p.v[5] * 0.01f;
        const double stereo = p.v[6] / 360.0;
        float* io[2] = {L, R};
        for (uint32_t i = 0; i < n; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                const float f = clampf(centre * std::pow(2.f, 2.f * depth * lfo_.sine(ch ? stereo : 0.0)), 20.f, 0.45f * sr_);
                const float t = std::tan(kPi * f / sr_);
                const float a = (t - 1.f) / (t + 1.f);
                float x = io[ch][i] + fb * fb_[ch];
                for (int s = 0; s < stages; ++s) x = ap_[ch][s].process(x, a);
                fb_[ch] = flushDenormal(x);
                io[ch][i] = x;
            }
            lfo_.step(inc);
        }
    }
private:
    float sr_;
    AllPass1 ap_[2][12];
    ClockedLfo lfo_;
    float fb_[2]{};
};

// -------------------------------------------------------------------------
class Ensemble final : public Effect {
public:
    explicit Ensemble(float sr) : sr_(sr) { dl_[0].allocate(static_cast<uint32_t>(0.03f * sr)); dl_[1].allocate(static_cast<uint32_t>(0.03f * sr)); }
    void reset() noexcept override { dl_[0].reset(); dl_[1].reset(); tone_[0].reset(); tone_[1].reset(); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float base = 0.007f * sr_, depth = p.v[2] * 0.01f;
        const float slow = 0.0025f * sr_ * depth, fast = 0.0003f * sr_ * depth, width = p.v[3] * 0.01f;
        tone_[0].set(3000.f + 12000.f * p.v[4] * 0.01f, sr_); tone_[1].set(3000.f + 12000.f * p.v[4] * 0.01f, sr_);
        const double incS = static_cast<double>(p.v[0]) / sr_, incF = static_cast<double>(p.v[1]) / sr_;
        float* io[2] = {L, R};
        for (uint32_t i = 0; i < n; ++i) {
            const float mono = 0.5f * (io[0][i] + io[1][i]);
            float y[2] = {0.f, 0.f};
            dl_[0].write(mono); dl_[1].write(mono);
            for (int tap = 0; tap < 3; ++tap) {
                const double off = tap / 3.0;
                for (int ch = 0; ch < 2; ++ch) {
                    const double o = off + (ch ? 0.5 / 3.0 : 0.0);
                    const float d = base + slow * std::sin(kTwoPi * static_cast<float>(ps_ + o)) + fast * std::sin(kTwoPi * static_cast<float>(pf_ + o));
                    y[ch] += dl_[ch].readCubic(d);
                }
            }
            ps_ += incS; ps_ -= std::floor(ps_); pf_ += incF; pf_ -= std::floor(pf_);
            float l = tone_[0].lp(y[0] / 3.f), r = tone_[1].lp(y[1] / 3.f);
            applyWidth(l, r, width);
            L[i] = l; R[i] = r;
        }
    }
private:
    float sr_;
    DelayLine dl_[2];
    OnePole tone_[2];
    double ps_ = 0.0, pf_ = 0.0;
};

// -------------------------------------------------------------------------
class Tremolo final : public Effect {
public:
    explicit Tremolo(float sr) : sr_(sr) {}
    void reset() noexcept override { sm_[0] = sm_[1] = 1.f; split_[0].reset(); split_[1].reset(); }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext& c) noexcept override {
        const int mode = static_cast<int>(p.v[0]), shape = static_cast<int>(p.v[4]);
        const double inc = lfo_.begin(p.v[1], static_cast<int>(p.v[2]), c, sr_);
        const float depth = p.v[3] * 0.01f;
        const double stereo = p.v[5] / 360.0;
        const float smooth = 1.f - tauCoef(p.v[6], sr_);
        split_[0].set(800.f, sr_); split_[1].set(800.f, sr_);
        auto wave = [&](double ph) {
            ph -= std::floor(ph);
            switch (shape) {
                case 1: return static_cast<float>(ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
                case 2: return ph < 0.5 ? 1.f : -1.f;
                case 3: return static_cast<float>(1.0 - 2.0 * ph);
                default: return std::sin(kTwoPi * static_cast<float>(ph));
            }
        };
        float* io[2] = {L, R};
        for (uint32_t i = 0; i < n; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                double ph = lfo_.phase + (ch ? stereo : 0.0);
                if (mode == 1 && ch == 1) ph += 0.5;              // auto-pan: opposite gains
                const float w = wave(ph);                         // −1…1
                sm_[ch] += (w - sm_[ch]) * smooth;
                const float g = 1.f - depth * 0.5f * (1.f - sm_[ch]);
                if (mode == 2) {                                  // harmonic: lows and highs in opposition
                    const float lo = split_[ch].lp(io[ch][i]), hi = io[ch][i] - lo;
                    const float g2 = 1.f - depth * 0.5f * (1.f + sm_[ch]);
                    io[ch][i] = lo * g + hi * g2;
                } else io[ch][i] *= g;
            }
            lfo_.step(inc);
        }
    }
private:
    float sr_;
    ClockedLfo lfo_;
    float sm_[2]{1.f, 1.f};
    OnePole split_[2];
};

} // namespace

std::unique_ptr<Effect> makeModulationFamily(uint32_t type, float sr) {
    switch (type) {
        case FxChorus: return std::make_unique<Chorus>(sr);
        case FxFlanger: return std::make_unique<Flanger>(sr);
        case FxPhaser: return std::make_unique<Phaser>(sr);
        case FxEnsemble: return std::make_unique<Ensemble>(sr);
        case FxTremolo: return std::make_unique<Tremolo>(sr);
        default: return nullptr;
    }
}

} // namespace vektur::fx
