// FFT.hpp — radix-2 FFT for the convolution and spectral effects.
// Tables are built in the constructor (control thread); transforms are
// allocation-free. Real transforms use the half-length complex trick.
//
// Phase 10: on Apple platforms the transforms run on vDSP (Accelerate,
// vDSP_fft_zrip) behind the same interface and scaling; the portable radix-2
// code stays as the Linux / WebAssembly path and the reference that
// test_effects checks against. Define VK_PORTABLE_FFT=1 to force it.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__APPLE__) && !defined(VK_PORTABLE_FFT)
#define VK_FFT_VDSP 1
#include <Accelerate/Accelerate.h>
#endif

namespace vektur::fx {

class FFT {
public:
    explicit FFT(uint32_t n) : n_(n), h_(n / 2) {
        // complex FFT of size h_ on interleaved (re, im)
        rev_.resize(h_);
        uint32_t bits = 0; while ((1u << bits) < h_) ++bits;
        for (uint32_t i = 0; i < h_; ++i) { uint32_t r = 0; for (uint32_t b = 0; b < bits; ++b) if (i & (1u << b)) r |= 1u << (bits - 1 - b); rev_[i] = r; }
        tw_.resize(h_);
        for (uint32_t i = 0; i < h_ / 2 + 1 && i < h_; ++i) {
            const double a = -2.0 * 3.141592653589793 * i / h_;
            tw_[i] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
        }
        post_.resize(h_ + 1);
        for (uint32_t k = 0; k <= h_; ++k) {
            const double a = -2.0 * 3.141592653589793 * k / n_;
            post_[k] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
        }
        work_.resize(n_);
#if VK_FFT_VDSP
        uint32_t lg = 0; while ((1u << lg) < n_) ++lg;
        log2n_ = lg;
        setup_ = vDSP_create_fftsetup(log2n_, kFFTRadix2);
        sr_.resize(h_); si_.resize(h_);
#endif
    }
#if VK_FFT_VDSP
    ~FFT() { if (setup_) vDSP_destroy_fftsetup(setup_); }
    FFT(const FFT&) = delete;
    FFT& operator=(const FFT&) = delete;
#endif
    uint32_t size() const noexcept { return n_; }
    uint32_t bins() const noexcept { return h_ + 1; }

    // Real forward: in[n] → re[h+1], im[h+1].
    void forward(const float* in, float* re, float* im) noexcept {
#if VK_FFT_VDSP
        if (setup_) {
            // zrip packs DC in realp[0] and Nyquist in imagp[0]; its output is 2·X.
            DSPSplitComplex z{sr_.data(), si_.data()};
            vDSP_ctoz(reinterpret_cast<const DSPComplex*>(in), 2, &z, 1, h_);
            vDSP_fft_zrip(setup_, &z, 1, log2n_, kFFTDirection_Forward);
            const float half = 0.5f;
            re[0] = sr_[0] * half; im[0] = 0.f;
            re[h_] = si_[0] * half; im[h_] = 0.f;
            vDSP_vsmul(sr_.data() + 1, 1, &half, re + 1, 1, h_ - 1);
            vDSP_vsmul(si_.data() + 1, 1, &half, im + 1, 1, h_ - 1);
            return;
        }
#endif
        float* w = work_.data();
        for (uint32_t i = 0; i < h_; ++i) { const uint32_t r = rev_[i]; w[2 * r] = in[2 * i]; w[2 * r + 1] = in[2 * i + 1]; }
        butterflies(w, false);
        // Split: X[k] = (Z[k] + Z*[h-k])/2 + e^{-j2πk/n} (Z[k] − Z*[h-k])/(2j)
        re[0] = w[0] + w[1]; im[0] = 0.f;
        re[h_] = w[0] - w[1]; im[h_] = 0.f;
        for (uint32_t k = 1; k < h_; ++k) {
            const float zr = w[2 * k], zi = w[2 * k + 1], cr = w[2 * (h_ - k)], ci = -w[2 * (h_ - k) + 1];
            const float er = 0.5f * (zr + cr), ei = 0.5f * (zi + ci);
            const float orr = 0.5f * (zi - ci), oi = -0.5f * (zr - cr);   // (Z − Z*)/(2j)
            const float tr = post_[k].c, ti = post_[k].s;
            re[k] = er + tr * orr - ti * oi;
            im[k] = ei + tr * oi + ti * orr;
        }
    }

    // Real inverse (unnormalised ×n removed: output is the true inverse).
    void inverse(const float* re, const float* im, float* out) noexcept {
#if VK_FFT_VDSP
        if (setup_) {
            // Inverse zrip of the true spectrum X gives n·x.
            sr_[0] = re[0]; si_[0] = re[h_];
            for (uint32_t k = 1; k < h_; ++k) { sr_[k] = re[k]; si_[k] = im[k]; }
            DSPSplitComplex z{sr_.data(), si_.data()};
            vDSP_fft_zrip(setup_, &z, 1, log2n_, kFFTDirection_Inverse);
            vDSP_ztoc(&z, 1, reinterpret_cast<DSPComplex*>(out), 2, h_);
            const float s = 1.f / static_cast<float>(n_);
            vDSP_vsmul(out, 1, &s, out, 1, n_);
            return;
        }
#endif
        float* w = work_.data();
        for (uint32_t k = 0; k < h_; ++k) {
            // Z[k] = E[k] + j·O[k], E = (X[k] + X*[h−k])/2, O = (X[k] − X*[h−k])·e^{+j2πk/n}/2
            const float xr = re[k], xi = im[k], yr = re[h_ - k], yi = -im[h_ - k];
            const float er = 0.5f * (xr + yr), ei = 0.5f * (xi + yi);
            const float dr = 0.5f * (xr - yr), di = 0.5f * (xi - yi);
            const float tr = post_[k].c, ti = -post_[k].s;     // conjugate twiddle
            const float orr = dr * tr - di * ti, oi = dr * ti + di * tr;
            const float zr = er - oi, zi = ei + orr;
            const uint32_t r = rev_[k];
            w[2 * r] = zr; w[2 * r + 1] = zi;
        }
        butterflies(w, true);
        const float s = 1.f / static_cast<float>(h_);
        for (uint32_t i = 0; i < h_; ++i) { out[2 * i] = w[2 * i] * s; out[2 * i + 1] = w[2 * i + 1] * s; }
    }

private:
    struct CS { float c, s; };
    void butterflies(float* w, bool inverse) noexcept {
        for (uint32_t len = 2; len <= h_; len <<= 1) {
            const uint32_t half = len / 2, step = h_ / len;
            for (uint32_t i = 0; i < h_; i += len)
                for (uint32_t j = 0; j < half; ++j) {
                    const uint32_t ti = j * step;
                    float c, s;
                    if (ti <= h_ / 2) { c = tw_[ti].c; s = tw_[ti].s; }
                    else { c = -tw_[ti - h_ / 2].c; s = -tw_[ti - h_ / 2].s; }
                    if (inverse) s = -s;
                    float* a = w + 2 * (i + j); float* b = w + 2 * (i + j + half);
                    const float br = b[0] * c - b[1] * s, bi = b[0] * s + b[1] * c;
                    b[0] = a[0] - br; b[1] = a[1] - bi;
                    a[0] += br; a[1] += bi;
                }
        }
    }
    uint32_t n_, h_;
    std::vector<uint32_t> rev_;
    std::vector<CS> tw_, post_;
    std::vector<float> work_;
#if VK_FFT_VDSP
    FFTSetup setup_ = nullptr;
    vDSP_Length log2n_ = 0;
    std::vector<float> sr_, si_;
#endif
};

} // namespace vektur::fx
