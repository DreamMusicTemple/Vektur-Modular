// preset_audition.hpp — renders a factory preset through the real kernel
// with a short performance chosen by its audition style, and measures it
// (loudness, peak, finite) plus a timbre/envelope fingerprint used to check
// that presets are audibly distinct. Shared by preset_audit and test_presets.
#pragma once
#include "VekturDSP.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <string>
#include <vector>
#include "factory_presets.inc"

struct Audition { double rms = -120, peak = -120; bool finite = true; std::vector<double> features; int rejectedCables = 0; };

inline int rackModule(const std::string& key) {
    for (uint32_t m = 0; m < vk_rack_module_count(); ++m) { VKModuleInfo i; vk_rack_module_info(m, &i); if (key == i.key) return static_cast<int>(m); }
    return -1;
}
inline int rackPort(int m, const std::string& key, bool input) {
    VKModuleInfo mi; vk_rack_module_info(static_cast<uint32_t>(m), &mi);
    for (uint32_t p = 0; p < (input ? mi.inputCount : mi.outputCount); ++p) { VKPortInfo pi; vk_rack_port_info(static_cast<uint32_t>(m), p, input, &pi); if (key == pi.key) return static_cast<int>(p); }
    return -1;
}

inline void fftMag(std::vector<std::complex<double>>& a) {   // in-place radix-2
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}

inline Audition audition(const FPreset& p, double sr = 48000) {
    VKKernel* k = vk_kernel_create();
    for (auto& q : p.params) vk_kernel_set_param(k, q.a, q.v);
    vk_kernel_allocate(k, sr, 2, 256);
    std::vector<VKCable> cables;
    uint16_t id = 0;
    for (auto& c : p.cables) {
        const std::string f(c.from), t(c.to);
        const int sm = rackModule(f.substr(0, f.find('.'))), dm = rackModule(t.substr(0, t.find('.')));
        if (sm < 0 || dm < 0) continue;
        const int sp = rackPort(sm, f.substr(f.find('.') + 1), false), dp = rackPort(dm, t.substr(t.find('.') + 1), true);
        if (sp < 0 || dp < 0) continue;
        cables.push_back({id++, static_cast<uint8_t>(sm), static_cast<uint8_t>(sp), static_cast<uint8_t>(dm), static_cast<uint8_t>(dp), c.gain});
    }
    Audition a;
    if (!cables.empty()) { VKPatchResult r; vk_kernel_set_patch(k, cables.data(), static_cast<uint32_t>(cables.size()), &r); a.rejectedCables = static_cast<int>(r.rejected); }
    vk_kernel_update_effects(k);

    struct N { double t, len; int note, vel; };
    std::vector<N> notes;
    const std::string style(p.audition);
    double secs = 3.0; bool transport = false;
    if (style == "bass") { const int s[8] = {36, 36, 48, 36, 39, 36, 46, 43}; for (int i = 0; i < 16; ++i) notes.push_back({i * 0.125, 0.09, s[i % 8], 110}); secs = 2.6; }
    else if (style == "lead") { const int s[6] = {60, 64, 67, 69, 67, 72}; for (int i = 0; i < 6; ++i) notes.push_back({i * 0.35, 0.36, s[i], 100}); secs = 3.0; }
    else if (style == "chord") { for (int n : {48, 52, 55, 59}) notes.push_back({0, 1.6, n, 100}); secs = 3.2; }
    else if (style == "arp") { for (int n : {48, 52, 55}) notes.push_back({0, 2.8, n, 100}); transport = true; }
    else if (style == "transport") { transport = true; secs = 4.0; notes.push_back({0, 3.8, 48, 100}); }
    else if (style == "drone") { for (int n : {36, 43}) notes.push_back({0, 2.6, n, 100}); secs = 3.4; }
    else { for (int i = 0; i < 3; ++i) notes.push_back({i * 0.7, 0.3, 48 + i * 5, 110}); }

    std::vector<float> L(256), R(256), out; float* ch[2] = {L.data(), R.data()};
    const int blocks = static_cast<int>(secs * sr / 256);
    double beat = 0;
    for (int b = 0; b < blocks; ++b) {
        const double t0 = b * 256 / sr, t1 = (b + 1) * 256 / sr;
        std::vector<VKEvent> ev;
        for (auto& n : notes) for (int on = 1; on >= 0; --on) {
            const double t = on ? n.t : n.t + n.len;
            if (t >= t0 && t < t1) { VKEvent e{}; e.type = VK_EVENT_MIDI1; e.length = 3; e.sampleOffset = static_cast<uint32_t>((t - t0) * sr);
                e.data[0] = on ? 0x90 : 0x80; e.data[1] = static_cast<uint8_t>(n.note); e.data[2] = on ? static_cast<uint8_t>(n.vel) : 0; ev.push_back(e); }
        }
        std::stable_sort(ev.begin(), ev.end(), [](auto& x, auto& y) { return x.sampleOffset < y.sampleOffset; });
        if (transport) { vk_kernel_set_transport(k, 120, beat, true); beat += 256 / sr * 2; }
        vk_kernel_render(k, ch, 2, 256, ev.data(), static_cast<uint32_t>(ev.size()));
        for (int i = 0; i < 256; ++i) { const float m = 0.5f * (L[i] + R[i]); a.finite = a.finite && std::isfinite(L[i]) && std::isfinite(R[i]); out.push_back(m);
            a.peak = std::max(a.peak, 20.0 * std::log10(static_cast<double>(std::max({std::fabs(L[i]), std::fabs(R[i]), 1e-9f})))); }
    }
    vk_kernel_destroy(k);
    std::vector<double> win;
    const size_t W = static_cast<size_t>(sr * 0.05);
    for (size_t i = 0; i + W <= out.size(); i += W) { double s = 0; for (size_t j = i; j < i + W; ++j) s += out[j] * out[j]; win.push_back(s / W); }
    // Loudness: gated RMS — 50 ms windows within 20 dB of the loudest (the
    // notes themselves, not the gaps and decays; like loudness-meter gating).
    const double loud = win.empty() ? 0 : *std::max_element(win.begin(), win.end());
    double sum = 0; size_t cnt = 0; for (double w : win) if (w >= loud * 0.01) { sum += w; ++cnt; }
    a.rms = 10 * std::log10(std::max(sum / std::max<size_t>(cnt, 1), 1e-12));
    // Fingerprint: 10 octave-band shares (31 Hz … 16 kHz) + centroid + 8-point envelope.
    const size_t F = 4096; std::vector<double> bands(10, 1e-12); double cn = 0, cd = 1e-12;
    for (size_t start = 0; start + F <= out.size(); start += F) {
        std::vector<std::complex<double>> x(F);
        for (size_t i = 0; i < F; ++i) x[i] = out[start + i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / F));
        fftMag(x);
        for (size_t b2 = 1; b2 < F / 2; ++b2) {
            const double f = b2 * sr / F, e = std::norm(x[b2]);
            const int band = std::clamp(static_cast<int>(std::floor(std::log2(f / 31.25))), 0, 9);
            bands[band] += e; cn += f * e; cd += e;
        }
    }
    double tot = 0; for (double v : bands) tot += v;
    for (double v : bands) a.features.push_back(std::max(-6.0, std::log10(v / tot)) * 0.5);
    a.features.push_back(std::log2(cn / cd / 250.0) * 0.6);
    const double maxWin = *std::max_element(win.begin(), win.end()) + 1e-12;
    for (int s = 0; s < 8; ++s) { const size_t i = win.size() * s / 8; a.features.push_back(std::max(-6.0, std::log10(win[i] / maxWin + 1e-6)) * 0.35); }
    return a;
}
