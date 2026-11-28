// Convolution.cpp — uniformly partitioned overlap-save convolution (block
// B = 256, FFT 512, frequency-domain delay line), stereo IR, latency B.
// Built-in IRs are synthesised deterministically for the session sample rate.
#include "EffectsInternal.hpp"
#include "FFT.hpp"

namespace vektur::fx {

namespace {
constexpr uint32_t kBlock = 256, kFft = 512, kBins = kFft / 2 + 1;
constexpr float kMaxIrSeconds = 3.0f;
}

// --------------------------------------------------------------------------
// Built-in impulse responses: band-split decaying noise (three bands with
// their own RT60), sparse early reflections, decorrelated L/R; the cabinet is
// a short filtered impulse with a few box reflections.
// --------------------------------------------------------------------------
ImpulseResponse builtInImpulse(uint32_t index, float sr) {
    struct Room { float rtLow, rtMid, rtHigh, length, erDensity, preMs; };
    static constexpr Room kRooms[7] = {
        {0.45f, 0.40f, 0.25f, 0.6f, 14.f, 2.f},   // small room
        {0.6f, 0.55f, 0.35f, 0.9f, 10.f, 4.f},    // studio
        {1.4f, 1.2f, 0.8f, 1.8f, 8.f, 8.f},       // chamber
        {2.6f, 2.2f, 1.4f, 3.0f, 6.f, 18.f},      // concert hall
        {4.5f, 3.8f, 2.2f, 3.0f, 4.f, 30.f},      // cathedral (truncated to 3 s)
        {1.9f, 2.0f, 1.8f, 2.6f, 30.f, 0.5f},     // plate (dense, bright)
        {2.2f, 1.8f, 0.9f, 2.4f, 2.f, 0.f},       // spring tank
    };
    ImpulseResponse ir; ir.sampleRate = sr;
    if (index == 7) {   // 4×12 cabinet: ~25 ms
        const auto n = static_cast<size_t>(0.025f * sr);
        ir.left.assign(n, 0.f); ir.right.assign(n, 0.f);
        Biquad hp, peak, scoop, pres, lp1, lp2;
        hp.set(Biquad::HighPass, 75.f, 0.8f, 0.f, sr); peak.set(Biquad::Peak, 110.f, 1.4f, 5.f, sr);
        scoop.set(Biquad::Peak, 450.f, 1.f, -4.f, sr); pres.set(Biquad::Peak, 2600.f, 1.5f, 5.f, sr);
        lp1.set(Biquad::LowPass, 4800.f, 0.9f, 0.f, sr); lp2.set(Biquad::LowPass, 5600.f, 0.7f, 0.f, sr);
        for (size_t i = 0; i < n; ++i) {
            float x = i == 0 ? 1.f : 0.f;
            // box reflections at 1.3, 2.9, 4.4 ms
            for (float ms : {1.3f, 2.9f, 4.4f}) if (i == static_cast<size_t>(ms * 0.001f * sr)) x += -0.3f;
            x = lp2.process(lp1.process(pres.process(scoop.process(peak.process(hp.process(x))))));
            ir.left[i] = x; ir.right[i] = x;
        }
    } else {
        const Room& r = kRooms[std::min<uint32_t>(index, 6)];
        const auto n = static_cast<size_t>(std::min(r.length, kMaxIrSeconds) * sr);
        ir.left.assign(n, 0.f); ir.right.assign(n, 0.f);
        Rng rng(0x1234u + index * 977u);
        OnePole lo[2], hi[2];
        for (int c = 0; c < 2; ++c) { lo[c].set(300.f, sr); hi[c].set(3000.f, sr); }
        const auto pre = static_cast<size_t>(r.preMs * 0.001f * sr);
        for (int c = 0; c < 2; ++c) {
            std::vector<float>& out = c ? ir.right : ir.left;
            for (size_t i = pre; i < n; ++i) {
                const float t = static_cast<float>(i - pre) / sr;
                const float noise = rng.bip();
                const float l = lo[c].lp(noise), h = noise - hi[c].lp(noise), m = noise - l - h;
                const float el = std::pow(10.f, -3.f * t / r.rtLow), em = std::pow(10.f, -3.f * t / r.rtMid), eh = std::pow(10.f, -3.f * t / r.rtHigh);
                const float onset = std::min(1.f, t / 0.01f);   // late field builds up over 10 ms
                out[i] = (l * el + m * em + h * eh) * onset;
            }
            // Early reflections.
            Rng er(0x77u + index * 31u + static_cast<uint32_t>(c));
            const int count = static_cast<int>(r.erDensity * 3.f);
            for (int k = 0; k < count; ++k) {
                const float ms = 3.f + 80.f * er.uni() * er.uni();
                const auto at = pre + static_cast<size_t>(ms * 0.001f * sr);
                if (at < n) out[at] += (er.uni() < 0.5f ? -1.f : 1.f) * 0.6f * (1.f - ms / 90.f);
            }
            if (index == 6) {   // spring: dispersive "drip" chirp at the start
                for (size_t i = 0; i < std::min(n, static_cast<size_t>(0.06f * sr)); ++i) {
                    const float t = static_cast<float>(i) / sr;
                    out[i] += 0.5f * std::sin(kTwoPi * (400.f * t + 18000.f * t * t)) * std::exp(-t * 40.f);
                }
            }
        }
    }
    // Normalise to unit energy per channel × 0.5 (perceived level close to a dry note).
    for (auto* ch : {&ir.left, &ir.right}) {
        double e = 0.0; for (float v : *ch) e += static_cast<double>(v) * v;
        const float g = e > 0.0 ? static_cast<float>(0.5 / std::sqrt(e)) : 0.f;
        for (float& v : *ch) v *= g;
    }
    return ir;
}

namespace {

class Convolver final : public Effect {
public:
    Convolver(float sr, const ImpulseResponse& ir) : sr_(sr), fft_(kFft) {
        const size_t len = std::min(std::max(ir.left.size(), ir.right.size()), static_cast<size_t>(kMaxIrSeconds * sr));
        parts_ = std::max<uint32_t>(1, static_cast<uint32_t>((len + kBlock - 1) / kBlock));
        for (int c = 0; c < 2; ++c) {
            hRe_[c].assign(static_cast<size_t>(parts_) * kBins, 0.f); hIm_[c].assign(static_cast<size_t>(parts_) * kBins, 0.f);
            xRe_[c].assign(static_cast<size_t>(parts_) * kBins, 0.f); xIm_[c].assign(static_cast<size_t>(parts_) * kBins, 0.f);
            const std::vector<float>& h = c == 0 ? ir.left : (ir.right.empty() ? ir.left : ir.right);
            std::vector<float> buf(kFft, 0.f);
            for (uint32_t p = 0; p < parts_; ++p) {
                std::fill(buf.begin(), buf.end(), 0.f);
                for (uint32_t i = 0; i < kBlock; ++i) { const size_t k = static_cast<size_t>(p) * kBlock + i; if (k < h.size() && k < len) buf[i] = h[k]; }
                fft_.forward(buf.data(), &hRe_[c][static_cast<size_t>(p) * kBins], &hIm_[c][static_cast<size_t>(p) * kBins]);
            }
            in_[c].assign(kFft, 0.f); out_[c].assign(kBlock, 0.f); acc_[c].assign(kFft, 0.f);
            yRe_[c].assign(kBins, 0.f); yIm_[c].assign(kBins, 0.f);
        }
        pre_[0].allocate(static_cast<uint32_t>(0.26f * sr)); pre_[1].allocate(static_cast<uint32_t>(0.26f * sr));
        irSeconds_ = static_cast<float>(len) / sr;
    }
    uint32_t latency() const noexcept override { return kBlock; }
    float tailSeconds(const FxParams&) const noexcept override { return irSeconds_ + 0.3f; }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) {
            std::fill(in_[c].begin(), in_[c].end(), 0.f); std::fill(out_[c].begin(), out_[c].end(), 0.f);
            std::fill(xRe_[c].begin(), xRe_[c].end(), 0.f); std::fill(xIm_[c].begin(), xIm_[c].end(), 0.f);
            pre_[c].reset(); lo_[c].reset(); hi_[c].reset();
        }
        fill_ = 0; head_ = 0;
    }
    void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext&) noexcept override {
        const float preD = p.v[1] <= 0.11f ? 0.f : p.v[1] * 0.001f * sr_;   // minimum = off
        const float g = dbToGain(p.v[5]);
        const bool lowOn = p.v[2] > 21.f, highOn = p.v[3] < 19900.f;
        for (int c = 0; c < 2; ++c) { lo_[c].set(Biquad::HighPass, p.v[2], 0.707f, 0.f, sr_); hi_[c].set(Biquad::LowPass, p.v[3], 0.707f, 0.f, sr_); }
        for (uint32_t i = 0; i < n; ++i) {
            float x[2] = {L[i], R[i]};
            for (int c = 0; c < 2; ++c) {
                pre_[c].write(x[c]);
                const float v = preD >= 1.f ? pre_[c].readLinear(preD) : x[c];
                in_[c][kBlock + fill_] = v;
                float y = out_[c][fill_] * g;
                if (lowOn) y = lo_[c].process(y);
                if (highOn) y = hi_[c].process(y);
                x[c] = y;
            }
            float l = x[0], r = x[1];
            applyWidth(l, r, p.v[4] * 0.01f);
            L[i] = l; R[i] = r;
            if (++fill_ == kBlock) { fill_ = 0; convolveBlock(); }
        }
    }
private:
    void convolveBlock() noexcept {
        head_ = (head_ + parts_ - 1) % parts_;   // newest spectrum goes to slot head_
        for (int c = 0; c < 2; ++c) {
            float* xr = &xRe_[c][static_cast<size_t>(head_) * kBins];
            float* xi = &xIm_[c][static_cast<size_t>(head_) * kBins];
            fft_.forward(in_[c].data(), xr, xi);
            float* yr = yRe_[c].data(); float* yi = yIm_[c].data();
            std::fill(yr, yr + kBins, 0.f); std::fill(yi, yi + kBins, 0.f);
            for (uint32_t p = 0; p < parts_; ++p) {
                const size_t xs = static_cast<size_t>((head_ + p) % parts_) * kBins, hs = static_cast<size_t>(p) * kBins;
                const float* ar = &xRe_[c][xs]; const float* ai = &xIm_[c][xs];
                const float* br = &hRe_[c][hs]; const float* bi = &hIm_[c][hs];
                for (uint32_t k = 0; k < kBins; ++k) { yr[k] += ar[k] * br[k] - ai[k] * bi[k]; yi[k] += ar[k] * bi[k] + ai[k] * br[k]; }
            }
            fft_.inverse(yr, yi, acc_[c].data());
            for (uint32_t k = 0; k < kBlock; ++k) out_[c][k] = acc_[c][kBlock + k];   // overlap-save: keep the last B
            for (uint32_t k = 0; k < kBlock; ++k) in_[c][k] = in_[c][kBlock + k];      // slide the input window
        }
    }
    float sr_;
    FFT fft_;
    uint32_t parts_ = 1, fill_ = 0, head_ = 0;
    std::vector<float> hRe_[2], hIm_[2], xRe_[2], xIm_[2], in_[2], out_[2], acc_[2], yRe_[2], yIm_[2];
    DelayLine pre_[2];
    Biquad lo_[2], hi_[2];
    float irSeconds_ = 0.f;
};

} // namespace

std::unique_ptr<Effect> makeConvolution(float sr, const ImpulseResponse& ir) { return std::make_unique<Convolver>(sr, ir); }

} // namespace vektur::fx
