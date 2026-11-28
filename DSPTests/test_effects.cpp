// test_effects.cpp — Phase 6: the effect catalogue, every effect family
// measured, routing (series / parallel / sends), latency compensation,
// click-free bypass and swaps, real-time safety, fuzz and load.
#include "VekturDSP.h"
#include "Kernel.hpp"
#include "ParamTable.hpp"
#include "fx/EffectsEngine.hpp"
#include "fx/FxSpecs.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> gTrack{false};
static std::atomic<long> gAllocs{0};
void* operator new(std::size_t n) {
    if (gTrack.load(std::memory_order_relaxed)) gAllocs.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { if (gTrack.load(std::memory_order_relaxed)) gAllocs.fetch_add(1); return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return operator new(n, t); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...) do { ++gChecks; if (!(cond)) { ++gFailures; \
    std::printf("  FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("  ok   "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using namespace vektur::fx;
using vektur::Kernel; using vektur::ParameterStore; using vektur::kParamTable;
using Rng = vektur::fx::Rng;
constexpr float kPiT = 3.14159265358979f;
using vektur::fx::dbToGain;
constexpr float SR = 48000.f;
constexpr uint32_t SPAN = 32;   // the kernel's control block
using Sig = std::vector<float>;

// --------------------------------------------------------------------------
struct Rig {
    std::unique_ptr<ParameterStore> ps = std::make_unique<ParameterStore>();
    std::unique_ptr<EffectsEngine> eng = std::make_unique<EffectsEngine>();
    double beat = 0.0, tempo = 120.0; bool playing = false;
    float sr;
    explicit Rig(float rate = SR) : sr(rate) {
        for (const auto& d : kParamTable) ps->registerParam({d.address, d.minValue, d.maxValue, d.defaultValue, d.smoothingMs, d.flags});
        ps->prepare(rate);
        eng->allocate(rate, 512, *ps);
    }
    void set(uint64_t a, float v) { ps->set(a, v); ps->pollChanges(); ps->snapAll(); }
    void slotParam(uint32_t s, uint32_t idx, float v) { set(EffectsEngine::slotAddr(s, idx), v); }
    // Real-unit value for generic parameter k (0-based).
    void p(uint32_t s, uint32_t k, float real) {
        const uint32_t type = static_cast<uint32_t>(std::lround(ps->get(EffectsEngine::slotAddr(s, 0))));
        set(EffectsEngine::slotAddr(s, 5 + k), unmapValue(typeSpec(type).p[k], real));
    }
    void type(uint32_t s, uint32_t t, float mix = 100.f) {
        const TypeSpec& sp = typeSpec(t);
        ps->set(EffectsEngine::slotAddr(s, 0), static_cast<float>(t));
        ps->set(EffectsEngine::slotAddr(s, 2), mix);
        for (uint32_t i = 0; i < 12; ++i) ps->set(EffectsEngine::slotAddr(s, 5 + i), sp.p[i].name ? unmapValue(sp.p[i], sp.p[i].def) : 0.5f);
        ps->pollChanges(); ps->snapAll();
        eng->update(*ps);
        settle();
    }
    void settle() { Sig z(1024, 0.f), z2(1024, 0.f); run(z, z2); }
    void run(Sig& L, Sig& R) {
        for (size_t pos = 0; pos < L.size(); pos += SPAN) {
            const auto n = static_cast<uint32_t>(std::min<size_t>(SPAN, L.size() - pos));
            ps->pollChanges();
            FxContext c; c.sampleRate = sr; c.tempo = tempo; c.beat = beat; c.playing = playing;
            eng->process(L.data() + pos, R.data() + pos, n, *ps, c);
            ps->advance(n);
            beat += n / static_cast<double>(sr) * tempo / 60.0;
        }
    }
    // Mono in → stereo out.
    std::pair<Sig, Sig> run(const Sig& in) { Sig L = in, R = in; run(L, R); return {L, R}; }
};

static Sig impulse(size_t n, float a = 1.f) { Sig s(n, 0.f); s[0] = a; return s; }
static Sig sine(size_t n, float hz, float amp = 0.5f, float sr = SR) { Sig s(n); for (size_t i = 0; i < n; ++i) s[i] = amp * std::sin(2.f * kPiT * hz * static_cast<float>(i) / sr); return s; }
static Sig noise(size_t n, float amp = 0.3f, uint32_t seed = 1) { Rng r(seed); Sig s(n); for (auto& v : s) v = amp * r.bip(); return s; }
static float rms(const Sig& s, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b && i < s.size(); ++i) e += static_cast<double>(s[i]) * s[i]; return static_cast<float>(std::sqrt(e / std::max<size_t>(1, b - a))); }
static float peak(const Sig& s, size_t a = 0, size_t b = SIZE_MAX) { float m = 0; for (size_t i = a; i < b && i < s.size(); ++i) m = std::max(m, std::fabs(s[i])); return m; }
static size_t argmax(const Sig& s) { size_t k = 0; for (size_t i = 1; i < s.size(); ++i) if (std::fabs(s[i]) > std::fabs(s[k])) k = i; return k; }
static float db(float x) { return 20.f * std::log10(std::max(x, 1e-12f)); }
// Frequency by zero crossings with interpolation.
static double freqOf(const Sig& x, size_t a, size_t b, float sr = SR) {
    double first = -1, last = -1; int n = 0;
    for (size_t i = a + 1; i < b && i < x.size(); ++i)
        if (x[i - 1] < 0 && x[i] >= 0) { const double t = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++n; }
    return n > 1 ? (n - 1) / ((last - first) / sr) : 0.0;
}
// Gain of a sine at hz through the rig (dB), after settling.
static float sineGainDb(Rig& r, float hz) {
    auto [L, R] = r.run(sine(static_cast<size_t>(SR), hz, 0.25f));
    return db(rms(L, 24000, 48000) / (0.25f / std::sqrt(2.f)));
}
static float thd(const Sig& x, float f0, size_t a, size_t b) {   // DFT at harmonics
    auto mag = [&](double f) { std::complex<double> s{}; for (size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * std::polar(1.0, -2 * 3.141592653589793 * f * static_cast<double>(i) / SR); return std::abs(s); };
    const double h1 = mag(f0); double hs = 0; for (int k = 2; k <= 6; ++k) { const double m = mag(f0 * k); hs += m * m; }
    return static_cast<float>(std::sqrt(hs) / std::max(h1, 1e-12));
}

// --------------------------------------------------------------------------
static void testCatalogue() {
    std::puts("Catalogue, mapping and display");
    CHECK(vk_fx_type_count() == 28 && VK_PARAM_COUNT == 737, "28 effect types; %u parameters (Phase 6 added 148: 8 slots × 17, bus 12)", VK_PARAM_COUNT);
    CHECK(VK_P_FX1_TYPE == 39 * 256 && VK_P_FX8_P12 == 46 * 256 + 16 && VK_P_FXBUS_OUTPUT == 47 * 256 + 11, "slots at modules 39–46, buses at 47 (reserved blocks)");
    ParameterStore ps; for (const auto& d : kParamTable) ps.registerParam({d.address, d.minValue, d.maxValue, d.defaultValue, d.smoothingMs, d.flags});
    CHECK(!ps.isModulatable(VK_P_FX1_P1) && !ps.isModulatable(VK_P_FXBUS_SEND_A), "effects parameters are global, not per-voice matrix destinations");
    bool ok = true, round = true; int used = 0, structural = 0;
    for (uint32_t t = 1; t < FxTypeCount; ++t) {
        VKFxTypeInfo ti; ok &= vk_fx_type_info(t, &ti) && ti.name && ti.family && ti.paramCount >= 4 && ti.defaultMix > 0.f;
        for (uint32_t k = 0; k < 12; ++k) {
            VKFxParamInfo pi; vk_fx_param_info(t, k, &pi);
            if (!pi.name) continue;
            ++used; structural += pi.structural;
            ok &= pi.minValue < pi.maxValue && pi.defaultValue >= pi.minValue && pi.defaultValue <= pi.maxValue;
            const float back = vk_fx_map_value(t, k, pi.defaultNormalised);
            round &= std::fabs(back - pi.defaultValue) <= 1e-3f * std::max(1.f, std::fabs(pi.defaultValue));
        }
    }
    CHECK(ok && round, "every type: name, family, ≥ 4 parameters, defaults in range and round-trip (%d parameters, %d structural)", used, structural);
    char b[32];
    VKFxParamInfo pi; vk_fx_param_info(FxStereoDelay, 0, &pi);
    vk_fx_format_value(FxStereoDelay, 0, pi.defaultNormalised, b, 32); std::string s1 = b;
    vk_fx_param_info(FxStereoDelay, 2, &pi); vk_fx_format_value(FxStereoDelay, 2, pi.defaultNormalised, b, 32); std::string s2 = b;
    vk_fx_format_value(FxParamEQ, 4, unmapValue(typeSpec(FxParamEQ).p[4], 3.f), b, 32); std::string s3 = b;
    vk_fx_format_value(FxHall, 1, unmapValue(typeSpec(FxHall).p[1], 2.8f), b, 32); std::string s4 = b;
    CHECK(s1 == "1/8 dotted" && s2 == "375 ms" && s3 == "+3.0 dB" && s4 == "2.80 s", "display: \"%s\", \"%s\", \"%s\", \"%s\"", s1.c_str(), s2.c_str(), s3.c_str(), s4.c_str());
    CHECK(std::string(vk_fx_choice_name(FxConvolution, 0, 8)) == "User IR" && typeSpec(FxConvolution).p[0].structural, "convolution impulse choice is structural; last choice is User IR");
}

static void testPassThroughAndLatency() {
    std::puts("Empty engine, mix 0, bypass");
    Rig r;
    auto x = noise(4800); auto [L, R] = r.run(x);
    CHECK(L == x && R == x && r.eng->latency() == 0, "no effects: bit-exact pass-through, latency 0");
    Rig s; s.type(0, FxSpectral); s.slotParam(0, 2, 0.f);
    auto y = noise(20000); auto [l2, r2] = s.run(y);
    float err = 0; for (size_t i = 2048; i < y.size(); ++i) err = std::max(err, std::fabs(l2[i] - y[i - 2048]));
    CHECK(s.eng->latency() == 2048 && err < 1e-6f, "mix 0 %%: exactly the dry signal delayed by the reported latency (2048, err %.1e)", err);
    s.slotParam(0, 2, 100.f); s.slotParam(0, 1, 0.f); s.settle();
    auto [l3, r3] = s.run(y);
    err = 0; for (size_t i = 2048; i < y.size(); ++i) err = std::max(err, std::fabs(l3[i] - y[i - 2048]));
    CHECK(err < 1e-6f, "bypassed slot: latency-aligned dry (err %.1e)", err);
}

static void testDelays() {
    std::puts("Delays");
    { Rig r; r.type(0, FxStereoDelay); r.p(0, 0, 0); r.p(0, 1, 0); r.p(0, 2, 250); r.p(0, 3, 125); r.p(0, 4, 0); r.p(0, 6, 20); r.p(0, 7, 20000);
      r.settle(); r.eng->reset(); auto [L, R] = r.run(impulse(30000));
      CHECK(argmax(L) == 12000 && argmax(R) == 6000 && std::fabs(L[12000] - 1.f) < 0.02f, "stereo delay 250 / 125 ms: echoes at exactly 12000 / 6000 samples (%.3f)", L[12000]);
      r.p(0, 4, 50); r.settle(); r.eng->reset();
      Sig burst(40000, 0.f); for (size_t i = 0; i < 480; ++i) burst[i] = 0.5f * std::sin(2.f * kPiT * 1000.f * static_cast<float>(i) / SR);
      auto [L2, R2] = r.run(burst);
      const float e1 = peak(L2, 11000, 13000), e2 = peak(L2, 23000, 25000);
      CHECK(std::fabs(e2 / e1 - 0.5f) < 0.03f, "feedback 50 %%: second repeat at half level (%.3f)", e2 / e1);
      r.p(0, 0, 11); r.settle(); r.eng->reset();
      auto [L3, R3] = r.run(impulse(30000));
      CHECK(argmax(L3) == 18000, "synced 1/8 dotted at 120 BPM: 18000 samples (%zu)", argmax(L3)); }
    { Rig r; r.type(0, FxPingPong); r.p(0, 0, 0); r.p(0, 1, 100); r.p(0, 2, 60); r.p(0, 4, 20); r.p(0, 5, 20000);
      r.settle(); r.eng->reset(); auto [L, R] = r.run(impulse(24000));
      const float l1 = peak(L, 4700, 5100), r1 = peak(R, 4700, 5100), l2 = peak(L, 9500, 9900), r2 = peak(R, 9500, 9900);
      CHECK(l1 > 10 * r1 && r2 > 10 * l2, "ping-pong: first repeat left, second right (%.2f/%.3f, %.3f/%.2f)", l1, r1, l2, r2); }
    { Rig a, b; a.type(0, FxTapeEcho); b.type(0, FxTapeEcho);
      a.p(0, 1, 300); a.p(0, 2, 0); a.p(0, 4, 0); a.p(0, 5, 0); a.p(0, 8, 0); b.p(0, 1, 300); b.p(0, 2, 0); b.p(0, 4, 100); b.p(0, 5, 100); b.p(0, 8, 0);
      a.settle(); b.settle();
      auto x = sine(96000, 800.f, 0.3f);
      auto [La, Ra] = a.run(x); auto [Lb, Rb] = b.run(x);
      auto spread = [](const Sig& s) { double lo = 1e9, hi = 0; for (size_t k = 30000; k + 4800 < 96000; k += 4800) { const double f = freqOf(s, k, k + 4800); lo = std::min(lo, f); hi = std::max(hi, f); } return hi - lo; };
      const double sa = spread(La), sb = spread(Lb);
      CHECK(sa < 1.0 && sb > 4.0, "tape echo: wow & flutter move the pitch (spread %.2f Hz off → %.1f Hz on)", sa, sb); }
    { Rig a, b; a.type(0, FxBBD); b.type(0, FxBBD); a.p(0, 1, 40); b.p(0, 1, 600); a.p(0, 2, 0); b.p(0, 2, 0); a.p(0, 6, 0); b.p(0, 6, 0); a.p(0, 4, 0); b.p(0, 4, 0);
      a.settle(); b.settle();
      auto x = noise(60000, 0.3f);
      auto [La, Ra] = a.run(x); auto [Lb, Rb] = b.run(x);
      auto hf = [](const Sig& s) { double e = 0, t = 0; for (size_t i = 40000; i < s.size(); ++i) { const double d = s[i] - s[i - 1]; e += d * d; t += static_cast<double>(s[i]) * s[i]; } return e / std::max(t, 1e-12); };
      CHECK(hf(Lb) < 0.5 * hf(La), "BBD: a longer delay is darker (HF ratio %.3f vs %.3f)", hf(Lb), hf(La)); }
}

// Energy decay → RT60 (Schroeder backward integration, −5 … −35 dB fit).
static double rt60(const Sig& x, size_t start) {
    std::vector<double> e(x.size() - start);
    double acc = 0; for (size_t i = x.size(); i-- > start;) { acc += static_cast<double>(x[i]) * x[i]; e[i - start] = acc; }
    const double e0 = e[0];
    size_t a = 0, b = 0;
    for (size_t i = 0; i < e.size(); ++i) { const double d = 10 * std::log10(e[i] / e0); if (!a && d < -5) a = i; if (d < -35) { b = i; break; } }
    if (!b) return -1;
    return static_cast<double>(b - a) / SR * 60.0 / 30.0;
}

static void testReverbs() {
    std::puts("Reverbs");
    for (float target : {1.0f, 2.5f}) {
        Rig r; r.type(0, FxHall); r.p(0, 0, 0.1f); r.p(0, 1, target); r.p(0, 3, 1.f); r.p(0, 4, 1.f); r.p(0, 8, 0); r.p(0, 6, 0); r.settle();
        auto [L, R] = r.run(impulse(static_cast<size_t>(SR * 6)));
        const double t = rt60(L, 2400);
        CHECK(std::fabs(t / target - 1.0) < 0.25, "hall: RT60 %.2f s for %.1f s set (flat decay)", t, target);
    }
    { Rig r; r.type(0, FxRoom); r.p(0, 1, 0.6f); r.p(0, 3, 0); r.p(0, 4, 0); r.settle();
      auto [L, R] = r.run(impulse(static_cast<size_t>(SR * 3)));
      const double t = rt60(L, 480);
      double num = 0, dl = 0, dr = 0; for (size_t i = 2400; i < 48000; ++i) { num += static_cast<double>(L[i]) * R[i]; dl += static_cast<double>(L[i]) * L[i]; dr += static_cast<double>(R[i]) * R[i]; }
      const double corr = num / std::sqrt(dl * dr);
      CHECK(t > 0.35 && t < 0.9 && std::fabs(corr) < 0.5, "room: RT60 %.2f s (0.6 set, damped), L/R correlation %.2f", t, corr); }
    { Rig r; r.type(0, FxHall); r.p(0, 1, 2.f); r.p(0, 3, 0.2f); r.settle();
      auto [L, R] = r.run(noise(static_cast<size_t>(SR * 4), 0.3f, 3));
      Sig tail = L; (void)tail;
      // High decay 0.2×: after the input stops the late tail is darker than the input.
      Sig z(static_cast<size_t>(SR * 1.5), 0.f); Sig zl = z, zr = z; r.run(zl, zr);
      double hf = 0, all = 0; for (size_t i = 4800; i < 24000; ++i) { const double d = zl[i] - zl[i - 1]; hf += d * d; all += static_cast<double>(zl[i]) * zl[i]; }
      CHECK(hf / all < 0.1, "hall High Decay 0.2×: dark tail (HF ratio %.3f; white noise ≈ 2)", hf / all); }
    for (uint32_t t : {FxPlate, FxHall, FxRoom, FxSpring}) {
        Rig r; r.type(0, t);
        // Maximum decay / size / feedback.
        for (uint32_t k = 0; k < 12; ++k) if (typeSpec(t).p[k].name && typeSpec(t).p[k].curve != Curve::Choice) r.slotParam(0, 5 + k, 1.f);
        r.settle();
        Sig x = noise(static_cast<size_t>(SR * 2), 0.5f, 9); x.resize(static_cast<size_t>(SR * 12), 0.f);
        auto [L, R] = r.run(x);
        bool finite = true; for (float v : L) finite &= std::isfinite(v);
        CHECK(finite && peak(L) < 8.f, "%s at maximum settings: stable over 12 s (peak %.2f)", typeSpec(t).name, peak(L));
    }
    { Rig r; r.type(0, FxPlate); r.settle();
      auto [L, R] = r.run(impulse(static_cast<size_t>(SR * 10)));
      CHECK(db(peak(L, static_cast<size_t>(SR * 8), static_cast<size_t>(SR * 10))) < -80.f && peak(L, 2000, 20000) > 1e-3f, "plate (default) rings then decays below −80 dB by 8 s"); }
    { Rig r; r.type(0, FxSpring); r.settle();
      auto [L, R] = r.run(impulse(static_cast<size_t>(SR * 2)));
      // Dispersion: high frequencies arrive before lows in the first echo (chirp).
      double tl = 0, th = 0, wl = 0, wh = 0; OnePole lp; lp.set(500.f, SR);
      for (size_t i = 0; i < 4000; ++i) { const float lo = lp.lp(L[i]), hi = L[i] - lo; tl += i * lo * lo; wl += lo * lo; th += i * hi * hi; wh += hi * hi; }
      CHECK(th / wh < tl / wl, "spring: highs arrive before lows (dispersion chirp: %.0f vs %.0f samples)", th / wh, tl / wl); }
}

static void testConvolution() {
    std::puts("Convolution (partitioned FFT)");
    Rig r;
    std::vector<float> irL(5000), irR(5000); Rng g(5);
    for (size_t i = 0; i < irL.size(); ++i) { irL[i] = g.bip() * std::exp(-static_cast<float>(i) / 1500.f); irR[i] = g.bip() * std::exp(-static_cast<float>(i) / 900.f); }
    r.eng->setUserIR(0, irL.data(), irR.data(), 5000, SR);
    r.type(0, FxConvolution); r.p(0, 0, 8); r.p(0, 1, 0.1f); r.eng->update(*r.ps); r.settle();
    auto x = noise(20000, 0.3f, 11);
    Sig L = x, R = x; r.run(L, R);
    double err = 0, ref = 0;
    for (size_t t = 256 + 6000; t < 20000; t += 7) {
        double yl = 0; for (size_t k = 0; k < irL.size() && k <= t - 256; ++k) yl += static_cast<double>(irL[k]) * x[t - 256 - k];
        err = std::max(err, std::fabs(yl - L[t])); ref = std::max(ref, std::fabs(yl));
    }
    CHECK(r.eng->latency() == 256 && err < 1e-4 * std::max(1.0, ref), "user IR (5000 taps): equals direct convolution delayed by the reported 256 samples (max err %.1e)", err);
    bool ok = true;
    for (uint32_t i = 0; i < kBuiltInIRCount; ++i) {
        const ImpulseResponse ir = builtInImpulse(i, SR);
        double e = 0; for (float v : ir.left) e += static_cast<double>(v) * v;
        ok &= !ir.left.empty() && ir.left.size() <= static_cast<size_t>(3 * SR) && std::fabs(std::sqrt(e) - 0.5) < 1e-3;
    }
    CHECK(ok, "8 built-in IRs: deterministic, ≤ 3 s, energy-normalised");
    std::vector<float> d96(9600, 0.f); d96[0] = 1.f;
    r.eng->setUserIR(1, d96.data(), nullptr, 9600, 96000.f);
    CHECK(r.eng->userIRFrames(1) == 4800, "a 96 kHz IR is resampled to the 48 kHz session (%u frames)", r.eng->userIRFrames(1));
}

static void testEQ() {
    std::puts("EQs");
    Rig r; r.type(0, FxParamEQ); r.p(0, 3, 1000); r.p(0, 4, 12); r.p(0, 5, 1.f); r.settle();
    const float g1k = sineGainDb(r, 1000), g100 = sineGainDb(r, 100), g10k = sineGainDb(r, 10000);
    CHECK(std::fabs(g1k - 12.f) < 0.3f && std::fabs(g100) < 0.5f && std::fabs(g10k) < 0.7f, "peak +12 dB at 1 kHz: %.2f dB (100 Hz %.2f, 10 kHz %.2f)", g1k, g100, g10k);
    r.p(0, 4, 0); r.p(0, 0, 200); r.settle();
    const float hp = sineGainDb(r, 200), hp50 = sineGainDb(r, 50);
    CHECK(std::fabs(hp + 3.f) < 0.5f && hp50 < -20.f, "low cut 200 Hz: −3 dB at 200 (%.2f), %.1f dB at 50 Hz", hp, hp50);
    r.p(0, 0, 10); r.p(0, 9, 4000); r.p(0, 10, -9); r.settle();
    const float hs = sineGainDb(r, 15000);
    CHECK(std::fabs(hs + 9.f) < 1.f, "high shelf −9 dB: %.2f dB at 15 kHz", hs);
    Rig gq; gq.type(0, FxGraphicEQ); gq.p(0, 5, 6); gq.settle();
    const float b1k = sineGainDb(gq, 1000), b250 = sineGainDb(gq, 250);
    CHECK(std::fabs(b1k - 6.f) < 0.3f && b250 < 1.f, "graphic 1 kHz +6: %.2f dB (250 Hz %.2f)", b1k, b250);
}

static void testSpectralGranular() {
    std::puts("Spectral & granular");
    { Rig r; r.type(0, FxSpectral); r.settle();
      auto x = noise(30000, 0.3f, 4); auto [L, R] = r.run(x);
      float err = 0; for (size_t i = 4096; i < x.size(); ++i) err = std::max(err, std::fabs(L[i] - x[i - 2048]));
      CHECK(err < 2e-5f, "neutral spectral frame resynthesis is transparent (latency 2048, err %.1e)", err); }
    { Rig r; r.type(0, FxSpectral); r.p(0, 2, 12); r.settle();
      auto [L, R] = r.run(sine(96000, 440.f, 0.3f));
      const double f = freqOf(L, 48000, 96000);
      CHECK(std::fabs(f / 880.0 - 1.0) < 0.02, "shift +12 st: 440 Hz → %.1f Hz", f); }
    { Rig r; r.type(0, FxSpectral);
      auto x = sine(48000, 660.f, 0.3f); auto [L, R] = r.run(x);
      r.p(0, 0, 1); Sig a = sine(9600, 660.f, 0.3f); Sig b = a; r.run(a, b);
      Sig z(96000, 0.f), z2 = z; r.run(z, z2);
      CHECK(rms(z, 48000, 96000) > 0.1f && std::fabs(freqOf(z, 48000, 96000) / 660.0 - 1.0) < 0.02, "freeze: the frozen spectrum sustains 2 s after the input stops (%.3f rms)", rms(z, 48000, 96000)); }
    { Rig r; r.type(0, FxGranular); r.p(0, 2, 12); r.p(0, 3, 0); r.p(0, 0, 60); r.p(0, 1, 40); r.p(0, 6, 0); r.p(0, 5, 0); r.settle();
      auto [L, R] = r.run(sine(96000, 300.f, 0.3f));
      // Dominant frequency by DFT peak search.
      double best = 0, bf = 0;
      for (double f = 200; f < 900; f += 1) { std::complex<double> s{}; for (size_t i = 48000; i < 96000; ++i) s += static_cast<double>(L[i]) * std::polar(1.0, -2 * 3.141592653589793 * f * static_cast<double>(i) / SR); if (std::abs(s) > best) { best = std::abs(s); bf = f; } }
      CHECK(rms(L, 48000, 96000) > 0.03f && std::fabs(bf - 600) < 12, "granular pitch +12: 300 Hz → %.0f Hz grains", bf); }
    { Rig r; r.type(0, FxGranular); r.p(0, 7, 0); auto x = noise(48000, 0.3f); auto [L, R] = r.run(x);
      r.p(0, 7, 1); Sig z(96000, 0.f), z2 = z; r.run(z, z2);
      CHECK(rms(z, 48000, 96000) > 0.02f, "granular freeze keeps playing the captured audio (%.3f rms)", rms(z, 48000, 96000)); }
}

static void testCharacter() {
    std::puts("Tape, lo-fi, amp, cabinet, stutter");
    { Rig a, b; a.type(0, FxTape); b.type(0, FxTape); a.p(0, 0, 0); b.p(0, 0, 18); for (Rig* r : {&a, &b}) { r->p(0, 3, 0); r->p(0, 4, 0); r->p(0, 7, 0); r->settle(); }
      auto [La, Ra] = a.run(sine(48000, 200.f, 0.5f)); auto [Lb, Rb] = b.run(sine(48000, 200.f, 0.5f));
      const float t0 = thd(La, 200, 24000, 48000), t1 = thd(Lb, 200, 24000, 48000);
      CHECK(a.eng->latency() == 240 && t1 > 5 * t0 && t1 > 0.03f, "tape: drive 18 dB raises THD %.3f → %.3f; wow/flutter centre reported as 240 samples latency", t0, t1); }
    { Rig r; r.type(0, FxLoFi); r.p(0, 0, 3); r.p(0, 1, 48000); r.p(0, 4, 0); r.settle();
      auto [L, R] = r.run(sine(4800, 100.f, 0.9f));
      std::set<int> levels; for (size_t i = 1000; i < 4800; ++i) levels.insert(static_cast<int>(std::lround(L[i] * 1000)));
      Rig s; s.type(0, FxLoFi); s.p(0, 0, 16); s.p(0, 1, 4000); s.p(0, 4, 0); s.settle();
      auto [L2, R2] = s.run(sine(4800, 100.f, 0.5f));
      int runs = 0; for (size_t i = 1001; i < 4800; ++i) runs += L2[i] != L2[i - 1];
      CHECK(levels.size() <= 9 && runs > 300 && runs < 450, "lo-fi: 3 bits → %zu levels; 4 kHz rate → %d changes in 3800 samples (≈ 317)", levels.size(), runs); }
    { Rig a, b; a.type(0, FxAmp); b.type(0, FxAmp); a.p(0, 1, 5); b.p(0, 1, 90);
      a.p(0, 6, 0); b.p(0, 6, 0); a.settle(); b.settle();
      auto [La, Ra] = a.run(sine(48000, 220.f, 0.3f)); auto [Lb, Rb] = b.run(sine(48000, 220.f, 0.3f));
      bool fin = true; for (float v : Lb) fin &= std::isfinite(v);
      CHECK(fin && thd(Lb, 220, 24000, 48000) > 4 * thd(La, 220, 24000, 48000) && peak(Lb) < 4.f, "amp: gain 90 %% distorts much more than 5 %% (THD %.3f vs %.3f), bounded", thd(Lb, 220, 24000, 48000), thd(La, 220, 24000, 48000)); }
    { Rig r; r.type(0, FxCabinet); r.settle();
      const float g1 = sineGainDb(r, 1000), g10 = sineGainDb(r, 10000), g40 = sineGainDb(r, 40);
      CHECK(g10 < g1 - 20.f && g40 < g1 - 6.f, "4×12 cabinet: band-limited speaker response (1 kHz %.1f, 10 kHz %.1f, 40 Hz %.1f dB)", g1, g10, g40); }
    { auto x = noise(48000, 0.3f, 21);
      float maxErr = 0;
      Rig q; q.type(0, FxStutter); q.p(0, 0, 2); q.p(0, 1, 100); q.p(0, 2, 0); q.p(0, 3, 100);
      q.beat = 0.0; q.eng->reset();                 // start exactly on a grid line
      Sig L2 = x, R2 = x; q.run(L2, R2);
      for (size_t i = 6200; i < 11800; ++i) maxErr = std::max(maxErr, std::fabs(L2[i] - x[i - 6000]));
      bool repeats = maxErr < 1e-4f;
      CHECK(repeats && std::fabs(L2[100] - x[100]) < 1e-6f, "stutter 1/16 on a 1/4 grid: the first slice repeats sample-exactly (err %.1e)", maxErr); }
}

static void testModulation() {
    std::puts("Modulation effects");
    auto variation = [](const Sig& s) { float lo = 1e9f, hi = 0; for (size_t k = 9600; k + 960 < s.size(); k += 960) { const float v = rms(s, k, k + 960); lo = std::min(lo, v); hi = std::max(hi, v); } return db(hi) - db(lo); };
    { Rig r; r.type(0, FxPhaser, 50.f); r.p(0, 1, 1.f); r.p(0, 3, 100); r.p(0, 5, 70); r.settle();
      auto [L, R] = r.run(sine(96000, 1000.f, 0.3f));
      CHECK(variation(L) > 10.f, "phaser: moving notches sweep through a 1 kHz tone (%.1f dB swing)", variation(L)); }
    { Rig r; r.type(0, FxFlanger, 50.f); r.p(0, 0, 1.f); r.p(0, 4, 80); r.settle();
      auto [L, R] = r.run(sine(96000, 2000.f, 0.3f));
      CHECK(variation(L) > 6.f, "flanger: comb sweep (%.1f dB swing at 2 kHz)", variation(L)); }
    { Rig r; r.type(0, FxChorus); r.settle();
      auto [L, R] = r.run(sine(96000, 500.f, 0.3f));
      double lo = 1e9, hi = 0; for (size_t k = 20000; k + 4800 < 96000; k += 4800) { const double f = freqOf(L, k, k + 4800); lo = std::min(lo, f); hi = std::max(hi, f); }
      CHECK(hi - lo > 0.5 && rms(L, 20000, 96000) > 0.1f, "chorus: modulated delay detunes the voice (±%.2f Hz)", (hi - lo) / 2); }
    { Rig r; r.type(0, FxEnsemble); r.settle(); auto [L, R] = r.run(sine(48000, 440.f, 0.3f));
      double num = 0, dl = 0, dr = 0; for (size_t i = 10000; i < 48000; ++i) { num += static_cast<double>(L[i]) * R[i]; dl += static_cast<double>(L[i]) * L[i]; dr += static_cast<double>(R[i]) * R[i]; }
      CHECK(rms(L, 10000, 48000) > 0.1f && num / std::sqrt(dl * dr) < 0.98, "ensemble: three-phase taps widen the image (L/R corr %.2f)", num / std::sqrt(dl * dr)); }
    { Rig r; r.type(0, FxTremolo); r.p(0, 1, 5.f); r.p(0, 3, 100); r.settle();
      auto [L, R] = r.run(Sig(48000, 0.5f));
      CHECK(*std::min_element(L.begin() + 1000, L.end()) < 0.01f && *std::max_element(L.begin() + 1000, L.end()) > 0.49f, "tremolo 100 %%: full-depth gain swing");
      Rig p; p.type(0, FxTremolo); p.p(0, 0, 1); p.p(0, 1, 2.f); p.p(0, 3, 100); p.settle();
      auto [L2, R2] = p.run(Sig(48000, 0.5f));
      double c = 0; for (size_t i = 1000; i < 48000; ++i) c += (L2[i] - 0.25) * (R2[i] - 0.25);
      CHECK(c < 0, "auto-pan: left and right gains move in opposition"); }
    { Rig r; r.type(0, FxTremolo); r.p(0, 2, 7); r.p(0, 3, 100); r.p(0, 4, 2); r.p(0, 6, 0.1f); r.playing = true; r.beat = 0; r.tempo = 120;
      auto [L, R] = r.run(Sig(24000, 0.5f));
      // 1/16 at 120 BPM = 6000 samples per cycle; square: high for the first half of each cycle from beat 0.
      CHECK(L[700] > 0.45f && L[4200] < 0.05f && L[6700] > 0.45f, "tremolo synced 1/16 (6000 samples at 120 BPM): phase locked to the song position"); }
}

static void testDynamics() {
    std::puts("Dynamics & width");
    { Rig r; r.type(0, FxCompressor); r.p(0, 0, -24); r.p(0, 1, 4); r.p(0, 4, 0); r.p(0, 2, 1); r.p(0, 3, 50); r.settle();
      auto [L, R] = r.run(sine(48000, 1000.f, 0.5f));   // −6 dBFS peak
      const float out = db(peak(L, 24000, 48000));
      CHECK(std::fabs(out - (-24.f + 18.f / 4.f)) < 0.6f, "compressor −24 dB / 4:1: −6 dBFS in → %.2f dBFS out (expect −19.5)", out);
      CHECK(r.eng->meter(0) < -13.f, "gain-reduction meter %.1f dB", r.eng->meter(0)); }
    { Rig r; r.type(0, FxLimiter); r.p(0, 0, 18); r.p(0, 1, -1); r.settle();
      auto x = noise(96000, 0.5f, 7); for (size_t i = 0; i < x.size(); i += 9001) x[i] = 1.f;
      auto [L, R] = r.run(x);
      const float ceil = dbToGain(vk_fx_map_value(FxLimiter, 1, r.ps->get(EffectsEngine::slotAddr(0, 6))));   // −1 dB as stored
      CHECK(peak(L) <= ceil + 1e-6f && peak(R) <= ceil + 1e-6f && r.eng->latency() == 144, "limiter +18 dB into −1 dB ceiling: peak %.5f ≤ %.5f, look-ahead latency 144", peak(L), ceil);
      // Transparent below the ceiling: aligned, unity.
      Rig q; q.type(0, FxLimiter); q.settle();
      auto y = noise(20000, 0.1f, 8); auto [L2, R2] = q.run(y);
      float err = 0; for (size_t i = 200; i < y.size(); ++i) err = std::max(err, std::fabs(L2[i] - y[i - 144]));
      CHECK(err < 1e-6f, "limiter below the ceiling: unity and latency-exact (err %.1e)", err); }
    { Rig r; r.type(0, FxGate); r.p(0, 0, -40); r.p(0, 1, -80); r.settle();
      auto [L, R] = r.run(sine(48000, 300.f, 0.001f));   // −60 dBFS
      auto [L2, R2] = r.run(sine(48000, 300.f, 0.3f));
      CHECK(db(rms(L, 24000, 48000) / (0.001f / std::sqrt(2.f))) < -60.f && std::fabs(db(rms(L2, 24000, 48000) / (0.3f / std::sqrt(2.f)))) < 0.2f,
            "gate −40 dB: a −60 dBFS tone is closed, a −10 dBFS tone passes at unity"); }
    { Rig r; r.type(0, FxWidth); r.p(0, 0, 0); r.settle();
      Sig L = noise(4800, 0.3f, 1), R = noise(4800, 0.3f, 2); r.run(L, R);
      bool mono = true; for (size_t i = 0; i < L.size(); ++i) mono &= std::fabs(L[i] - R[i]) < 1e-6f;
      Rig b; b.type(0, FxWidth); b.p(0, 3, 200); b.settle();
      Sig l2 = sine(48000, 60.f, 0.3f), r2 = l2; for (auto& v : r2) v = -v; b.run(l2, r2);
      CHECK(mono && rms(l2, 24000, 48000) < 0.02f, "width 0 %% folds to mono; bass mono removes a 60 Hz side signal (%.3f)", rms(l2, 24000, 48000)); }
}

static void testRouting() {
    std::puts("Routing: series, parallel, sends, latency compensation");
    { Rig r; for (uint32_t s : {0u, 1u}) { r.type(s, FxParamEQ); r.p(s, 3, 1000); r.p(s, 4, 6); } r.settle();
      const float series = sineGainDb(r, 1000);
      r.set(EffectsEngine::kBusBase + 1, 1); r.settle();
      const float par = sineGainDb(r, 1000);
      CHECK(std::fabs(series - 12.f) < 0.4f && std::fabs(par - 6.f) < 0.3f, "two +6 dB EQs: series %.2f dB, parallel (averaged) %.2f dB", series, par); }
    { Rig r; r.type(0, FxLimiter); r.type(1, FxConvolution);
      std::vector<float> delta(64, 0.f); delta[0] = 2.f;   // IR energy 4 → output ×2
      r.eng->setUserIR(1, delta.data(), nullptr, 64, SR); r.p(1, 0, 8); r.p(1, 1, 0.1f); r.eng->update(*r.ps); r.settle();
      r.slotParam(1, 4, 1);                                 // slot 2 on send A (post inserts)
      r.set(EffectsEngine::kBusBase + 4, 100); r.settle(); r.settle();
      auto [L, R] = r.run(impulse(2000, 0.25f));
      int spikes = 0; for (float v : L) spikes += std::fabs(v) > 1e-3f;
      CHECK(r.eng->latency() == 144 + 256 && argmax(L) == 400 && spikes == 1 && std::fabs(L[400] - 0.75f) < 1e-3f,
            "insert limiter (144) + send A convolution (256) post-insert: total latency %u, dry and return arrive together at %zu (%.3f = 0.25 + 2 × 0.25)",
            r.eng->latency(), argmax(L), L[400]);
      r.set(EffectsEngine::kBusBase + 6, 0); r.settle(); r.settle();   // pre-insert tap
      auto [L2, R2] = r.run(impulse(2000, 0.25f));
      spikes = 0; for (float v : L2) spikes += std::fabs(v) > 1e-3f;
      CHECK(r.eng->latency() == 256 && spikes == 1 && argmax(L2) == 256, "send A pre-insert: paths realigned to the new total (%u)", r.eng->latency()); }
    { Rig r; r.type(0, FxLimiter); r.type(1, FxParamEQ); r.set(EffectsEngine::kBusBase + 1, 1); r.settle(); r.settle();
      auto [L, R] = r.run(impulse(1000, 0.25f));
      int spikes = 0; for (float v : L) spikes += std::fabs(v) > 1e-4f;
      CHECK(r.eng->latency() == 144 && spikes == 1 && argmax(L) == 144 && std::fabs(L[144] - 0.25f) < 1e-4f, "parallel chain: zero-latency EQ delayed to match the limiter (single aligned spike)"); }
    { Rig r; r.type(0, FxHall); r.slotParam(0, 4, 2); r.set(EffectsEngine::kBusBase + 5, 100); r.set(EffectsEngine::kBusBase + 10, -60); r.settle(); r.settle();
      auto [L, R] = r.run(impulse(48000));
      CHECK(std::fabs(L[0]) < 1e-6f && rms(L, 1000, 48000) > 1e-4f, "dry at −∞, send B → hall: only the reverb return is heard"); }
}

static void testBypassAndSwaps() {
    std::puts("Click-free bypass and type swaps");
    Rig r; r.type(0, FxStereoDelay, 50.f); r.p(0, 0, 0); r.p(0, 2, 10); r.p(0, 3, 10); r.p(0, 4, 0);
    auto x = sine(48000, 200.f, 0.4f);
    Sig L = x, R = x;
    float maxStep = 0;
    for (size_t pos = 0; pos < L.size(); pos += 4800) {
        if (pos == 9600) { r.ps->set(EffectsEngine::slotAddr(0, 1), 0.f); }
        if (pos == 24000) { r.ps->set(EffectsEngine::slotAddr(0, 1), 1.f); }
        if (pos == 33600) { r.ps->set(EffectsEngine::slotAddr(0, 0), FxChorus); r.eng->update(*r.ps); }
        Sig a(L.begin() + static_cast<long>(pos), L.begin() + static_cast<long>(pos + 4800)), b = a;
        r.run(a, b);
        std::copy(a.begin(), a.end(), L.begin() + static_cast<long>(pos));
    }
    for (size_t i = 1; i < L.size(); ++i) maxStep = std::max(maxStep, std::fabs(L[i] - L[i - 1]));
    const float sineStep = 0.4f * 2.f * kPiT * 200.f / SR;
    CHECK(maxStep < 2.5f * sineStep, "bypass on/off and a delay → chorus swap: largest sample step %.4f (sine alone %.4f)", maxStep, sineStep);
    CHECK(r.eng->activeType(0) == FxChorus, "swapped instance is running");
    // Retired instances are freed on the next update (not on the audio thread).
    gAllocs = 0;
    for (int k = 0; k < 6; ++k) { r.ps->set(EffectsEngine::slotAddr(0, 0), k & 1 ? FxHall : FxSpectral); r.eng->update(*r.ps); r.settle(); }
    CHECK(r.eng->activeType(0) == FxHall, "rapid type changes settle on the last choice");
}

static void testRealtime() {
    std::puts("Real-time safety, robustness, load");
    Rig r;
    const uint32_t heavy[8] = {FxConvolution, FxHall, FxSpectral, FxGranular, FxTapeEcho, FxAmp, FxPlate, FxLimiter};
    for (uint32_t s = 0; s < 8; ++s) r.type(s, heavy[s], 40.f);
    r.slotParam(1, 4, 1); r.slotParam(6, 4, 2);
    r.settle();
    auto x = noise(SPAN, 0.2f);
    std::vector<float> L(SPAN), R(SPAN);
    gAllocs = 0; gTrack = true;
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    const int spans = 6000;
    for (int b = 0; b < spans; ++b) {
        std::copy(x.begin(), x.end(), L.begin()); std::copy(x.begin(), x.end(), R.begin());
        r.ps->pollChanges();
        FxContext c; c.tempo = 128; c.beat = b * 0.0083;
        r.eng->process(L.data(), R.data(), SPAN, *r.ps, c);
        r.ps->advance(SPAN);
        if (b % 500 == 0) r.ps->set(EffectsEngine::slotAddr(3, 5), static_cast<float>(b % 1000) / 1000.f);
    }
    const double secs = std::chrono::duration<double>(clk::now() - t0).count();
    gTrack = false;
    CHECK(gAllocs == 0, "zero allocations: 8 heavy slots (convolution, hall, spectral, granular, tape echo, amp, plate, limiter) on all three buses");
    const double load = secs / (spans * SPAN / static_cast<double>(SR)) * 100.0;
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    const double budget = 2000.0;
#else
    const double budget = 60.0;
#endif
    CHECK(load < budget, "8 heavy slots together: %.1f %% of one core (host build, scalar FFT)", load);

    // Per-type cost table (default settings, noise input).
    std::printf("    per-effect load at 48 kHz (%% of one core):");
    for (uint32_t t = 1; t < FxTypeCount; ++t) {
        Rig q; q.type(0, t);
        Sig a = noise(48000, 0.2f), b2 = a;
        const auto s0 = clk::now(); q.run(a, b2); const double el = std::chrono::duration<double>(clk::now() - s0).count();
        std::printf("%s%s %.2f", t % 4 == 1 ? "\n      " : "  ", typeSpec(t).name, el * 100.0);
    }
    std::printf("\n");

    // NaN input: the slot recovers on its own.
    Rig n; n.type(0, FxHall);
    Sig bad(256, 0.f); bad[10] = std::nanf(""); Sig bad2 = bad; n.run(bad, bad2);
    auto [L2, R2] = n.run(noise(4800, 0.1f));
    bool fin = true; for (float v : L2) fin &= std::isfinite(v);
    CHECK(n.eng->nonFinite() && fin, "NaN input: slot resets, flags it once, output finite again");

    // Fuzz every type with random settings: always finite and bounded.
    Rng g(99); int bad3 = 0; float worst = 0;
    for (uint32_t t = 1; t < FxTypeCount; ++t)
        for (int trial = 0; trial < 6; ++trial) {
            Rig q; q.type(0, t);
            for (uint32_t k = 0; k < 12; ++k) q.slotParam(0, 5 + k, g.uni());
            q.eng->update(*q.ps); q.settle();
            Sig a = noise(24000, 0.5f, static_cast<uint32_t>(trial + 1)); a.resize(48000, 0.f); Sig b2 = a;
            q.run(a, b2);
            for (float v : a) { if (!std::isfinite(v)) { ++bad3; break; } worst = std::max(worst, std::fabs(v)); }
        }
    CHECK(bad3 == 0 && worst < 30.f, "27 types × 6 random settings: all finite, peak %.2f", worst);

    // Other sample rates.
    bool rates = true;
    for (float sr : {44100.f, 96000.f}) {
        Rig q(sr);
        for (uint32_t t = 1; t < FxTypeCount; ++t) { q.type(0, t); Sig a = noise(4096, 0.2f), b2 = a; q.run(a, b2); for (float v : a) rates &= std::isfinite(v); }
        q.type(0, FxLimiter); rates &= q.eng->latency() == static_cast<uint32_t>(std::lround(0.003f * sr));
    }
    CHECK(rates, "every type builds and runs at 44.1 and 96 kHz; latencies scale with the rate");

    // UI thread rebuilding while audio runs (TSan).
    Rig t; t.type(0, FxStereoDelay);
    std::atomic<bool> stop{false};
    std::thread ui([&] { int k = 0; while (!stop) { t.ps->set(EffectsEngine::slotAddr(0, 0), (k++ & 1) ? FxChorus : FxPhaser); t.eng->update(*t.ps); std::this_thread::sleep_for(std::chrono::milliseconds(2)); } });
    std::vector<float> a(SPAN), b2(SPAN);
    for (int k = 0; k < 20000; ++k) { std::fill(a.begin(), a.end(), 0.1f); std::fill(b2.begin(), b2.end(), 0.1f); FxContext c; t.eng->process(a.data(), b2.data(), SPAN, *t.ps, c); }
    stop = true; ui.join(); t.eng->update(*t.ps);
    CHECK(true, "concurrent rebuilds from a UI thread while rendering (checked under TSan)");
}

static void testKernel() {
    std::puts("Kernel integration");
    auto k = std::make_unique<Kernel>();
    k->allocate(SR, 2, 512);
    vk_kernel_set_effect_type(reinterpret_cast<VKKernel*>(k.get()), 0, FxHall);
    vk_kernel_set_effect_type(reinterpret_cast<VKKernel*>(k.get()), 1, FxLimiter);
    std::vector<float> L(256), R(256); float* ch[2] = {L.data(), R.data()};
    VKEvent on{}; on.type = VK_EVENT_MIDI1; on.length = 3; on.data[0] = 0x90; on.data[1] = 60; on.data[2] = 100;
    VKEvent off = on; off.data[0] = 0x80;
    for (int b = 0; b < 10; ++b) k->render(ch, 2, 256, nullptr, 0);
    gAllocs = 0; gTrack = true;
    float tailPeak = 0;
    for (int b = 0; b < 600; ++b) {
        k->render(ch, 2, 256, b == 0 ? &on : (b == 40 ? &off : nullptr), (b == 0 || b == 40) ? 1 : 0);
        if (b > 120 && b < 200) tailPeak = std::max(tailPeak, peak(L));
    }
    gTrack = false;
    CHECK(gAllocs == 0 && vk_kernel_effects_latency(reinterpret_cast<VKKernel*>(k.get())) == 144, "kernel: note through hall + limiter, zero allocations, latency %u reported", vk_kernel_effects_latency(reinterpret_cast<VKKernel*>(k.get())));
    CHECK(tailPeak > 1e-3f, "reverb tail continues after the voice released (%.4f)", tailPeak);
    CHECK(vk_kernel_effect_active_type(reinterpret_cast<VKKernel*>(k.get()), 0) == FxHall, "active-type readout");
    k->allocate(96000, 2, 512);
    CHECK(vk_kernel_effect_active_type(reinterpret_cast<VKKernel*>(k.get()), 0) == FxHall && vk_kernel_effect_active_type(reinterpret_cast<VKKernel*>(k.get()), 1) == FxLimiter,
          "re-allocation at 96 kHz rebuilds the slots synchronously");
    for (int b = 0; b < 40; ++b) k->render(ch, 2, 256, nullptr, 0);
    CHECK(vk_kernel_effects_latency(reinterpret_cast<VKKernel*>(k.get())) == 288, "96 kHz: limiter look-ahead now 288 samples");
}

int main() {
    testCatalogue();
    testPassThroughAndLatency();
    testDelays();
    testReverbs();
    testConvolution();
    testEQ();
    testSpectralGranular();
    testCharacter();
    testModulation();
    testDynamics();
    testRouting();
    testBypassAndSwaps();
    testRealtime();
    testKernel();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
