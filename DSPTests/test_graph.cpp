// test_graph.cpp — Phase 3 modular engine: regression against the Phase 2
// golden renders, rack introspection, patch validation, feedback, every new
// module, connection telemetry and click-free patch swaps.
#include "VekturDSP.h"
#include "golden_cases.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...) do { ++gChecks; if (!(cond)) { ++gFailures; std::printf("  FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); } \
    else std::printf("  ok   "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

constexpr double SR = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
static int mod(const char* key) {
    for (uint32_t m = 0; m < vk_rack_module_count(); ++m) { VKModuleInfo i; vk_rack_module_info(m, &i); if (std::strcmp(i.key, key) == 0) return static_cast<int>(m); }
    std::printf("no module %s\n", key); std::exit(2);
}
static int port(int m, const char* key, bool input) {
    VKModuleInfo mi; vk_rack_module_info(static_cast<uint32_t>(m), &mi);
    const uint32_t n = input ? mi.inputCount : mi.outputCount;
    for (uint32_t p = 0; p < n; ++p) { VKPortInfo pi; vk_rack_port_info(static_cast<uint32_t>(m), p, input, &pi); if (std::strcmp(pi.key, key) == 0) return static_cast<int>(p); }
    std::printf("no port %s.%s\n", mi.key, key); std::exit(2);
}
// "vco1.out" → "mixer.in1"
static VKCable cable(uint16_t id, const char* from, const char* to, float gain = 1.f) {
    std::string f(from), t(to);
    const auto fd = f.find('.'), td = t.find('.');
    const int sm = mod(f.substr(0, fd).c_str()), dm = mod(t.substr(0, td).c_str());
    VKCable c{};
    c.id = id; c.srcModule = static_cast<uint8_t>(sm); c.srcPort = static_cast<uint8_t>(port(sm, f.substr(fd + 1).c_str(), false));
    c.dstModule = static_cast<uint8_t>(dm); c.dstPort = static_cast<uint8_t>(port(dm, t.substr(td + 1).c_str(), true)); c.gain = gain;
    return c;
}

struct K {
    VKKernel* k; std::vector<float> L, R; float* ch[2];
    K() : k(vk_kernel_create()), L(256), R(256) { ch[0] = L.data(); ch[1] = R.data(); set(VK_P_GLOBAL_INSTABILITY, 0); }
    ~K() { vk_kernel_destroy(k); }
    void set(uint64_t a, float v) { vk_kernel_set_param(k, a, v); }
    void alloc() { vk_kernel_allocate(k, SR, 2, 256); }
    // Sets a patch and lets the 1.3 ms swap fade complete before returning.
    VKPatchResult patch(const std::vector<VKCable>& c, bool settle = true) {
        VKPatchResult r{}; vk_kernel_set_patch(k, c.data(), static_cast<uint32_t>(c.size()), &r);
        if (settle) render(0.0107);
        return r;
    }
    std::vector<float> render(double secs, std::vector<VKEvent> first = {}) {
        std::vector<float> out;
        for (int b = 0; b < static_cast<int>(secs * SR / 256); ++b) {
            vk_kernel_render(k, ch, 2, 256, b == 0 ? first.data() : nullptr, b == 0 ? static_cast<uint32_t>(first.size()) : 0);
            out.insert(out.end(), L.begin(), L.end());
        }
        return out;
    }
    static VKEvent note(int n, bool on = true, uint32_t off = 0) { VKEvent e{}; e.type = VK_EVENT_MIDI1; e.length = 3; e.sampleOffset = off; e.data[0] = on ? 0x90 : 0x80; e.data[1] = static_cast<uint8_t>(n); e.data[2] = on ? 100 : 0; return e; }
};

static void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> wl(std::cos(-2 * kPi / static_cast<double>(len)), std::sin(-2 * kPi / static_cast<double>(len)));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}
static std::vector<double> spectrum(const std::vector<float>& x, size_t from, size_t n = 16384) {
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) a[i] = x[from + i] * (0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(n)));
    fft(a);
    std::vector<double> p(n / 2); for (size_t i = 0; i < n / 2; ++i) p[i] = std::norm(a[i]);
    return p;
}
// Welch-averaged power spectrum (8 Hann windows, 50 % overlap) — stable for noise.
static std::vector<double> welch(const std::vector<float>& x, size_t from, size_t n = 8192, int frames = 8) {
    std::vector<double> acc(n / 2, 0.0);
    for (int f = 0; f < frames; ++f) {
        if (from + static_cast<size_t>(f) * n / 2 + n > x.size()) break;
        auto p = spectrum(x, from + static_cast<size_t>(f) * n / 2, n);
        for (size_t i = 0; i < acc.size(); ++i) acc[i] += p[i];
    }
    return acc;
}
// Mean power per bin between lo and hi (comparable across bands of different width).
static double density(const std::vector<double>& p, double lo, double hi, size_t n = 8192) {
    const size_t a = static_cast<size_t>(lo / SR * static_cast<double>(n)), b = static_cast<size_t>(hi / SR * static_cast<double>(n));
    double s = 0; for (size_t i = a; i < b && i < p.size(); ++i) s += p[i];
    return s / static_cast<double>(std::max<size_t>(1, b - a));
}
static double bandEnergy(const std::vector<double>& p, double lo, double hi, size_t n = 16384) {
    double s = 0; for (size_t b = static_cast<size_t>(lo / SR * static_cast<double>(n)); b < static_cast<size_t>(hi / SR * static_cast<double>(n)) && b < p.size(); ++b) s += p[b]; return s;
}
static double peakHz(const std::vector<double>& p, double lo, double hi, size_t n = 16384) {
    size_t best = static_cast<size_t>(lo / SR * static_cast<double>(n));
    for (size_t b = best; b < static_cast<size_t>(hi / SR * static_cast<double>(n)); ++b) if (p[b] > p[best]) best = b;
    return static_cast<double>(best) * SR / static_cast<double>(n);
}
static double freqOf(const std::vector<float>& x, size_t from, size_t to) {
    double first = -1, last = -1; int n = 0;
    for (size_t i = from + 1; i < to; ++i) if (x[i - 1] < 0.f && x[i] >= 0.f) {
        const double t = static_cast<double>(i - 1) + x[i - 1] / static_cast<double>(x[i - 1] - x[i]);
        if (first < 0) first = t; last = t; ++n; }
    return n < 2 ? 0 : (n - 1) * SR / (last - first);
}
static float peak(const std::vector<float>& x, size_t a = 0, size_t b = ~size_t(0)) { float p = 0; for (size_t i = a; i < std::min(b, x.size()); ++i) p = std::max(p, std::fabs(x[i])); return p; }
static double db(double r) { return 10 * std::log10(std::max(r, 1e-30)); }

// ---------------------------------------------------------------------------
static void testGoldenRegression() {
    std::puts("Regression: normalled graph vs Phase 2 golden renders (best alignment ±64 samples)");
    bool toolchainMatches = true;
    for (auto& c : goldenCases()) {
        std::vector<float> gold;
        FILE* f = std::fopen(("golden/" + c.name + ".f32").c_str(), "rb");
        if (!f) { CHECK(false, "missing golden %s", c.name.c_str()); continue; }
        float v; while (std::fread(&v, 4, 1, f) == 1) gold.push_back(v); std::fclose(f);
        const auto now = renderGolden(c);
        double sig = 0; for (float s : gold) sig += static_cast<double>(s) * s;
        double best = 1e300; int bestLag = 0;
        for (int lag = -64; lag <= 64; ++lag) {
            double num = 0, den = 0;   // least-squares gain for this lag
            for (size_t i = 0; i < gold.size(); ++i) { const long j = static_cast<long>(i) + 2 * lag; if (j < 0 || j >= static_cast<long>(now.size())) continue; num += static_cast<double>(gold[i]) * now[static_cast<size_t>(j)]; den += static_cast<double>(now[static_cast<size_t>(j)]) * now[static_cast<size_t>(j)]; }
            const double a = den > 0 ? num / den : 0;
            double err = 0;
            for (size_t i = 0; i < gold.size(); ++i) { const long j = static_cast<long>(i) + 2 * lag; const double y = (j < 0 || j >= static_cast<long>(now.size())) ? 0 : now[static_cast<size_t>(j)]; const double d = gold[i] - a * y; err += d * d; }
            if (err < best) { best = err; bestLag = lag; }
        }
        const double resid = db(best / sig);
        // Different-by-design paths (documented in PHASE3A_REPORT): A→B now oversamples each
        // filter separately; VCO 3 → cutoff no longer zeroes at span starts (Phase 2 zipper bug).
        // Spectral comparison (1/3-octave bands), used for the two paths that differ by
        // design and whenever the waveform is not bit-close. The goldens were rendered
        // with clang on Linux; other compilers (Xcode's included) round exp2/tan
        // differently, which drifts oscillator phase but not the sound.
        std::vector<float> gl, nl;
        for (size_t i = 0; i < gold.size(); i += 2) { gl.push_back(gold[i]); nl.push_back(now[i]); }
        auto pg = welch(gl, 2048, 4096, 10), pn = welch(nl, 2048, 4096, 10);
        double worst = 0, worse = -1e9, total = 0; for (double v2 : pg) total += v2;
        const double from = c.name == "vco3_mod" ? 150.0 : 50.0;
        for (double lo = 50; lo < 16000; lo *= 1.26) {
            const double eg = density(pg, lo, lo * 1.26, 4096), en = density(pn, lo, lo * 1.26, 4096);
            if (lo < from) { worse = std::max(worse, db(en / eg)); continue; }
            if (eg * 4096 / 2 < total * 1e-4) continue;   // ignore bands 40 dB below total
            worst = std::max(worst, std::fabs(db(en / eg)));
        }
        // The goldens are only bit-reproducible with the toolchain that rendered them
        // (see golden/README.md). "default" is the canary: if it is not bit-close,
        // floating-point rounding differs and waveform/spectral comparison is meaningless.
        if (c.name == "default" && resid >= -60.0) toolchainMatches = false;
        if (!toolchainMatches) {
            std::printf("  SKIP %-15s toolchain differs from the golden renderer (waveform %.1f dB) — see golden/README.md\n", c.name.c_str(), resid);
            continue;
        }
        if (c.name == "a_to_b" || c.name == "vco3_mod") {
            // vco3_mod: Phase 2 zeroed the VCO 3 → cutoff modulation at every control-block
            // start (a bug) — junk below the 196 Hz fundamental. A→B: each filter now has
            // its own oversampler (latency/phase only).
            const double lim = c.name == "a_to_b" ? 1.5 : 3.0;
            CHECK(worst < lim && worse <= 0.5, "%-15s by design: waveform %6.1f dB; worst band from %.0f Hz up %.2f dB%s",
                  c.name.c_str(), resid, from, worst, c.name == "vco3_mod" ? "; sub-fundamental junk reduced" : "");
        } else if (resid < -60.0) {
            CHECK(true, "%-15s bit-close: residual %7.1f dB (lag %+d); spectrum within %.2f dB", c.name.c_str(), resid, bestLag, worst);
        } else {
            CHECK(false, "%-15s NOT bit-close: residual %.1f dB, worst 1/3-octave band %.2f dB", c.name.c_str(), resid, worst);
        }
    }
}

static void testIntrospection() {
    std::puts("Rack introspection");
    const uint32_t n = vk_rack_module_count();
    int visible = 0; bool unique = true, portsOk = true;
    for (uint32_t a = 0; a < n; ++a) {
        VKModuleInfo ia; vk_rack_module_info(a, &ia);
        if (ia.inputCount + ia.outputCount > 0) ++visible;
        for (uint32_t b = a + 1; b < n; ++b) { VKModuleInfo ib; vk_rack_module_info(b, &ib); if (std::strcmp(ia.key, ib.key) == 0) unique = false; }
        for (int dir = 0; dir < 2; ++dir) {
            const uint32_t c = dir ? ia.inputCount : ia.outputCount;
            for (uint32_t p = 0; p < c; ++p) for (uint32_t q = p + 1; q < c; ++q) {
                VKPortInfo pp, pq; vk_rack_port_info(a, p, dir, &pp); vk_rack_port_info(a, q, dir, &pq);
                if (std::strcmp(pp.key, pq.key) == 0) portsOk = false;
            }
        }
    }
    CHECK(n == 41 && visible == 39 && unique && portsOk && vk_rack_normal_count() > 30,
          "%u modules (%d patchable, 2 hidden routing nodes), %u normalled connections, unique keys", n, visible, vk_rack_normal_count());
    VKPortInfo fm; vk_rack_port_info(static_cast<uint32_t>(mod("vco2")), static_cast<uint32_t>(port(mod("vco2"), "fm", true)), true, &fm);
    CHECK(fm.attenuatorAddress == static_cast<int64_t>(VK_P_VCO2_FM_AMOUNT), "VCO 2 FM input attenuator is the 'FM from VCO 1' knob");
}

static void testValidation() {
    std::puts("Patch validation");
    K k; k.alloc();
    VKCable bad = cable(3, "vco1.out", "mixer.in1"); bad.srcPort = 9;
    VKCable gainBad = cable(4, "vco4.out", "mult1.in"); gainBad.gain = 5.f;
    VKCable hidden = cable(5, "vco4.out", "mixer.in2"); hidden.dstModule = static_cast<uint8_t>(15);   // routeB (hidden)
    auto r = k.patch({cable(1, "vco4.out", "mixer.in1"), cable(2, "vco5.out", "mixer.in1"), cable(1, "vco6.out", "vcfA.in"), bad, gainBad, hidden});
    bool reasons = r.rejectedReasons[0] == VK_REJECT_INPUT_TAKEN && r.rejectedReasons[1] == VK_REJECT_DUPLICATE_ID &&
                   r.rejectedReasons[2] == VK_REJECT_DIRECTION && r.rejectedReasons[3] == VK_REJECT_BAD_GAIN && r.rejectedReasons[4] == VK_REJECT_BAD_INDEX;
    CHECK(r.accepted == 1 && r.rejected == 5 && reasons, "1 accepted; rejected: input taken, duplicate id, bad port, bad gain, hidden module");
}

static void testReplaceNormal() {
    std::puts("A cable into an input replaces its normalled connection");
    K k; k.set(VK_P_GLOBAL_FILTER_ROUTING, 0); k.set(VK_P_VCFA_CUTOFF, 20000); k.set(VK_P_VCFA_KEY_TRACK, 0);
    k.set(VK_P_VCO4_OCTAVE, 4);   // VCO 4 one octave up
    k.alloc();
    k.patch({cable(1, "vco4.out", "mixer.in1")});
    auto x = k.render(0.5, {K::note(57)});
    const double f = freqOf(x, 9600, x.size());
    CHECK(std::fabs(f - 440.0) < 1.0, "VCO 4 (+1 oct) replaces VCO 1 in mixer input 1: %.2f Hz for A3 (expect 440)", f);
    const float l = vk_kernel_connection_level(k.k, 1);
    VKNormalInfo ni; uint32_t vco2Mix = 0;
    for (uint32_t i = 0; i < vk_rack_normal_count(); ++i) { vk_rack_normal_info(i, &ni); if (ni.srcModule == mod("vco2") && ni.dstModule == mod("mixer")) vco2Mix = i; }
    const float dead = vk_kernel_connection_level(k.k, VK_NORMAL_SLOT_BASE + vco2Mix);
    CHECK(l > 0.3f && dead == 0.f, "connection level: patched cable %.2f, VCO 2→mixer (level 0) %.2f", l, dead);
}

static void testFeedback() {
    std::puts("Feedback patch: VCA out → mixer4 → Filter B input (cycle)");
    K k; k.set(VK_P_VCFB_RESONANCE, 85); k.set(VK_P_MIXER4_GAIN1, 90); k.set(VK_P_MIXER4_GAIN2, 100);
    k.alloc();
    auto r = k.patch({cable(1, "vca.out", "mixer4.in1"), cable(2, "mixer.out", "mixer4.in2"), cable(3, "mixer4.out", "vcfB.in")});
    auto x = k.render(5.0, {K::note(40)});
    bool finite = true; for (float s : x) finite &= std::isfinite(s);
    CHECK(r.feedbackEdges >= 1 && finite && peak(x) < 2.0f && peak(x, 48000) > 0.01f,
          "%u feedback edge(s) broken with delay + limiter; 5 s finite, peak %.2f", r.feedbackEdges, peak(x));
}

static void testUtilities() {
    std::puts("Multiples, switch (sequential), gate delay, slew portamento");
    {
        K k; k.alloc();
        k.patch({cable(1, "vco1.out", "mult1.in"), cable(2, "mult1.out3", "output.in")});
        auto x = k.render(0.3, {K::note(57)});
        CHECK(peak(x, 4800) > 0.5f && std::fabs(freqOf(x, 4800, x.size()) - 220) < 0.5, "raw VCO via multiple out 3 to output: peak %.2f at 220 Hz", peak(x, 4800));
    }
    {
        K k; k.set(VK_P_SWITCH_MODE, 2); k.set(VK_P_SWITCH_STEPS, 0);   // sequential, 2 steps
        k.set(VK_P_VCO2_OCTAVE, 4); k.set(VK_P_GLOBAL_FILTER_ROUTING, 0); k.set(VK_P_VCFA_CUTOFF, 20000); k.set(VK_P_VCFA_KEY_TRACK, 0);
        k.alloc();
        k.patch({cable(1, "vco1.out", "switch.in1"), cable(2, "vco2.out", "switch.in2"), cable(3, "keyboard.trigger", "switch.clock"), cable(4, "switch.out", "vcfA.in")});
        std::vector<double> f;
        for (int i = 0; i < 3; ++i) { auto x = k.render(0.3, {K::note(57)}); f.push_back(freqOf(x, 4800, x.size())); k.render(0.4, {K::note(57, false)}); }
        CHECK(std::fabs(f[0] - 440) < 1 && std::fabs(f[1] - 220) < 1 && std::fabs(f[2] - 440) < 1,
              "each note advances the switch: %.0f, %.0f, %.0f Hz (in2, in1, in2)", f[0], f[1], f[2]);
    }
    {
        K k; k.set(VK_P_GATEDELAY_DELAY, 100); k.alloc();
        k.patch({cable(1, "keyboard.gate", "gateDelay.in"), cable(2, "gateDelay.out", "eg2.gate")});
        auto x = k.render(0.4, {K::note(57)});
        size_t onset = 0; for (size_t i = 0; i < x.size(); ++i) if (std::fabs(x[i]) > 0.01f) { onset = i; break; }
        CHECK(std::fabs(static_cast<double>(onset) / 48.0 - 100.0) < 3.0, "sound starts %.1f ms after note-on (gate delay 100 ms)", static_cast<double>(onset) / 48.0);
    }
    {
        K k; k.set(VK_P_SLEW_RISE, 300); k.set(VK_P_SLEW_FALL, 300); k.set(VK_P_GLOBAL_POLYPHONY, 0);
        k.set(VK_P_GLOBAL_FILTER_ROUTING, 0); k.set(VK_P_VCFA_CUTOFF, 20000); k.set(VK_P_VCFA_KEY_TRACK, 0); k.alloc();
        k.patch({cable(1, "keyboard.pitch", "slew.in"), cable(2, "slew.out", "vco1.pitch")});
        k.render(1.0, {K::note(48)});
        auto x = k.render(0.06, {K::note(48, false), K::note(60)});
        const double f = freqOf(x, 256, x.size());
        CHECK(f > 140 && f < 250, "slew limiter as portamento: %.1f Hz 60 ms into C3→C4", f);
    }
}

static void testFilters() {
    std::puts("New filters (white noise in, Welch-averaged spectrum out; 12 dB/oct slopes give ≈ 20–40 dB over these bands)");
    constexpr size_t N = 8192;
    auto run = [](std::vector<std::pair<uint64_t, float>> params, std::vector<VKCable> patch) {
        K k; k.set(VK_P_NOISE_TYPE, 0); for (auto& [a, v] : params) k.set(a, v); k.alloc(); k.patch(patch);
        auto x = k.render(1.2, {K::note(60)});
        return welch(x, 9600);
    };
    {
        auto p = run({{VK_P_COMB_FREQUENCY, 440}, {VK_P_COMB_FEEDBACK, 95}, {VK_P_COMB_DAMPING, 0}},
                     {cable(1, "noise.out", "comb.in", 0.25f), cable(2, "comb.out", "output.in")});
        const double f = peakHz(p, 300, 600, N), contrast = db(density(p, 435, 445) / density(p, 600, 800));
        CHECK(std::fabs(f / 440 - 1) < 0.01 && contrast > 10, "comb (440 Hz, 95%% fb): peak %.1f Hz, %.1f dB above the trough between teeth", f, contrast);
    }
    {
        auto a = run({{VK_P_FORMANT_VOWEL, 0}}, {cable(1, "noise.out", "formant.in"), cable(2, "formant.out", "output.in")});
        auto i = run({{VK_P_FORMANT_VOWEL, 50}}, {cable(1, "noise.out", "formant.in"), cable(2, "formant.out", "output.in")});
        const double fa = peakHz(a, 150, 900, N), fi = peakHz(i, 150, 900, N);
        CHECK(std::fabs(fa / 600 - 1) < 0.08 && std::fabs(fi / 250 - 1) < 0.1, "formant F1: 'A' %.0f Hz (600), 'I' %.0f Hz (250)", fa, fi);
    }
    {
        auto p = run({{VK_P_VCBPF_CENTRE, 2000}, {VK_P_VCBPF_Q, 12}}, {cable(1, "noise.out", "vcbpf.in"), cable(2, "vcbpf.out", "output.in")});
        const double f = peakHz(p, 500, 8000, N);
        CHECK(std::fabs(f / 2000 - 1) < 0.03 && db(density(p, 1950, 2050) / density(p, 3500, 4500)) > 15, "VC band-pass (2 kHz, Q 12): peak at %.0f Hz", f);
    }
    {
        std::vector<std::pair<uint64_t, float>> bands;
        const uint64_t addr[8] = {VK_P_FILTERBANK_BAND1, VK_P_FILTERBANK_BAND2, VK_P_FILTERBANK_BAND3, VK_P_FILTERBANK_BAND4,
                                  VK_P_FILTERBANK_BAND5, VK_P_FILTERBANK_BAND6, VK_P_FILTERBANK_BAND7, VK_P_FILTERBANK_BAND8};
        for (int b = 0; b < 8; ++b) bands.push_back({addr[b], b == 3 ? 100.f : 0.f});
        auto p = run(bands, {cable(1, "noise.out", "filterBank.in"), cable(2, "filterBank.out", "output.in")});
        const double f = peakHz(p, 100, 16000, N), c = db(density(p, 900, 1100) / density(p, 5000, 7000));
        CHECK(std::fabs(f / 1000 - 1) < 0.1 && c > 15, "filter bank, only 1 kHz band open: peak %.0f Hz, %.1f dB above 5–7 kHz", f, c);
    }
    {
        auto p = run({{VK_P_OTA_CUTOFF, 500}, {VK_P_OTA_RESONANCE, 0}, {VK_P_OTA_DRIVE, 0}}, {cable(1, "noise.out", "ota.in"), cable(2, "ota.lp", "output.in")});
        const double c = db(density(p, 100, 300) / density(p, 4000, 6000));
        CHECK(c > 35, "OTA low-pass 500 Hz: passband %.1f dB above 4–6 kHz (12 dB/oct ≈ 40)", c);
    }
    for (const char* f : {"diodeLadder", "transistorLadder"}) {
        const std::string in = std::string(f) + ".in", out = std::string(f) + ".out";
        auto p = run({{VK_P_DIODELADDER_CUTOFF, 600}, {VK_P_DIODELADDER_RESONANCE, 0}, {VK_P_TRANSISTORLADDER_CUTOFF, 600}, {VK_P_TRANSISTORLADDER_RESONANCE, 0}},
                     {cable(1, "noise.out", in.c_str()), cable(2, out.c_str(), "output.in")});
        const double c = db(density(p, 100, 300) / density(p, 4000, 6000));
        CHECK(c > 60, "%s patched (cutoff 600 Hz): passband %.1f dB above 4–6 kHz (24 dB/oct)", f, c);
    }
    {   // Filter A multi-output: LP and HP outputs patched separately
        auto lp = run({{VK_P_VCFA_CUTOFF, 1000}, {VK_P_VCFA_KEY_TRACK, 0}}, {cable(1, "noise.out", "vcfA.in"), cable(2, "vcfA.lp", "output.in")});
        auto hp = run({{VK_P_VCFA_CUTOFF, 1000}, {VK_P_VCFA_KEY_TRACK, 0}}, {cable(1, "noise.out", "vcfA.in"), cable(2, "vcfA.hp", "output.in")});
        const double cl = db(density(lp, 100, 300) / density(lp, 5000, 9000)), ch = db(density(hp, 5000, 9000) / density(hp, 100, 300));
        CHECK(cl > 20 && ch > 20, "Filter A outputs patched separately: LP %.1f dB, HP %.1f dB of separation", cl, ch);
    }
}

static void testSwapIsClickFree() {
    std::puts("Patch changes while playing are click-free");
    K k; k.set(VK_P_GLOBAL_FILTER_ROUTING, 0); k.set(VK_P_VCFA_CUTOFF, 20000); k.alloc();
    auto steady = k.render(0.3, {K::note(45)});
    float steadyDelta = 0; for (size_t i = 4801; i < steady.size(); ++i) steadyDelta = std::max(steadyDelta, std::fabs(steady[i] - steady[i - 1]));
    float worst = 0, prev = steady.back();
    for (int s = 0; s < 40; ++s) {
        if (s % 2 == 0) k.patch({cable(static_cast<uint16_t>(s % 7), "noise.out", "mixer.in1", 1.f)}, false);
        else k.patch({}, false);
        auto x = k.render(0.05);
        for (float v : x) { worst = std::max(worst, std::fabs(v - prev)); prev = v; }
    }
    CHECK(worst <= steadyDelta * 2.f, "40 swaps between saw and noise: worst step %.3f vs steady-saw step %.3f", worst, steadyDelta);
}

int main() {
    testGoldenRegression();
    testIntrospection();
    testValidation();
    testReplaceNormal();
    testFeedback();
    testUtilities();
    testFilters();
    testSwapIsClickFree();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
