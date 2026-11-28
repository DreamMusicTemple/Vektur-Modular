// test_modules.cpp — measurement-based tests for every Phase 2 DSP module and
// the normalled voice. Numbers printed alongside PASS/FAIL are the measured
// values, so regressions in sound quality are visible, not just pass/fail.
#include "VekturDSP.h"
#include "Kernel.hpp"
#include "modules/Envelope.hpp"
#include "modules/Filters.hpp"
#include "modules/Utilities.hpp"
#include "modules/Vco.hpp"
#include "modules/LadderVariants.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace vektur;

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...) do { ++gChecks; if (!(cond)) { ++gFailures; std::printf("  FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); } \
    else std::printf("  ok   "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

constexpr double SR = 48000.0;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t j = 0; j < len / 2; ++j) {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
}
static std::vector<double> powerSpectrum(const std::vector<float>& x, size_t from, size_t n) {
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) a[i] = x[from + i];
    fft(a);
    std::vector<double> p(n / 2);
    for (size_t i = 0; i < n / 2; ++i) p[i] = std::norm(a[i]);
    return p;
}
static double db(double r) { return 10.0 * std::log10(std::max(r, 1e-30)); }

// Interpolated rising zero crossings → frequency.
static double freqOf(const std::vector<float>& x, size_t from) {
    double first = -1, last = -1; int n = 0;
    for (size_t i = from + 1; i < x.size(); ++i)
        if (x[i - 1] < 0.f && x[i] >= 0.f) {
            const double t = static_cast<double>(i - 1) + x[i - 1] / static_cast<double>(x[i - 1] - x[i]);
            if (first < 0) first = t; last = t; ++n;
        }
    return n < 2 ? 0.0 : (n - 1) * SR / (last - first);
}
static double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

static double sineGain(const std::function<float(float)>& f, double hz, int settle = 9600, int measure = 9600) {
    double peak = 0;
    for (int i = 0; i < settle + measure; ++i) {
        const float y = f(static_cast<float>(0.1 * std::sin(2.0 * kPi * hz * i / SR)));
        if (i >= settle) peak = std::max(peak, static_cast<double>(std::fabs(y)));
    }
    return peak / 0.1;
}

// ---------------------------------------------------------------------------
// VCO
// ---------------------------------------------------------------------------
static std::vector<float> runVco(Wave w, double hz, size_t n, float pw = 0.5f) {
    Vco v; v.reset(0.f); Vco::Wrap wr;
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = v.process(hz / SR, w, pw, nullptr, false, wr);
    return out;
}

static void testVcoPitch() {
    std::puts("VCO pitch accuracy (all waveforms, 27.5 Hz … 4186 Hz)");
    const Wave waves[] = {Wave::Sine, Wave::Triangle, Wave::Saw, Wave::ReverseSaw, Wave::Square, Wave::Pulse};
    const char* names[] = {"sine", "tri", "saw", "rsaw", "square", "pulse"};
    double worst = 0;
    for (int w = 0; w < 6; ++w)
        for (double hz : {27.5, 110.0, 440.0, 1760.0, 4186.0}) {
            auto x = runVco(waves[w], hz, 48000, 0.3f);
            worst = std::max(worst, std::fabs(cents(freqOf(x, 100), hz)));
            (void)names;
        }
    CHECK(worst < 0.1, "worst pitch error %.4f cents", worst);
}

// Alias energy: coherent sampling puts every true harmonic exactly on a bin
// multiple of k; everything else is aliasing (or numerical noise).
// Alias energy in the audible band (< 15 kHz) relative to the harmonic energy.
static double aliasDb(const std::vector<float>& x, size_t k, size_t n) {
    auto p = powerSpectrum(x, 4096, n);
    double harm = 0, alias = 0;
    for (size_t b = 1; b < p.size(); ++b) {
        if ((b % k) == 0) harm += p[b];
        else if (static_cast<double>(b) * SR / static_cast<double>(n) < 15000.0) alias += p[b];
    }
    return db(alias / harm);
}

static void testVcoAliasing() {
    std::puts("VCO aliasing below 15 kHz vs naive waveform (f = 4101.6 Hz, coherent 16384-pt FFT)");
    const size_t N = 16384, K = 1400;                    // bin spacing 2.93 Hz → f = 4101.6 Hz
    const double hz = K * SR / N;
    struct Case { Wave w; const char* name; double minImprovement; } cases[] = {
        {Wave::Saw, "saw", 25}, {Wave::Square, "square", 25}, {Wave::Triangle, "triangle", 20}, {Wave::Pulse, "pulse 30%", 25}};
    for (auto& c : cases) {
        auto blep = runVco(c.w, hz, N + 4096, 0.3f);
        std::vector<float> naive(N + 4096);
        double ph = 0; for (auto& s : naive) { s = Vco::naiveValue(ph, c.w, 0.3f); ph += hz / SR; ph -= std::floor(ph); }
        const double a = aliasDb(blep, K, N), b = aliasDb(naive, K, N);
        CHECK(b - a >= c.minImprovement && a < -38.0, "%-9s alias %6.1f dB (naive %6.1f dB) → %.1f dB better", c.name, a, b, b - a);
    }
}

static void testVcoSyncAndSub() {
    std::puts("VCO hard sync, soft sync, sub-oscillator");
    const size_t N = 16384, K = 300;   // master 878.9 Hz on bin 300
    const double fm = K * SR / N;
    Vco master, slave; master.reset(); slave.reset();
    std::vector<float> s(N + 4096), sub(N + 4096);
    Vco::Wrap w1, w2;
    for (auto i = 0u; i < s.size(); ++i) {
        float so = 0;
        master.process(fm / SR, Wave::Saw, 0.5f, nullptr, false, w1, &so, 1);
        sub[i] = so;
        s[i] = slave.process(fm * 1.37 / SR, Wave::Saw, 0.5f, &w1, false, w2);
    }
    auto p = powerSpectrum(s, 4096, N);
    double harm = 0, other = 0;
    for (size_t b = 1; b < p.size(); ++b) ((b % K) == 0 ? harm : other) += p[b];
    CHECK(db(other / harm) < -30, "hard-synced slave locks to master: off-harmonic energy %.1f dB", db(other / harm));
    const double fsub = freqOf(sub, 4096);
    CHECK(std::fabs(cents(fsub, fm / 2)) < 0.5, "sub −1 oct = %.2f Hz (expect %.2f)", fsub, fm / 2);

    // Soft sync: slave near an integer ratio is pulled onto the master's harmonics.
    master.reset(); slave.reset();
    for (auto i = 0u; i < s.size(); ++i) { master.process(fm / SR, Wave::Saw, .5f, nullptr, false, w1); s[i] = slave.process(fm * 2.03 / SR, Wave::Triangle, .5f, &w1, true, w2); }
    p = powerSpectrum(s, 4096, N); harm = other = 0;
    for (size_t b = 1; b < p.size(); ++b) ((b % K) == 0 ? harm : other) += p[b];
    CHECK(db(other / harm) < -20, "soft sync pulls slave onto master harmonics: off-harmonic %.1f dB", db(other / harm));
}

// ---------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------
static void testSvf() {
    std::puts("Filter A (SVF) responses, fc = 1 kHz, resonance 0");
    const float g = std::tan(kPiF * 1000.f / static_cast<float>(SR));
    auto resp = [&](SvfFilter::Mode m, double hz) {
        SvfFilter f; f.reset(); f.setCoefficients(g, 0.f, 1.f);
        return 20 * std::log10(sineGain([&](float x) { return f.process(x, m); }, hz));
    };
    const double lpLow = resp(SvfFilter::Mode::LowPass, 100), lpHigh = resp(SvfFilter::Mode::LowPass, 10000);
    CHECK(std::fabs(lpLow) < 0.5 && lpHigh < -36, "LP: %.2f dB @100 Hz, %.1f dB @10 kHz (12 dB/oct)", lpLow, lpHigh);
    const double hpLow = resp(SvfFilter::Mode::HighPass, 100), hpHigh = resp(SvfFilter::Mode::HighPass, 10000);
    CHECK(hpLow < -36 && std::fabs(hpHigh) < 0.5, "HP: %.1f dB @100 Hz, %.2f dB @10 kHz", hpLow, hpHigh);
    const double bpC = resp(SvfFilter::Mode::BandPass, 1000), bpL = resp(SvfFilter::Mode::BandPass, 100);
    CHECK(std::fabs(bpC + 6.02) < 0.3 && bpL < bpC - 12, "BP: %.2f dB @fc (−6.02 = 1/k at Q 0.5), %.1f dB @fc/10", bpC, bpL);
    const double nC = resp(SvfFilter::Mode::Notch, 1000), nL = resp(SvfFilter::Mode::Notch, 100);
    CHECK(nC < -30 && std::fabs(nL) < 0.5, "Notch: %.1f dB @fc, %.2f dB @fc/10", nC, nL);
    // DC rejection of HP
    SvfFilter f; f.setCoefficients(g, 0.5f, 1.f); double mean = 0;
    for (int i = 0; i < 48000; ++i) { const float y = f.process(0.5f, SvfFilter::Mode::HighPass); if (i > 24000) mean += y; }
    CHECK(std::fabs(mean / 24000) < 1e-4, "HP removes DC: residual %.2e", mean / 24000);
}

static void testLadder() {
    std::puts("Filter B (ladder) response, fc = 1 kHz, resonance 0");
    const float g = std::tan(kPiF * 1000.f / static_cast<float>(SR));
    auto resp = [&](double hz, float res) {
        LadderFilter f; f.reset(); f.setCoefficients(g, res, 1.f, 0.f);
        return 20 * std::log10(sineGain([&](float x) { return f.process(x); }, hz));
    };
    const double pass = resp(100, 0), at4 = resp(4000, 0), at8 = resp(8000, 0);
    CHECK(std::fabs(pass) < 0.5, "passband %.2f dB @100 Hz", pass);
    CHECK(at8 < -40 && (at4 - at8) > 20 && (at4 - at8) < 27, "%.1f dB @4 kHz, %.1f dB @8 kHz → slope %.1f dB/oct (24 nominal)", at4, at8, at4 - at8);
    const double resPeak = resp(1000, 0.8f), resPass = resp(100, 0.8f);
    CHECK(resPeak > resPass + 6, "resonance 0.8: %.1f dB peak @fc vs %.1f dB @100 Hz (bass loss, as hardware)", resPeak, resPass);
}

template <typename F>
static void selfOsc(const char* name, F&& proc, double fc, double tolPct) {
    std::vector<float> y(48000);
    for (size_t i = 0; i < y.size(); ++i) y[i] = proc(i == 0 ? 0.5f : 0.f);
    const double f = freqOf(y, 24000);
    float peak = 0; for (size_t i = 24000; i < y.size(); ++i) peak = std::max(peak, std::fabs(y[i]));
    CHECK(peak > 0.1f && peak < 2.f && std::fabs(f / fc - 1) * 100 < tolPct,
          "%s self-oscillates: %.1f Hz (fc %.0f, %.2f%% off), amplitude %.2f", name, f, fc, (f / fc - 1) * 100, peak);
}

static void testSelfOscillationAndStability() {
    std::puts("Self-oscillation and stability");
    for (double fc : {220.0, 1000.0, 4000.0}) {
        const float g = std::tan(kPiF * static_cast<float>(fc / SR));
        SvfFilter a; a.setCoefficients(g, 1.f, 1.f);
        selfOsc("SVF   ", [&](float x) { return a.process(x, SvfFilter::Mode::BandPass); }, fc, 2.0);
        LadderFilter b; b.setCoefficients(g, 1.f, 1.f, 0.6f);
        selfOsc("Ladder", [&](float x) { return b.process(x); }, fc, 6.0);
    }
    // Fuzz: random cutoff/resonance/drive jumps every 32 samples, loud noise in, 20 s.
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    SvfFilter a; LadderFilter b; Oversampler os; os.setFactor(2);
    float worstA = 0, worstB = 0; bool finite = true;
    for (int i = 0; i < 48000 * 20; ++i) {
        if (i % 32 == 0) {
            const float fc = 20.f * std::pow(1000.f, u(rng));
            const float gg = std::tan(kPiF * std::min(fc, 0.45f * 96000.f) / 96000.f);
            a.setCoefficients(gg, u(rng), dbToGain(24.f * u(rng)));
            b.setCoefficients(gg, u(rng), dbToGain(24.f * u(rng)), 0.6f);
        }
        const float x = 2.f * (u(rng) - 0.5f);
        const float ya = os.process(x, [&](float v) { return a.process(v, SvfFilter::Mode::LowPass); });
        const float yb = b.process(x);
        finite &= std::isfinite(ya) && std::isfinite(yb);
        worstA = std::max(worstA, std::fabs(ya)); worstB = std::max(worstB, std::fabs(yb));
    }
    CHECK(finite && worstA < 4.f && worstB < 6.f, "20 s random modulation fuzz: finite, peaks %.2f (A) %.2f (B)", worstA, worstB);
}

static void testLadderVariants() {
    std::puts("Diode-ladder and transistor-ladder filters");
    auto gainDb = [](auto& f, double hz) { f.reset(); return 20 * std::log10(sineGain([&](float x) { return f.process(x); }, hz)); };
    for (double fc : {250.0, 1000.0, 4000.0}) {
        DiodeLadder d; d.setCoefficients(static_cast<float>(fc), SR, 0.f, 1.f);
        // Spread (coupled) poles: softer knee, ~1 dB droop at fc/10, slope approaches
        // 24 dB/oct only well above fc — measured 4→8×fc (2→4× at 4 kHz, below Nyquist).
        const double mult = fc < 2000 ? 4.0 : 2.0;
        const double pass = gainDb(d, fc / 10), a4 = gainDb(d, fc * mult), a8 = gainDb(d, fc * mult * 2);
        CHECK(std::fabs(pass) < 2.0 && (a4 - a8) > 20 && (a4 - a8) < 34,
              "diode fc %.0f: passband %+.2f dB (soft knee), slope %.1f dB/oct (%.0f→%.0f×fc)", fc, pass, a4 - a8, mult, mult * 2);
        TransistorLadder t; t.setCoefficients(static_cast<float>(fc), SR, 0.f, 1.f);
        const double tp = gainDb(t, fc / 10), tc = gainDb(t, fc), t4 = gainDb(t, fc * 2), t8 = gainDb(t, fc * 4);
        CHECK(std::fabs(tp) < 0.5 && std::fabs(tc + 12.0) < 1.5 && (t4 - t8) > 20,
              "transistor fc %.0f: passband %+.2f dB, %.1f dB at fc (4 poles at fc ≈ −12), slope %.1f dB/oct", fc, tp, tc, t4 - t8);
        DiodeLadder ds; ds.setCoefficients(static_cast<float>(fc), SR, 1.f, 1.f);
        selfOsc("Diode     ", [&](float x) { return ds.process(x); }, fc, 5.0);
        TransistorLadder ts; ts.setCoefficients(static_cast<float>(fc), SR, 1.f, 1.f);
        selfOsc("Transistor", [&](float x) { return ts.process(x); }, fc, 6.0);
    }
    std::mt19937 rng(11); std::uniform_real_distribution<float> u(0.f, 1.f);
    DiodeLadder d; TransistorLadder t; float wd = 0, wt = 0; bool fin = true;
    for (int i = 0; i < 48000 * 10; ++i) {
        if (i % 32 == 0) {
            const float fc = 20.f * std::pow(1000.f, u(rng));
            d.setCoefficients(fc, SR, u(rng), dbToGain(24.f * u(rng)));
            t.setCoefficients(fc, SR, u(rng), dbToGain(24.f * u(rng)));
        }
        const float x = 2.f * (u(rng) - 0.5f);
        const float a = d.process(x), b = t.process(x);
        fin &= std::isfinite(a) && std::isfinite(b);
        wd = std::max(wd, std::fabs(a)); wt = std::max(wt, std::fabs(b));
    }
    CHECK(fin && wd < 8.f && wt < 8.f, "10 s random modulation fuzz: finite, peaks %.2f (diode) %.2f (transistor)", wd, wt);
}

static void testOversampler() {
    std::puts("Oversampler passband");
    for (int f : {2, 4}) {
        for (double hz : {1000.0, 10000.0}) {
            Oversampler o; o.setFactor(f);
            const double gdb = 20 * std::log10(sineGain([&](float x) { return o.process(x, [](float v) { return v; }); }, hz));
            CHECK(std::fabs(gdb) < 0.2, "%d×: %.0f Hz passes at %+.3f dB", f, hz, gdb);
        }
    }
}

// ---------------------------------------------------------------------------
// Envelope, VCA, utilities
// ---------------------------------------------------------------------------
static int samplesUntil(Envelope& e, float level, bool rising, int limit = 48000 * 30) {
    for (int i = 1; i <= limit; ++i) { const float v = e.next(); if (rising ? v >= level : v <= level) return i; }
    return -1;
}

static void testEnvelope() {
    std::puts("Envelope timing and curves");
    for (auto c : {Envelope::Curve::Linear, Envelope::Curve::Exponential, Envelope::Curve::Logarithmic, Envelope::Curve::Analogue}) {
        Envelope e; e.prepare(SR);
        Envelope::Params p; p.attackMs = 100; p.decayMs = 200; p.sustain = 0.5f; p.releaseMs = 300; p.curve = c;
        e.setParams(p); e.gateOn();
        int a = 0; while (e.stage() == Envelope::Stage::Attack && a < 96000) { e.next(); ++a; }
        int d = 0; while (e.stage() != Envelope::Stage::Sustain && d < 48000) { e.next(); ++d; }
        const float sus = e.next();
        e.gateOff();
        int r = 0; while (!e.isIdle() && r < 96000) { e.next(); ++r; }
        CHECK(std::abs(a - 4800) <= 2 && std::abs(d - 9600) <= 3 && std::fabs(sus - 0.5f) < 1e-3f && std::abs(r - 14400) <= 3,
              "curve %d: attack %d (4800), decay %d (9600), sustain %.3f, release %d (14400) samples", static_cast<int>(c), a, d, sus, r);
    }
    // Retrigger from current level during release: continuous.
    Envelope e; e.prepare(SR); Envelope::Params p; p.attackMs = 20; p.releaseMs = 500; p.sustain = 1; e.setParams(p);
    e.gateOn(); for (int i = 0; i < 4800; ++i) e.next();
    e.gateOff(); for (int i = 0; i < 2400; ++i) e.next();
    const float before = e.level(); e.gateOn(); const float after = e.next();
    CHECK(std::fabs(after - before) < 0.01f, "retrigger continues from %.3f → %.3f (no click)", before, after);
    // Loop: cycles while gate held.
    p.attackMs = 10; p.decayMs = 10; p.sustain = 0.2f; p.loop = true; p.curve = Envelope::Curve::Linear; e.setParams(p); e.reset(); e.gateOn();
    int peaks = 0; float prev = 0, prev2 = 0;
    for (int i = 0; i < 48000; ++i) { const float v = e.next(); if (prev > v && prev > prev2 && prev > 0.99f) ++peaks; prev2 = prev; prev = v; }
    CHECK(peaks >= 45 && peaks <= 50, "loop: %d attack peaks per second (≈48 for 10 ms + 10 ms)", peaks);
}

static void testVcaAndUtilities() {
    std::puts("VCA, noise, ring mod, S&H, slew, follower, voltage processor, mixer");
    CHECK(Vca::gainLaw(0.5f, Vca::Response::Linear) == 0.5f && Vca::gainLaw(0, Vca::Response::Exponential) == 0.f &&
          std::fabs(Vca::gainLaw(1, Vca::Response::Exponential) - 1.f) < 1e-6f,
          "gain laws: lin 0.5→0.5, exp 0→0, 1→1, exp 0.5→%.1f dB", 20 * std::log10(Vca::gainLaw(0.5f, Vca::Response::Exponential)));
    float worst = 0; for (int i = 0; i < 1000; ++i) worst = std::max(worst, std::fabs(Vca::process(10.f * std::sin(i * 0.1f), 1.f, Vca::Response::Linear, 1.f, 0.f)));
    CHECK(worst <= 1.0001f, "saturating VCA bounded: peak %.3f", worst);

    Noise n(123); double sum = 0, sq = 0; const int N = 480000;
    for (int i = 0; i < N; ++i) { const float v = n.white(); sum += v; sq += v * v; }
    CHECK(std::fabs(sum / N) < 0.005 && std::fabs(std::sqrt(sq / N) - 0.5774) < 0.01, "white: mean %.4f, RMS %.4f (0.577)", sum / N, std::sqrt(sq / N));
    auto bandRatio = [](std::vector<float> x) {
        auto p = powerSpectrum(x, 0, 65536);
        auto band = [&](double lo, double hi) { double s = 0; for (size_t b = static_cast<size_t>(lo / SR * 65536); b < static_cast<size_t>(hi / SR * 65536); ++b) s += p[b]; return s; };
        return db(band(2000, 4000) / band(250, 500));
    };
    std::vector<float> w(65536), pk(65536); Noise n2(9);
    for (auto& v : w) v = n2.white();
    for (auto& v : pk) v = n2.pink();
    const double wr = bandRatio(w), pr = bandRatio(pk);
    CHECK(std::fabs(wr - 9.03) < 1.0 && std::fabs(pr) < 1.5, "octave-band energy 2–4 kHz vs 250–500 Hz: white %+.1f dB (+9), pink %+.1f dB (0)", wr, pr);

    const size_t NF = 16384;
    std::vector<float> rm(NF), rmLeak(NF);
    // carrier on bin 341 (999 Hz), modulator on bin 102 (299 Hz)
    for (size_t i = 0; i < NF; ++i) {
        const float c = static_cast<float>(std::sin(2 * kPi * 341 * i / NF)), m = static_cast<float>(std::sin(2 * kPi * 102 * i / NF));
        rm[i] = RingMod::process(c, m, 0.f); rmLeak[i] = RingMod::process(c, m, 0.1f);
    }
    auto p0 = powerSpectrum(rm, 0, NF), p1 = powerSpectrum(rmLeak, 0, NF);
    const double sideband = p0[341 - 102] + p0[341 + 102];
    CHECK(db(p0[341] / sideband) < -100 && db(p1[341] / sideband) > -25, "carrier suppression %.0f dB balanced, %.1f dB with 10%% leak; sidebands at 700/1300 Hz",
          db(p0[341] / sideband), db(p1[341] / sideband));

    SampleHold sh; sh.prepare(SR); sh.reset(); sh.setRate(10); sh.setSlew(0); Rng r(5);
    int changes = 0; float last = sh.value();
    for (int i = 0; i < 48000 / 32; ++i) { sh.advance(32, r.bipolar()); if (sh.value() != last) ++changes; last = sh.value(); }
    CHECK(changes >= 9 && changes <= 11, "S&H at 10 Hz: %d new values per second, held between clocks", changes);

    Slew sl; sl.prepare(SR); sl.set(100, 100, Slew::Mode::Linear); sl.reset(0);
    int t = 0; while (sl.process(1.f) < 1.f && t < 96000) ++t;
    Slew se; se.prepare(SR); se.set(100, 100, Slew::Mode::Exponential); se.reset(0);
    int te = 0; while (se.process(1.f) < 0.99f && te < 96000) ++te;
    CHECK(std::abs(t - 4800) <= 2 && std::abs(te - 4800) <= 6, "slew 100 ms: linear %d samples, exponential to 99 %% %d samples", t, te);

    EnvelopeFollower ef; ef.prepare(SR); ef.set(1, 50, EnvelopeFollower::Detector::Peak, 0, 1);
    EnvelopeFollower er; er.prepare(SR); er.set(50, 50, EnvelopeFollower::Detector::Rms, 0, 1);
    float pk2 = 0, rms = 0;
    for (int i = 0; i < 48000; ++i) { const float x = 0.5f * std::sin(2 * kPiF * 200 * i / 48000.f); pk2 = ef.process(x); rms = er.process(x); }
    CHECK(std::fabs(pk2 - 0.5f) < 0.05f && std::fabs(rms - 0.3536f) < 0.02f, "follower: peak %.3f (0.5), RMS %.3f (0.354)", pk2, rms);

    VoltageProcessor vp; vp.gain1 = -0.5f; vp.gain2 = 1.f; vp.offset = 0.25f; vp.limit = 1.f;
    CHECK(vp.process(1.f, 0.5f) == 0.25f && vp.process(-10.f, 0.f) == 1.f, "voltage processor: attenuvert/sum/offset/limit");
    Mixer<2> mx; mx.ch[0] = {1.f, -1.f, false}; mx.ch[1] = {1.f, 1.f, true};
    float in[2] = {1.f, 1.f}, l, rr; mx.process(in, l, rr);
    CHECK(std::fabs(l - 1.f) < 1e-6f && std::fabs(rr + 1.f) < 1e-6f, "mixer: hard pans and phase inversion (L %.3f, R %.3f)", l, rr);
}

// ---------------------------------------------------------------------------
// Voice-level behaviour through the full kernel
// ---------------------------------------------------------------------------
struct K {
    VKKernel* k; std::vector<float> L, R; float* ch[2];
    explicit K(bool stable = true) : k(vk_kernel_create()), L(512), R(512) {
        ch[0] = L.data(); ch[1] = R.data();
        if (stable) vk_kernel_set_param(k, VK_P_GLOBAL_INSTABILITY, 0.f);
    }
    void alloc() { vk_kernel_allocate(k, SR, 2, 512); }
    ~K() { vk_kernel_destroy(k); }
    void set(uint64_t a, float v) { vk_kernel_set_param(k, a, v); }
    std::vector<float> play(int note, double secs, bool right = false) {
        VKEvent e{}; e.type = VK_EVENT_MIDI1; e.length = 3; e.data[0] = 0x90; e.data[1] = static_cast<uint8_t>(note); e.data[2] = 100;
        return render(secs, &e, right);
    }
    std::vector<float> render(double secs, const VKEvent* first = nullptr, bool right = false) {
        std::vector<float> out;
        for (int b = 0; b < static_cast<int>(secs * SR / 512); ++b) {
            vk_kernel_render(k, ch, 2, 512, b == 0 ? first : nullptr, b == 0 && first ? 1 : 0);
            out.insert(out.end(), right ? R.begin() : L.begin(), right ? R.end() : L.end());
        }
        return out;
    }
};

static void cleanPath(K& k) {   // open SVF low-pass, Modern, no envelope on filter
    k.set(VK_P_GLOBAL_ENGINE_MODE, 1); k.set(VK_P_GLOBAL_FILTER_ROUTING, 0);
    k.set(VK_P_VCFA_CUTOFF, 20000); k.set(VK_P_VCFA_KEY_TRACK, 0); k.set(VK_P_GLOBAL_STEREO_SPREAD, 0);
}

static void testVoicePitchTracking() {
    std::puts("Voice pitch tracking through the full kernel (Modern, instability 0)");
    K k; cleanPath(k); k.alloc();
    double worst = 0; int worstNote = 0;
    for (int note = 24; note <= 96; note += 12) {
        vk_kernel_post_command(k.k, {VK_CMD_PANIC, 0, 0}); k.render(0.05);
        auto x = k.play(note, 0.6);
        const double ref = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const double e = std::fabs(cents(freqOf(x, 9600), ref));
        if (e > worst) { worst = e; worstNote = note; }
    }
    CHECK(worst < 1.0, "C1…C7 worst error %.3f cents (note %d)", worst, worstNote);
}

static void testAuthenticVsModern() {
    std::puts("Authentic mode: per-voice tolerance and drift; Modern: near-stable");
    auto measure = [](bool authentic, uint32_t voiceIndex) {
        K k(false); cleanPath(k);
        k.set(VK_P_GLOBAL_ENGINE_MODE, authentic ? 0.f : 1.f); k.set(VK_P_GLOBAL_INSTABILITY, 100);
        k.alloc();
        static_cast<Kernel*>(static_cast<void*>(k.k))->testNoteOnVoice(voiceIndex, 72, 0.8f);
        auto x = k.render(1.0);
        return cents(freqOf(x, 4800), 440.0 * std::pow(2.0, 3.0 / 12.0));
    };
    double aLo = 1e9, aHi = -1e9, mLo = 1e9, mHi = -1e9;
    for (uint32_t i = 0; i < 8; ++i) {
        const double a = measure(true, i), m = measure(false, i);
        aLo = std::min(aLo, a); aHi = std::max(aHi, a); mLo = std::min(mLo, m); mHi = std::max(mHi, m);
    }
    CHECK(aHi - aLo > 2.0 && (mHi - mLo) < 0.35 * (aHi - aLo),
          "8 voices on one note: Authentic spread %.2f cents, Modern spread %.2f cents", aHi - aLo, mHi - mLo);
    const double stable = measure(true, 3);
    CHECK(std::fabs(stable - measure(true, 3)) < 1e-6, "tolerances are deterministic per voice (%.3f cents reproduced)", stable);
}

static void testVoiceFeatures() {
    std::puts("Voice features: waves, routing, glide, sync, ring, noise, stereo, DC");
    for (int w = 0; w < 6; ++w) {
        K k; cleanPath(k); k.set(VK_P_VCO1_WAVE, static_cast<float>(w)); k.alloc();
        auto x = k.play(57, 0.3);
        float pk = 0; for (size_t i = 4800; i < x.size(); ++i) pk = std::max(pk, std::fabs(x[i]));
        CHECK(pk > 0.05f && std::fabs(cents(freqOf(x, 4800), 220) ) < 2.0, "wave %d audible (peak %.2f) at 220 Hz", w, pk);
    }
    for (int r = 0; r < 4; ++r) {
        K k; k.set(VK_P_GLOBAL_FILTER_ROUTING, static_cast<float>(r)); k.alloc();
        auto x = k.play(48, 0.3);
        float pk = 0; for (float s : x) pk = std::max(pk, std::fabs(s));
        CHECK(pk > 0.05f && pk < 2.f, "routing %d audible, peak %.2f", r, pk);
    }
    {   // glide 48 → 60 over 300 ms: midway pitch lies strictly between
        K k; cleanPath(k); k.set(VK_P_GLOBAL_GLIDE, 300); k.set(VK_P_GLOBAL_POLYPHONY, 0); k.alloc();
        k.play(48, 0.4);
        auto x = k.play(60, 0.06);
        const double f = freqOf(x, 512);
        CHECK(f > 140 && f < 255, "glide: %.1f Hz 60 ms into a 130.8→261.6 Hz glide", f);
    }
    {   // sync: VCO2 only (synced to VCO1 at 220 Hz) is pitched at VCO1
        K k; cleanPath(k); k.set(VK_P_VCO1_LEVEL, 0); k.set(VK_P_VCO2_LEVEL, 100); k.set(VK_P_VCO2_SYNC_MODE, 1);
        k.set(VK_P_VCO2_COARSE, 7); k.set(VK_P_VCO2_WAVE, 2); k.alloc();
        auto x = k.play(57, 0.5);
        auto p = powerSpectrum(x, 8192, 16384);
        size_t best = 1; for (size_t b = 2; b < 400; ++b) if (p[b] > p[best]) best = b;
        const double fpk = best * SR / 16384;
        CHECK(std::fabs(fpk - 220) < 6, "hard-synced VCO2 (+7 st) strongest partial at %.1f Hz (VCO1 220 Hz)", fpk);
    }
    {   // ring + noise contribute
        K k; cleanPath(k); k.set(VK_P_VCO1_LEVEL, 0); k.set(VK_P_RINGMOD_LEVEL, 100); k.set(VK_P_VCO2_COARSE, 5); k.alloc();
        auto x = k.play(60, 0.3); float pk = 0; for (float s : x) pk = std::max(pk, std::fabs(s));
        K n; cleanPath(n); n.set(VK_P_VCO1_LEVEL, 0); n.set(VK_P_NOISE_LEVEL, 100); n.alloc();
        auto y = n.play(60, 0.3); float pn = 0; for (float s : y) pn = std::max(pn, std::fabs(s));
        CHECK(pk > 0.05f && pn > 0.05f, "ring-mod only peak %.2f, noise only peak %.2f", pk, pn);
    }
    {   // stereo spread puts alternating voices on opposite sides
        K k; cleanPath(k); k.set(VK_P_GLOBAL_STEREO_SPREAD, 100); k.alloc();
        auto l = k.play(60, 0.2, false);
        K k2; cleanPath(k2); k2.set(VK_P_GLOBAL_STEREO_SPREAD, 100); k2.alloc();
        auto r = k2.play(60, 0.2, true);
        double el = 0, er = 0; for (size_t i = 0; i < l.size(); ++i) { el += l[i] * l[i]; er += r[i] * r[i]; }
        CHECK(db(el / er) > 20, "voice 0 at full spread is %.1f dB louder on the left", db(el / er));
    }
    {   // DC: saw through low-pass
        K k; k.alloc(); auto x = k.play(45, 1.0); double m = 0;
        for (size_t i = 24000; i < x.size(); ++i) m += x[i];
        CHECK(std::fabs(m / static_cast<double>(x.size() - 24000)) < 0.01, "output DC offset %.4f", m / static_cast<double>(x.size() - 24000));
    }
}

int main() {
    testVcoPitch();
    testVcoAliasing();
    testVcoSyncAndSub();
    testSvf();
    testLadder();
    testSelfOscillationAndStability();
    testLadderVariants();
    testOversampler();
    testEnvelope();
    testVcaAndUtilities();
    testVoicePitchTracking();
    testAuthenticVsModern();
    testVoiceFeatures();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
