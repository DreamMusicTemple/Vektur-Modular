// Spectral.cpp — STFT effect (2048-point, hop 512, √Hann analysis/synthesis)
// with a phase vocoder: freeze, temporal blur, pitch shift, spectral smear,
// tilt, spectral gate and phase scatter. Latency 2048 samples. With every
// control neutral the frames are resynthesised unchanged (identity).
#include "EffectsInternal.hpp"
#include "FFT.hpp"

namespace vektur::fx {
namespace {

constexpr uint32_t kN = 2048, kHop = 512, kBins = kN / 2 + 1;

class Spectral final : public Effect {
public:
    explicit Spectral(float sr) : sr_(sr), fft_(kN) {
        win_.resize(kN);
        for (uint32_t i = 0; i < kN; ++i) win_[i] = std::sqrt(0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / kN));
        frame_.resize(kN); re_.resize(kBins); im_.resize(kBins);
        for (int c = 0; c < 2; ++c) {
            in_[c].assign(kN, 0.f); out_[c].assign(kN, 0.f);
            mag_[c].assign(kBins, 0.f); prevPhase_[c].assign(kBins, 0.f); synPhase_[c].assign(kBins, 0.f);
            frozen_[c].assign(kBins, 0.f); blur_[c].assign(kBins, 0.f); freq_[c].assign(kBins, 0.f);
        }
        tmpMag_.resize(kBins); tmpFreq_.resize(kBins); smear_.resize(kBins);
    }
    uint32_t latency() const noexcept override { return kN; }
    float tailSeconds(const FxParams& p) const noexcept override { return p.v[0] >= 0.5f ? -1.f : static_cast<float>(kN) / sr_ * (1.f + 20.f * p.v[1] * 0.01f); }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) {
            std::fill(in_[c].begin(), in_[c].end(), 0.f); std::fill(out_[c].begin(), out_[c].end(), 0.f);
            std::fill(prevPhase_[c].begin(), prevPhase_[c].end(), 0.f); std::fill(synPhase_[c].begin(), synPhase_[c].end(), 0.f);
            std::fill(blur_[c].begin(), blur_[c].end(), 0.f);
        }
        fill_ = 0; wasFrozen_ = false;
    }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        p_ = p;
        for (uint32_t i = 0; i < n; ++i) {
            in_[0][kN - kHop + fill_] = L[i];
            in_[1][kN - kHop + fill_] = R[i];
            L[i] = out_[0][fill_];
            R[i] = out_[1][fill_];
            if (++fill_ == kHop) { fill_ = 0; frame(); }
        }
    }
private:
    void frame() noexcept {
        const bool freeze = p_.v[0] >= 0.5f;
        const float blur = std::min(0.98f, p_.v[1] * 0.01f);
        const float ratio = std::pow(2.f, p_.v[2] / 12.f);
        const int smear = static_cast<int>(std::lround(p_.v[3] * 0.01f * 8.f));
        const float tilt = p_.v[4];
        const float thr = p_.v[5] > -99.5f ? dbToGain(p_.v[5]) * (kN / 4.f) : 0.f;
        const float scatter = p_.v[6] * 0.01f;
        const bool neutral = !freeze && blur == 0.f && std::fabs(p_.v[2]) < 1e-3f && smear == 0 && tilt == 0.f && thr == 0.f && scatter == 0.f;
        const float expect = kTwoPi * kHop / kN;   // phase advance per hop of bin 1
        for (int c = 0; c < 2; ++c) {
            for (uint32_t i = 0; i < kN; ++i) frame_[i] = in_[c][i] * win_[i];
            fft_.forward(frame_.data(), re_.data(), im_.data());
            if (!neutral) {
                // Analysis: magnitude and true frequency (in bins) per bin.
                for (uint32_t k = 0; k < kBins; ++k) {
                    const float m = std::sqrt(re_[k] * re_[k] + im_[k] * im_[k]);
                    const float ph = std::atan2(im_[k], re_[k]);
                    float d = ph - prevPhase_[c][k] - expect * static_cast<float>(k);
                    prevPhase_[c][k] = ph;
                    d -= kTwoPi * std::round(d / kTwoPi);
                    tmpFreq_[k] = static_cast<float>(k) + d / expect;
                    tmpMag_[k] = m;
                }
                if (freeze) {
                    if (!wasFrozenCh_[c]) { for (uint32_t k = 0; k < kBins; ++k) { frozen_[c][k] = tmpMag_[k]; freq_[c][k] = tmpFreq_[k]; } }
                    for (uint32_t k = 0; k < kBins; ++k) { tmpMag_[k] = frozen_[c][k]; tmpFreq_[k] = freq_[c][k]; }
                }
                wasFrozenCh_[c] = freeze;
                for (uint32_t k = 0; k < kBins; ++k) { blur_[c][k] = blur * blur_[c][k] + (1.f - blur) * tmpMag_[k]; tmpMag_[k] = blur_[c][k]; }
                if (smear > 0) {
                    for (uint32_t k = 0; k < kBins; ++k) {
                        float s = 0.f; int cnt = 0;
                        for (int d = -smear; d <= smear; ++d) { const int j = static_cast<int>(k) + d; if (j >= 0 && j < static_cast<int>(kBins)) { s += tmpMag_[static_cast<uint32_t>(j)]; ++cnt; } }
                        smear_[k] = s / static_cast<float>(cnt);
                    }
                    for (uint32_t k = 0; k < kBins; ++k) tmpMag_[k] = smear_[k];
                }
                // Resynthesis with pitch shift (bins move by `ratio`).
                for (uint32_t k = 0; k < kBins; ++k) { mag_[c][k] = 0.f; smear_[k] = 0.f; }
                for (uint32_t k = 0; k < kBins; ++k) {
                    const auto t = static_cast<uint32_t>(std::lround(static_cast<float>(k) * ratio));
                    if (t >= kBins) break;
                    if (tmpMag_[k] > mag_[c][t]) { mag_[c][t] = tmpMag_[k]; smear_[t] = tmpFreq_[k] * ratio; }
                }
                for (uint32_t k = 1; k < kBins; ++k) {
                    float m = mag_[c][k];
                    if (tilt != 0.f) m *= std::pow(10.f, tilt * std::log2(std::max(1.f, static_cast<float>(k) * sr_ / kN) / 1000.f) / 20.f);
                    if (m < thr) m = 0.f;
                    synPhase_[c][k] += expect * (smear_[k] != 0.f ? smear_[k] : static_cast<float>(k));
                    synPhase_[c][k] -= kTwoPi * std::floor(synPhase_[c][k] / kTwoPi);
                    const float ph = synPhase_[c][k] + (scatter > 0.f ? scatter * kPi * rng_.bip() : 0.f);
                    re_[k] = m * std::cos(ph); im_[k] = m * std::sin(ph);
                }
                re_[0] = 0.f; im_[0] = 0.f; im_[kBins - 1] = 0.f;
            } else {
                // Keep the vocoder state current so engaging a control is smooth.
                for (uint32_t k = 0; k < kBins; ++k) { const float ph = std::atan2(im_[k], re_[k]); prevPhase_[c][k] = ph; synPhase_[c][k] = ph; }
                wasFrozenCh_[c] = false;
            }
            fft_.inverse(re_.data(), im_.data(), frame_.data());
            // Overlap-add (√Hann² at 75 % overlap sums to 2).
            for (uint32_t i = 0; i < kN - kHop; ++i) out_[c][i] = out_[c][i + kHop];
            for (uint32_t i = kN - kHop; i < kN; ++i) out_[c][i] = 0.f;
            for (uint32_t i = 0; i < kN; ++i) out_[c][i] += frame_[i] * win_[i] * 0.5f;
            for (uint32_t i = 0; i < kN - kHop; ++i) in_[c][i] = in_[c][i + kHop];
        }
        // out_[0..hop) now holds finished samples: shift so reads start at 0.
        wasFrozen_ = freeze;
    }

    float sr_;
    FFT fft_;
    FxParams p_{};
    std::vector<float> win_, frame_, re_, im_, tmpMag_, tmpFreq_, smear_;
    std::vector<float> in_[2], out_[2], mag_[2], prevPhase_[2], synPhase_[2], frozen_[2], blur_[2], freq_[2];
    uint32_t fill_ = 0;
    bool wasFrozen_ = false, wasFrozenCh_[2]{false, false};
    Rng rng_{0x5EC7u};
};

} // namespace

std::unique_ptr<Effect> makeSpectral(float sr) { return std::make_unique<Spectral>(sr); }

} // namespace vektur::fx
