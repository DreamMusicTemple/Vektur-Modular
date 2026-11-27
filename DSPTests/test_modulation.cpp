// test_modulation.cpp — Phase 4A: modulation matrix, LFOs, controller
// sources, macros, voice modes (mono / legato / unison), glide modes, bend
// ranges and the real-time safety of all of it.
#include "VekturDSP.h"
#include "Kernel.hpp"
#include "ParamTable.hpp"
#include "ChordAnalyzer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
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

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...) do { ++gChecks; if (!(cond)) { ++gFailures; \
    std::printf("  FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("  ok   "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using namespace vektur;
constexpr double SR = 48000.0;
constexpr uint32_t BLK = 256;
constexpr uint64_t PERF = VK_P_GLOBAL_VOICE_MODE, MATRIX = 60 * 256, LFO1 = 26 * 256, MACRO = 29 * 256;

// --------------------------------------------------------------------------
struct Rig {
    std::unique_ptr<Kernel> k = std::make_unique<Kernel>();
    std::vector<float> L = std::vector<float>(BLK), R = std::vector<float>(BLK);
    std::vector<VKEvent> pending;
    std::vector<float> out;   // mono capture (L)
    double hostTempo = 0, hostBeat = 0; bool hostPlay = false;   // host transport (tempo 0 = none)
    Rig() { k->allocate(SR, 2, 4096); pending.reserve(512); out.reserve(static_cast<size_t>(SR * 12)); }
    void set(uint64_t a, float v) { k->setParam(a, v); }
    void slot(uint32_t s, int src, uint64_t dest, float amountPct, float offsetPct = 0, int via = 0) {
        const uint64_t b = MATRIX + s * 16;
        set(b + 0, 1); set(b + 1, static_cast<float>(src)); set(b + 2, static_cast<float>(via));
        set(b + 3, static_cast<float>(dest)); set(b + 4, amountPct); set(b + 5, offsetPct);
    }
    void midi(uint8_t a, uint8_t b, uint8_t c) {
        VKEvent e{}; e.type = VK_EVENT_MIDI1; e.length = 3; e.data[0] = a; e.data[1] = b; e.data[2] = c;
        pending.push_back(e);
    }
    void on(int n, int vel = 100, int ch = 0) { midi(static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(n), static_cast<uint8_t>(vel)); }
    void off(int n, int ch = 0) { midi(static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(n), 0); }
    void bend(int ch, float norm) { const int v = std::clamp(static_cast<int>(std::lround(norm * 8192)) + 8192, 0, 16383);
        midi(static_cast<uint8_t>(0xE0 | ch), static_cast<uint8_t>(v & 0x7F), static_cast<uint8_t>(v >> 7)); }
    int voiceFor(int key, int zone = 0) const { for (uint32_t i = 0; i < VK_MAX_VOICES; ++i) if (k->voice(i).isHeld() && k->voice(i).key() == key && k->voice(i).zone() == zone) return static_cast<int>(i); return -1; }
    void run(double secs, bool capture = true) {
        const int blocks = std::max(1, static_cast<int>(secs * SR / BLK));
        for (int b = 0; b < blocks; ++b) {
            float* ch[2] = {L.data(), R.data()};
            if (hostTempo > 0) { k->setTransport(hostTempo, hostBeat, hostPlay); if (hostPlay) hostBeat += BLK / SR * hostTempo / 60.0; }
            k->render(ch, 2, BLK, pending.empty() ? nullptr : pending.data(), static_cast<uint32_t>(pending.size()));
            pending.clear();
            if (capture) out.insert(out.end(), L.begin(), L.end());
        }
    }
    uint32_t active() const { return k->activeVoiceCount(); }
    const VoiceContext& vc(uint32_t i) const { return k->voice(i).context(); }
    float offset(uint64_t addr, uint32_t voice = 0) const {
        return k->voice(voice).matrixOffset(slotOf(addr));
    }
    static int32_t slotOf(uint64_t addr) {
        // Registration order == table order.
        for (size_t i = 0; i < sizeof(kParamTable) / sizeof(kParamTable[0]); ++i)
            if (kParamTable[i].address == addr) return static_cast<int32_t>(i);
        return -1;
    }
};

static double freqOf(const std::vector<float>& x, size_t from, size_t to) {
    double first = -1, last = -1; int n = 0;
    for (size_t i = from + 1; i < to && i < x.size(); ++i) if (x[i - 1] < 0.f && x[i] >= 0.f) {
        const double t = static_cast<double>(i - 1) + x[i - 1] / static_cast<double>(x[i - 1] - x[i]);
        if (first < 0) first = t; last = t; ++n; }
    return n < 2 ? 0 : (n - 1) * SR / (last - first);
}
static float peakOf(const std::vector<float>& x, size_t a, size_t b) {
    float p = 0; for (size_t i = a; i < std::min(b, x.size()); ++i) p = std::max(p, std::fabs(x[i])); return p;
}
static double rmsOf(const std::vector<float>& x, size_t a, size_t b) {
    double s = 0; size_t n = 0; for (size_t i = a; i < std::min(b, x.size()); ++i) { s += x[i] * x[i]; ++n; } return n ? std::sqrt(s / n) : 0;
}
static size_t at(double secs) { return static_cast<size_t>(secs * SR); }

// --------------------------------------------------------------------------
static void testParameterTable() {
    std::puts("Parameter table & modulatable flags");
    ParameterStore ps;
    for (const auto& d : kParamTable) ps.registerParam({d.address, d.minValue, d.maxValue, d.defaultValue, d.smoothingMs, d.flags});
    CHECK(ps.isModulatable(VK_P_VCFB_CUTOFF) && ps.isModulatable(VK_P_LFO1_RATE) && ps.isModulatable(VK_P_VCO1_FINE),
          "filter cutoff, LFO rate and VCO fine are matrix destinations");
    CHECK(!ps.isModulatable(VK_P_GLOBAL_MASTER_VOLUME) && !ps.isModulatable(VK_P_VCO1_WAVE) && !ps.isModulatable(VK_P_MATRIX_SLOT1_AMOUNT),
          "global, discrete and matrix parameters are not destinations");
    Kernel k;   // the kernel additionally strips the attenuators
    (void)k;
    CHECK(VK_PARAM_COUNT == sizeof(kParamTable) / sizeof(kParamTable[0]) && VK_PARAM_COUNT == 737, "%u parameters registered", VK_PARAM_COUNT);
    CHECK(VK_P_GLOBAL_MASTER_VOLUME == 0 && VK_P_GLOBAL_PITCH_BEND_RANGE == 3 && VK_P_VCFB_CUTOFF == 2816,
          "shipped addresses unchanged");
}

static void testModulatedValue() {
    std::puts("ParameterStore::modulated — taper-aware offsets");
    ParameterStore ps;
    ps.registerParam({100, 20.f, 20000.f, 1000.f, 0.f, kParamFlagLog | kParamFlagModulatable});
    ps.registerParam({101, -12.f, 12.f, 0.f, 0.f, kParamFlagModulatable});
    std::vector<float> mod(VK_MAX_REGISTERED_PARAMS, 0.f);
    CHECK(ps.modulated(100, mod.data()) == 1000.f && ps.modulated(100, nullptr) == 1000.f, "zero offset is exact");
    mod[0] = 0.1f;   // 10 % of a 20 Hz – 20 kHz log range = 0.9966 octaves
    const float up = ps.modulated(100, mod.data());
    CHECK(std::fabs(up / 1000.f - std::exp2(0.99658f)) < 1e-3f, "log taper: +10 %% → ×%.4f (expect ×1.9953)", up / 1000.f);
    mod[0] = -5.f;
    CHECK(ps.modulated(100, mod.data()) == 20.f, "log taper clamps at minimum");
    mod[1] = 0.25f;
    CHECK(std::fabs(ps.modulated(101, mod.data()) - 6.f) < 1e-5f, "linear: +25 %% of ±12 st → +6 st");
    mod[1] = 3.f;
    CHECK(ps.modulated(101, mod.data()) == 12.f, "linear clamps at maximum");
}

static void testShaping() {
    std::puts("Slot shaping: polarity, curve, invert");
    ModSlotCfg c;
    CHECK(shapeModValue(0.5f, false, c) == 0.5f, "linear passthrough");
    c.polarity = 2; CHECK(shapeModValue(0.75f, false, c) == 0.5f, "unipolar → bipolar: 0.75 → 0.5");
    c.polarity = 1; CHECK(shapeModValue(-1.f, true, c) == 0.f && shapeModValue(1.f, true, c) == 1.f, "bipolar → unipolar");
    c.polarity = 0; c.curve = 1; CHECK(std::fabs(shapeModValue(-0.5f, true, c) + 0.25f) < 1e-6f, "exponential keeps sign: −0.5 → −0.25");
    c.curve = 2; CHECK(std::fabs(shapeModValue(0.25f, false, c) - 0.5f) < 1e-6f, "logarithmic: 0.25 → 0.5");
    c.curve = 3; CHECK(shapeModValue(1.f, true, c) == 1.f && shapeModValue(0.f, true, c) == 0.f && shapeModValue(0.5f, true, c) > 0.5f,
                      "S-curve fixes the ends and steepens the middle");
    c.curve = 0; c.invert = true;
    CHECK(shapeModValue(0.2f, false, c) == 0.8f && shapeModValue(0.3f, true, c) == -0.3f, "invert: unipolar 1−x, bipolar −x");
}

static void testLfoShapes() {
    std::puts("LFO shapes");
    Rng rng(7);
    for (int shape = 0; shape < 7; ++shape) {
        Lfo l; l.reset(0.f, rng);
        std::vector<float> b(48000);
        l.run(b.data(), 48000, 5.f, shape, SR, rng);
        float lo = 2, hi = -2, maxStep = 0;
        for (size_t i = 0; i < b.size(); ++i) { lo = std::min(lo, b[i]); hi = std::max(hi, b[i]); if (i) maxStep = std::max(maxStep, std::fabs(b[i] - b[i - 1])); }
        const bool bounded = lo >= -1.0001f && hi <= 1.0001f;
        if (shape <= 3) {
            const double f = freqOf(b, 0, b.size());
            CHECK(bounded && std::fabs(f - 5.0) < 0.05 && hi - lo > 1.9f, "shape %d: 5 Hz (measured %.3f Hz), range [%.2f, %.2f]", shape, f, lo, hi);
        } else if (shape == 4) {
            int edges = 0; for (size_t i = 1; i < b.size(); ++i) if (b[i] != b[i - 1]) ++edges;
            CHECK(bounded && edges == 10, "square: 10 transitions per second at 5 Hz (got %d)", edges);
        } else if (shape == 5) {
            int changes = 0; for (size_t i = 1; i < b.size(); ++i) if (b[i] != b[i - 1]) ++changes;
            CHECK(bounded && changes >= 4 && changes <= 5, "random step: one new value per cycle (%d changes)", changes);
        } else {
            CHECK(bounded && maxStep < 0.01f, "random smooth: continuous (max step %.5f)", maxStep);
        }
    }
}

static void testLfoToCutoff() {
    std::puts("Matrix: LFO 1 → Filter B cutoff");
    Rig r;
    r.set(LFO1 + 0, 0); r.set(LFO1 + 1, 4.f);           // sine, 4 Hz, free
    r.slot(0, MS_LFO1, VK_P_VCFB_CUTOFF, 50.f);
    r.on(48); r.run(0.05);
    float lo = 1, hi = -1;
    for (int b = 0; b < 200; ++b) { r.run(BLK / SR, false); const float o = r.offset(VK_P_VCFB_CUTOFF); lo = std::min(lo, o); hi = std::max(hi, o); }
    CHECK(std::fabs(hi - 0.5f) < 0.02f && std::fabs(lo + 0.5f) < 0.02f, "offset sweeps ±0.5 (got %.3f … %.3f)", lo, hi);
    CHECK(r.k->modFrame().slotCount == 1 && r.k->modFrame().lfo[0].used, "one active slot, LFO 1 marked in use");

    // Audible: brightness follows the LFO (HF energy varies strongly).
    Rig a, b;
    for (Rig* x : {&a, &b}) { x->set(VK_P_VCFB_CUTOFF, 400.f); x->set(VK_P_VCFB_ENV_AMOUNT, 0.f); x->set(LFO1 + 1, 2.f); }
    a.slot(0, MS_LFO1, VK_P_VCFB_CUTOFF, 30.f);
    a.on(36); b.on(36); a.run(2.0); b.run(2.0);
    auto hfVar = [](const std::vector<float>& x) {
        std::vector<double> e; for (size_t s = at(0.5); s + 2400 < x.size(); s += 2400) {
            double d = 0; for (size_t i = s + 1; i < s + 2400; ++i) d += (x[i] - x[i - 1]) * (x[i] - x[i - 1]); e.push_back(d); }
        const auto [mn, mx] = std::minmax_element(e.begin(), e.end()); return *mx / std::max(*mn, 1e-12); };
    const double va = hfVar(a.out), vb = hfVar(b.out);
    CHECK(va > 4.0 * vb, "high-frequency energy swings ×%.1f with the LFO vs ×%.2f without", va, vb);
}

static void testControllers() {
    std::puts("Controller sources: mod wheel → pitch, aftertouch, breath, expression, poly AT");
    Rig r;
    r.slot(0, MS_MODWHEEL, VK_MOD_DEST_PITCH, 100.f);
    r.set(VK_P_VCFB_CUTOFF, 8000.f);
    r.on(57); r.run(0.5);
    r.midi(0xB0, 1, 127); r.run(0.5);
    CHECK(std::fabs(r.vc(0).pitchModSemis - 12.f) < 1e-4f, "wheel fully up → +12 semitones (got %.4f)", r.vc(0).pitchModSemis);
    const double f0 = freqOf(r.out, at(0.2), at(0.45)), f1 = freqOf(r.out, at(0.75), at(0.95));
    CHECK(std::fabs(f1 / f0 - 2.0) < 0.01, "audible: %.2f Hz → %.2f Hz (ratio %.4f)", f0, f1, f1 / f0);

    r.slot(1, MS_AFTERTOUCH, VK_MOD_DEST_PAN, 100.f);
    r.midi(0xD0, 64, 0); r.run(0.02, false);
    CHECK(std::fabs(r.vc(0).panMod - 64.f / 127.f) < 1e-4f, "channel pressure → pan %.3f", r.vc(0).panMod);
    r.slot(1, MS_POLY_AT, VK_MOD_DEST_PAN, 100.f);
    r.midi(0xA0, 57, 127); r.run(0.02, false);
    CHECK(std::fabs(r.vc(0).panMod - 1.f) < 1e-4f, "poly pressure on the sounding key → pan 1.0");
    r.midi(0xA0, 60, 0); r.run(0.02, false);
    CHECK(std::fabs(r.vc(0).panMod - 1.f) < 1e-4f, "poly pressure on another key ignored");
    CHECK(r.k->controller(MS_EXPRESSION) == 1.f, "expression rests at full until CC 11 arrives");
    r.midi(0xB0, 11, 0); r.midi(0xB0, 2, 127); r.run(0.01, false);
    CHECK(r.k->controller(MS_EXPRESSION) == 0.f && r.k->controller(MS_BREATH) == 1.f, "CC 11 / CC 2 decoded");
}

static void testMacrosViaSlewAmp() {
    std::puts("Macros, scale-by source, slew, amplitude destination");
    Rig r;
    r.slot(0, MS_MACRO1, VK_P_VCO1_FINE, 50.f, 0.f, MS_VELOCITY);
    r.set(MACRO + 0, 100.f);
    r.on(60, 64); r.run(0.1, false);
    const float expect = 0.5f * (64.f / 127.f);
    CHECK(std::fabs(r.offset(VK_P_VCO1_FINE) - expect) < 1e-4f, "macro 1 × velocity (scale by) → %.4f (expect %.4f)", r.offset(VK_P_VCO1_FINE), expect);

    Rig s;
    s.slot(0, MS_CONSTANT, VK_P_VCO1_FINE, 0.f, 0.f);
    s.set(MATRIX + 9, 100.f);   // slot 1 slew 100 ms
    s.on(60); s.run(0.2, false);
    s.set(MATRIX + 5, 100.f);   // offset jumps to +100 %
    s.run(BLK * 2 / SR, false);
    const float early = s.offset(VK_P_VCO1_FINE);
    s.run(0.8, false);
    const float late = s.offset(VK_P_VCO1_FINE);
    CHECK(early > 0.01f && early < 0.2f && late > 0.99f, "100 ms slew: %.3f after 10 ms, %.4f after 0.8 s", early, late);

    Rig a;
    a.slot(0, MS_CONSTANT, VK_MOD_DEST_AMP, -100.f);
    a.on(60); a.run(0.5);
    Rig ref; ref.on(60); ref.run(0.5);
    CHECK(peakOf(a.out, at(0.2), at(0.5)) < 1e-6f && peakOf(ref.out, at(0.2), at(0.5)) > 0.05f,
          "amplitude −100 %% silences the voice (%.2e vs %.3f)", peakOf(a.out, at(0.2), at(0.5)), peakOf(ref.out, at(0.2), at(0.5)));
}

static void testDestinations() {
    std::puts("Destination validation & all-cutoffs");
    Rig r;
    r.slot(0, MS_CONSTANT, VK_P_GLOBAL_MASTER_VOLUME, 50.f);    // global: rejected
    r.slot(1, MS_CONSTANT, VK_P_VCO1_WAVE, 50.f);               // discrete: rejected
    r.slot(3, MS_CONSTANT, 9999, 50.f);                         // unregistered: rejected
    r.on(60); r.run(0.02, false);
    CHECK(r.k->modFrame().slotCount == 0, "non-modulatable / unknown destinations are ignored (%u active)", r.k->modFrame().slotCount);
    r.slot(4, MS_CONSTANT, VK_MOD_DEST_CUTOFFALL, 25.f);
    r.run(0.05, false);
    bool all = true;
    for (uint64_t a : {VK_P_VCFA_CUTOFF, VK_P_VCFB_CUTOFF, VK_P_OTA_CUTOFF, VK_P_DIODELADDER_CUTOFF, VK_P_TRANSISTORLADDER_CUTOFF})
        all = all && std::fabs(r.offset(a) - 0.25f) < 1e-4f;
    CHECK(all && r.k->modFrame().cutoffCount == 7, "all-cutoffs destination reaches every filter (%u targets)", r.k->modFrame().cutoffCount);
    r.set(MATRIX + 4 * 16, 0.f); r.run(0.02, false);
    CHECK(r.offset(VK_P_VCFB_CUTOFF) == 0.f, "disabling a slot clears its offsets");

    // Input attenuators (mixer levels, CV amounts) are per-voice destinations too:
    // the matrix opens VCO 1 although its level knob is at 0.
    Rig q, z;
    for (Rig* x : {&q, &z}) x->set(VK_P_VCO1_LEVEL, 0.f);
    q.slot(0, MS_VELOCITY, VK_P_VCO1_LEVEL, 100.f);
    q.on(57, 127); z.on(57, 127); q.run(0.5); z.run(0.5);
    const float pq = peakOf(q.out, at(0.2), at(0.5)), pz = peakOf(z.out, at(0.2), at(0.5));
    CHECK(pz < 1e-4f && pq > 0.05f, "velocity → VCO 1 level opens a closed mixer input (%.3f vs %.1e)", pq, pz);
    Rig v; v.set(VK_P_VCO1_LEVEL, 0.f); v.slot(0, MS_VELOCITY, VK_P_VCO1_LEVEL, 100.f);
    v.on(57, 20); v.on(64, 127); v.run(0.5);
    const float soft = v.k->voice(0).matrixOffset(Rig::slotOf(VK_P_VCO1_LEVEL)), loud = v.k->voice(1).matrixOffset(Rig::slotOf(VK_P_VCO1_LEVEL));
    CHECK(std::fabs(soft - 20.f / 127.f) < 1e-4f && std::fabs(loud - 1.f) < 1e-4f, "per-voice: soft note %.3f, loud note %.3f", soft, loud);
}

static void testKeyedLfoAndDelay() {
    std::puts("LFO retrigger & delay");
    Rig r;
    r.set(LFO1 + 0, 0);   // sine
    r.set(LFO1 + 2, 1);   // key
    r.set(LFO1 + 1, 3.f);
    r.slot(0, MS_LFO1, VK_P_VCO1_FINE, 100.f);
    r.on(60); r.run(0.31, false);
    r.on(64); r.run(BLK / SR, false);
    const float* l0 = r.vc(0).lfo[0];
    const float* l1 = r.vc(1).lfo[0];
    CHECK(l0 && l1, "voices expose LFO buffers");
    // Voice 1 restarted at phase 0 one block ago (sine, 3 Hz): ≈ sin(2π·3·Δ) with Δ ≤ 2 blocks.
    CHECK(l1 && std::fabs(l1[0]) < 0.25f && l0 && std::fabs(l0[0] - l1[0]) > 0.3f,
          "keyed: new note starts near phase 0 (%.3f) while the older voice is elsewhere (%.3f)", l1 ? l1[0] : 9.f, l0 ? l0[0] : 9.f);

    Rig d;
    d.set(LFO1 + 0, 4); d.set(LFO1 + 1, 10.f); d.set(LFO1 + 3, 1000.f);   // square, 1 s delay
    d.slot(0, MS_LFO1, VK_P_VCO1_FINE, 100.f);
    d.on(60);
    float early = 0, late = 0;
    for (int b = 0; b < 40; ++b) { d.run(BLK / SR, false); early = std::max(early, std::fabs(d.offset(VK_P_VCO1_FINE))); }   // first 0.21 s
    d.run(1.0, false);
    for (int b = 0; b < 40; ++b) { d.run(BLK / SR, false); late = std::max(late, std::fabs(d.offset(VK_P_VCO1_FINE))); }
    CHECK(early < 0.25f && late > 0.99f, "1 s delay fades the LFO in (≤ %.3f early, %.3f after)", early, late);
}

static void testLfoRackModule() {
    std::puts("LFO rack module (patchable jacks)");
    Rig r;
    auto find = [](const char* key) { for (uint32_t m = 0; m < vk_rack_module_count(); ++m) { VKModuleInfo i; vk_rack_module_info(m, &i); if (!std::strcmp(i.key, key)) return static_cast<int>(m); } return -1; };
    auto port = [](int m, const char* key, bool in) { VKModuleInfo mi; vk_rack_module_info(static_cast<uint32_t>(m), &mi);
        const uint32_t n = in ? mi.inputCount : mi.outputCount;
        for (uint32_t p = 0; p < n; ++p) { VKPortInfo pi; vk_rack_port_info(static_cast<uint32_t>(m), p, in, &pi); if (!std::strcmp(pi.key, key)) return static_cast<int>(p); } return -1; };
    const int lfo = find("lfo1"), outm = find("output");
    CHECK(lfo >= 0 && find("lfo3") >= 0 && port(lfo, "out", false) == 0 && port(lfo, "inv", false) == 1, "lfo1…lfo3 with out / inv jacks");
    VKCable c{}; c.id = 1; c.srcModule = static_cast<uint8_t>(lfo); c.srcPort = 0;
    c.dstModule = static_cast<uint8_t>(outm); c.dstPort = static_cast<uint8_t>(port(outm, "pan", true)); c.gain = 0.5f;
    VKPatchResult pr{};
    CHECK(r.k->setPatch(&c, 1, pr) == 0, "lfo1.out → output.pan accepted (auto-pan)");
    r.set(LFO1 + 1, 2.f);
    r.on(60); r.run(0.2);
    float mx = 0; for (int b = 0; b < 120; ++b) { r.run(BLK / SR); mx = std::max(mx, r.k->connectionLevel(1)); }
    CHECK(mx > 0.45f && mx < 0.55f, "cable carries the LFO (peak level %.3f over one cycle, gain 0.5)", mx);
    CHECK(r.k->modFrame().slotCount == 0 && r.vc(0).lfo[0] != nullptr, "rack demand alone runs the LFO");
}

static void testMonoLegatoUnison() {
    std::puts("Voice modes");
    {   Rig r; r.set(PERF + 0, 1);   // mono, last-note priority
        r.run(0.01, false);
        r.on(60); r.run(0.05, false); r.on(64); r.run(0.05, false);
        CHECK(r.active() == 1 && r.vc(0).note == 64, "mono: one voice, last note wins (%u voices, note %d)", r.active(), r.vc(0).note);
        r.off(64); r.run(0.05, false);
        CHECK(r.vc(0).note == 60 && r.k->voice(0).isHeld(), "mono: releasing returns to the held key");
        r.off(60); r.run(0.05, false);
        CHECK(r.k->voice(0).isReleasing() || r.active() == 0, "mono: last key up releases");
    }
    {   Rig r; r.set(PERF + 0, 1); r.set(PERF + 1, 1);   // low priority
        r.run(0.01, false);
        r.on(60); r.run(0.02, false); r.on(64); r.run(0.02, false);
        CHECK(r.vc(0).note == 60, "low priority keeps 60 when 64 is added");
        r.on(55); r.run(0.02, false);
        CHECK(r.vc(0).note == 55, "low priority switches to the lower 55");
        r.set(PERF + 1, 2); r.off(55); r.run(0.02, false);
        CHECK(r.vc(0).note == 64, "high priority (changed live) returns to 64");
    }
    // Legato vs mono retrigger, observed through an amp envelope that restarts from zero.
    auto dip = [](int mode) {
        Rig r; r.set(PERF + 0, static_cast<float>(mode));
        r.set(VK_P_EG2_ATTACK, 150.f); r.set(VK_P_EG2_RETRIGGER, 1.f); r.set(VK_P_VCFB_CUTOFF, 6000.f);
        r.run(0.01);
        r.on(57); r.run(0.5); r.on(62); r.run(0.3);
        const size_t t = at(0.51);
        return rmsOf(r.out, t + at(0.005), t + at(0.03)) / rmsOf(r.out, t - at(0.05), t - at(0.01));
    };
    const double dm = dip(1), dl = dip(2);
    CHECK(dm < 0.5 && dl > 0.8, "mono retriggers (level ×%.2f after the 2nd key), legato does not (×%.2f)", dm, dl);
    {   Rig r; r.set(PERF + 0, 4);   // duo
        r.run(0.01, false);
        r.on(60); r.run(0.05, false);
        CHECK(r.active() == 2 && r.vc(0).note == 60 && r.vc(1).note == 60, "duo: one key sounds on both voices");
        r.on(67); r.run(0.05, false);
        CHECK(r.vc(0).note == 60 && r.vc(1).note == 67, "duo: two keys → low on voice 1, high on voice 2");
        r.on(64); r.run(0.05, false);
        CHECK(r.vc(0).note == 60 && r.vc(1).note == 67 && r.active() == 2, "duo: a middle key changes nothing");
        r.off(67); r.run(0.05, false);
        CHECK(r.vc(0).note == 60 && r.vc(1).note == 64, "duo: releasing the top key moves voice 2 to the next highest");
        r.off(60); r.off(64); r.run(0.05, false);
        CHECK(!r.k->voice(0).isHeld() && !r.k->voice(1).isHeld(), "duo: all keys up releases both");
    }
    {   Rig r; r.set(PERF + 0, 3); r.set(PERF + 3, 2); r.set(PERF + 4, 20.f);   // unison, 4 voices, 20 cents
        r.run(0.05, false);
        r.on(60); r.run(0.1, false);
        CHECK(r.active() == 4, "unison: one key → 4 voices (%u)", r.active());
        CHECK(std::fabs(r.vc(0).detuneSemis + 0.2f) < 1e-3f && std::fabs(r.vc(3).detuneSemis - 0.2f) < 1e-3f &&
              std::fabs(r.vc(1).detuneSemis + 0.2f / 3.f) < 1e-3f, "detune spread −20 … +20 cents (%.3f, %.3f, %.3f st)",
              r.vc(0).detuneSemis, r.vc(1).detuneSemis, r.vc(3).detuneSemis);
        r.on(67); r.run(0.05, false);
        bool all = true; for (uint32_t i = 0; i < 4; ++i) all = all && r.vc(i).note == 67;
        CHECK(all && r.active() == 4, "unison follows the newest key on all voices");
        r.set(PERF + 0, 0); r.run(0.05, false);
        CHECK(r.active() == 0 && r.vc(0).detuneSemis == 0.f, "switching mode releases everything and clears detune");
    }
}

static void testGlideAndBend() {
    std::puts("Glide modes & bend ranges");
    {   Rig r; r.set(VK_P_GLOBAL_GLIDE, 200.f); r.set(PERF + 2, 1);   // legato only
        r.run(0.01, false);
        r.on(60); r.run(0.05, false);
        CHECK(!r.vc(0).glide, "legato-only: detached note does not glide");
        r.on(72); r.run(0.05, false);
        bool overlappingGlides = false; for (uint32_t i = 0; i < 8; ++i) if (r.vc(i).note == 72) overlappingGlides = r.vc(i).glide;
        CHECK(overlappingGlides, "legato-only: overlapping note glides");
    }
    auto glideTime = [](bool constantTime, int interval) {
        Rig r; r.set(VK_P_GLOBAL_GLIDE, 100.f); r.set(PERF + 6, constantTime ? 1.f : 0.f);
        r.set(PERF + 0, 2); r.set(VK_P_VCFB_CUTOFF, 8000.f);
        r.run(0.01);
        r.on(45); r.run(0.4); r.on(45 + interval); r.run(1.0);
        const double target = 110.0 * std::pow(2.0, interval / 12.0);
        const size_t s = at(0.41);
        for (size_t t = s; t + 2400 < r.out.size(); t += 480) if (std::fabs(freqOf(r.out, t, t + 2400) / target - 1.0) < 0.01) return (t - s) / SR;
        return 9.0;
    };
    const double ct12 = glideTime(true, 12), ct24 = glideTime(true, 24), cr12 = glideTime(false, 12), cr24 = glideTime(false, 24);
    CHECK(std::fabs(ct24 - ct12) < 0.06 && cr24 > 1.6 * cr12, "constant time: %.0f / %.0f ms; constant rate: %.0f / %.0f ms (1 vs 2 octaves)",
          ct12 * 1e3, ct24 * 1e3, cr12 * 1e3, cr24 * 1e3);

    Rig b; b.set(VK_P_GLOBAL_PITCH_BEND_RANGE, 2.f); b.set(PERF + 5, 12.f); b.set(VK_P_VCFB_CUTOFF, 8000.f);
    b.on(57); b.midi(0xE0, 0, 0); b.run(0.5);           // full down
    b.midi(0xE0, 0x7F, 0x7F); b.run(0.5);               // full up
    const double fd = freqOf(b.out, at(0.2), at(0.45)), fu = freqOf(b.out, at(0.7), at(0.95));
    CHECK(std::fabs(fd - 110.0) < 0.5 && std::fabs(fu - 220.0 * std::pow(2.0, 2.0 * (8191.0 / 8192.0) / 12.0)) < 0.5,
          "bend down 12 st → %.2f Hz, bend up 2 st → %.2f Hz", fd, fu);
    Rig s; s.set(VK_P_GLOBAL_PITCH_BEND_RANGE, 7.f); s.set(VK_P_VCFB_CUTOFF, 8000.f);   // down = same as up (−1)
    s.on(57); s.midi(0xE0, 0, 0); s.run(0.5);
    const double fs = freqOf(s.out, at(0.2), at(0.45));
    CHECK(std::fabs(fs / (220.0 * std::pow(2.0, -7.0 / 12.0)) - 1.0) < 0.003, "down range follows up range by default (%.2f Hz)", fs);
}

static void testRealtimeSafety() {
    std::puts("Real-time safety & cost");
    auto bench = [](bool heavy, bool track) {
        Rig r;
        r.set(VK_P_GLOBAL_POLYPHONY, 4);   // 8 voices
        if (heavy) {
            r.set(LFO1 + 2, 1); r.set(LFO1 + 256 + 2, 1);
            const uint64_t dests[16] = {VK_P_VCFB_CUTOFF, VK_P_VCFB_RESONANCE, VK_P_VCO1_FINE, VK_P_VCO2_FINE, VK_P_VCO1_PULSE_WIDTH,
                VK_MOD_DEST_PITCH, VK_MOD_DEST_PAN, VK_MOD_DEST_AMP, VK_MOD_DEST_CUTOFFALL, VK_P_EG1_DECAY, VK_P_VCFA_CUTOFF,
                VK_P_LFO1_RATE, VK_P_LFO2_LEVEL, VK_P_VCO2_LEVEL, VK_P_VCA_SATURATION, VK_P_VCO3_FINE};
            for (uint32_t s = 0; s < 16; ++s) {
                r.slot(s, 1 + static_cast<int>(s % 20), dests[s], 10.f, 0.f, s % 3 == 0 ? MS_VELOCITY : 0);
                r.set(MATRIX + s * 16 + 6, static_cast<float>(s % 4)); r.set(MATRIX + s * 16 + 9, s % 2 ? 20.f : 0.f);
            }
        }
        for (int n = 0; n < 8; ++n) r.on(48 + n * 3);
        r.run(0.2, false);
        if (track) { gAllocs = 0; gTrack = true; }
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 400; ++b) {
            if (b % 7 == 0) r.midi(0xB0, 1, static_cast<uint8_t>(b % 128));
            if (b % 11 == 0) r.midi(0xD0, static_cast<uint8_t>(b % 128), 0);
            if (b % 37 == 0) { r.set(PERF + 0, static_cast<float>((b / 37) % 5)); r.on(50 + b % 20); }
            if (b % 41 == 0) r.off(50 + (b - 4) % 20);
            r.run(BLK / SR, false);
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        gTrack = false;
        return std::make_pair(secs / (400.0 * BLK / SR), static_cast<long>(gAllocs));
    };
    const auto light = bench(false, false);
    const auto heavy = bench(true, true);
    CHECK(heavy.second == 0, "zero allocations while rendering with 16 slots, keyed LFOs, CC streams and mode switches");
    std::printf("  info realtime load (8 voices, mode switches): no matrix %.1f %%, 16 slots %.1f %% of one core\n", light.first * 100, heavy.first * 100);
}

// ===========================================================================
// Phase 4B
// ===========================================================================
static void testTempoSync() {
    std::puts("Tempo-synced LFOs");
    Rig r;
    r.set(LFO1 + 0, 2); r.set(LFO1 + 6, 5);   // saw up, 1/4 note (one cycle per beat)
    r.slot(0, MS_LFO1, VK_P_VCO1_FINE, 50.f);
    r.hostTempo = 120; r.hostBeat = 8.0; r.hostPlay = true;
    r.on(60); r.run(0.1, false);
    float worst = 0;
    for (int b = 0; b < 60; ++b) {
        r.run(BLK / SR, false);
        const double beatEnd = r.hostBeat - (BLK - 32) / SR * 2.0 - 1.0 / SR * 2.0;   // last control block of the render
        const double fr = beatEnd - std::floor(beatEnd);
        const float expect = static_cast<float>(0.5 * (2.0 * fr - 1.0));
        float err = std::fabs(r.offset(VK_P_VCO1_FINE) - expect);
        err = std::min(err, std::fabs(err - 1.f));   // wrap at the saw reset
        worst = std::max(worst, err);
    }
    CHECK(worst < 0.03f, "free LFO follows the host beat (saw phase error ≤ %.3f)", worst);
    CHECK(std::fabs(r.k->modFrame().lfo[0].syncedRate - 2.f) < 1e-5f, "1/4 at 120 BPM = 2 Hz");
    r.hostBeat = 100.5; r.run(BLK / SR, false);
    const float afterJump = r.offset(VK_P_VCO1_FINE);
    CHECK(std::fabs(afterJump - 0.5f * (2.f * static_cast<float>(std::fmod(100.5 + (BLK - 1) / SR * 2.0, 1.0)) - 1.f)) < 0.03f,
          "host relocation re-locks the phase immediately (%.3f)", afterJump);

    Rig s; s.set(VK_P_GLOBAL_TEMPO, 90); s.set(LFO1 + 6, 10); s.slot(0, MS_LFO1, VK_P_VCO1_FINE, 10.f);   // dotted 1/4 = 1.5 beats
    s.on(60); s.run(0.05, false);
    CHECK(std::fabs(s.k->modFrame().lfo[0].syncedRate - 1.f) < 1e-5f && std::fabs(s.k->tempo() - 90.0) < 1e-9,
          "no host: internal tempo 90 BPM, dotted 1/4 → %.3f Hz (expect 1.000)", s.k->modFrame().lfo[0].syncedRate);
    s.set(LFO1 + 6, 0); s.run(0.02, false);
    CHECK(s.k->modFrame().lfo[0].syncedRate == 0.f, "sync off returns to the rate knob");
}

static void testMpe() {
    std::puts("MPE");
    Rig r; r.set(VK_P_GLOBAL_MPE_MODE, 1);   // lower zone, 15 members, ±48 st
    r.slot(0, MS_POLY_AT, VK_MOD_DEST_PAN, 100.f);
    r.slot(1, MS_TIMBRE, VK_P_VCO1_FINE, 100.f);
    r.run(0.01, false);
    r.on(60, 100, 1); r.on(64, 100, 2); r.run(0.02, false);
    const int a = r.voiceFor(60), b = r.voiceFor(64);
    CHECK(a >= 0 && b >= 0 && r.k->voice(static_cast<uint32_t>(a)).channel() == 1 && r.k->voice(static_cast<uint32_t>(b)).channel() == 2, "notes tagged with their member channels");
    r.bend(1, 0.25f); r.midi(0xD2, 127, 0); r.midi(0xB1, 74, 127); r.run(0.02, false);
    const auto& va = r.vc(static_cast<uint32_t>(a)); const auto& vb = r.vc(static_cast<uint32_t>(b));
    CHECK(std::fabs(va.noteBendSemis - 12.f) < 1e-3f && vb.noteBendSemis == 0.f, "per-note bend: +12 st on ch 2 only (%.2f / %.2f)", va.noteBendSemis, vb.noteBendSemis);
    CHECK(std::fabs(vb.panMod - 1.f) < 1e-4f && va.panMod == 0.f, "channel pressure → that note's poly-pressure source only");
    CHECK(std::fabs(r.offset(VK_P_VCO1_FINE, static_cast<uint32_t>(a)) - 1.f) < 1e-4f &&
          std::fabs(r.offset(VK_P_VCO1_FINE, static_cast<uint32_t>(b)) - 0.5f) < 1e-4f, "CC 74 timbre per note (default 0.5)");
    r.off(64, 1); r.run(0.02, false);
    CHECK(r.k->voice(static_cast<uint32_t>(b)).isHeld(), "note-off on another channel does not release the note");
    r.off(64, 2); r.run(0.02, false);
    CHECK(!r.k->voice(static_cast<uint32_t>(b)).isHeld(), "note-off on its own channel releases it");
    r.bend(0, 1.f); r.run(0.02, false);   // master channel → zone-wide bend
    CHECK(r.k->modFrame().global[MS_BEND] > 0.99f && std::fabs(r.vc(static_cast<uint32_t>(a)).noteBendSemis - 12.f) < 1e-3f, "master-channel bend stays zone-wide");

    // Audible: per-note bend of +12 st on one note.
    Rig f; f.set(VK_P_GLOBAL_MPE_MODE, 1); f.set(VK_P_VCFB_CUTOFF, 8000.f); f.run(0.01);
    f.bend(3, 0.25f); f.on(57, 100, 3); f.run(0.5);   // bend sent before note-on (MPE convention)
    const double hz = freqOf(f.out, at(0.2), at(0.5));
    CHECK(std::fabs(hz - 440.0) < 1.0, "pre-note bend adopted: A3 on a channel bent +12 st sounds %.2f Hz", hz);

    Rig o;   // MPE off: channels are ignored (omni), behaviour unchanged
    o.on(60, 100, 5); o.run(0.02, false); o.off(60, 9); o.run(0.02, false);
    CHECK(!o.k->voice(0).isHeld(), "MPE off: omni note-off (channel-blind)");
    Rig u; u.set(VK_P_GLOBAL_MPE_MODE, 2); u.set(VK_P_GLOBAL_MPE_MEMBER_CHANNELS, 3);   // upper zone, members ch 12–15 → 11..14
    u.on(60, 100, 14); u.on(62, 100, 15); u.run(0.02, false);
    CHECK(u.k->voice(static_cast<uint32_t>(std::max(0, u.voiceFor(60)))).channel() == 14 && u.voiceFor(62) >= 0 &&
          u.k->voice(static_cast<uint32_t>(u.voiceFor(62))).channel() == 0, "upper zone: ch 15 is a member, ch 16 is the master");
}

static void testZones() {
    std::puts("Key zones");
    Rig r; r.set(VK_P_GLOBAL_ZONE_MODE, 1); r.set(VK_P_GLOBAL_SPLIT_POINT, 60); r.set(VK_P_GLOBAL_ZONE_A_TRANSPOSE, -12);
    r.slot(0, MS_ZONE, VK_P_VCO1_FINE, 100.f);
    r.run(0.01, false);
    r.on(48); r.on(72); r.run(0.02, false);
    const int lo = r.voiceFor(48, 0), hi = r.voiceFor(72, 1);
    CHECK(lo >= 0 && hi >= 0 && r.vc(static_cast<uint32_t>(lo)).note == 36 && r.vc(static_cast<uint32_t>(hi)).note == 72,
          "split at C4: 48 → zone A transposed to 36, 72 → zone B");
    CHECK(r.offset(VK_P_VCO1_FINE, static_cast<uint32_t>(lo)) == 0.f && std::fabs(r.offset(VK_P_VCO1_FINE, static_cast<uint32_t>(hi)) - 1.f) < 1e-5f,
          "Key Zone source: 0 in zone A, 1 in zone B");
    r.off(48); r.run(0.02, false);
    CHECK(!r.k->voice(static_cast<uint32_t>(lo)).isHeld() && r.k->voice(static_cast<uint32_t>(hi)).isHeld(), "release matches the played key, not the transposed note");

    Rig l; l.set(VK_P_GLOBAL_ZONE_MODE, 2); l.set(VK_P_GLOBAL_ZONE_B_TRANSPOSE, 7);
    l.run(0.01, false);
    l.on(60); l.run(0.02, false);
    const int za = l.voiceFor(60, 0), zb = l.voiceFor(60, 1);
    CHECK(l.active() == 2 && za >= 0 && zb >= 0 && l.vc(static_cast<uint32_t>(zb)).note == 67, "layer: one key → zone A (60) + zone B (+7 = 67)");
    l.on(60); l.run(0.02, false);
    CHECK(l.active() == 2, "repeating the key retriggers both layers in place (no extra voices)");
    l.off(60); l.run(0.02, false);
    CHECK(!l.k->voice(static_cast<uint32_t>(za)).isHeld() && !l.k->voice(static_cast<uint32_t>(zb)).isHeld(), "key up releases both layers");
}

static void testMetersAndChords() {
    std::puts("Matrix meters, held keys, chord analyser");
    Rig r;
    r.slot(0, MS_CONSTANT, VK_P_VCO1_FINE, 0.f, 30.f);
    r.slot(1, MS_VELOCITY, VK_P_VCO1_FINE, 100.f);
    r.on(60, 127); r.run(0.05, false); r.on(64, 32); r.run(0.05, false);
    CHECK(std::fabs(r.k->slotMeter(0) - 0.3f) < 1e-4f && std::fabs(r.k->slotMeter(1) - 32.f / 127.f) < 1e-4f,
          "meters show the newest voice (offset 0.30, velocity %.3f)", r.k->slotMeter(1));
    r.on(67); r.run(0.02, false);
    uint32_t m[4]; r.k->heldNotes(m);
    CHECK(m[1] == (1u << (60 - 32)) && m[2] == ((1u << (64 - 64)) | (1u << (67 - 64))) && m[0] == 0 && m[3] == 0, "held-key mask: 60, 64, 67");
    r.midi(0xB0, 64, 127); r.off(60); r.off(64); r.off(67); r.run(0.02, false);
    r.k->heldNotes(m);
    CHECK(m[1] == 0 && m[2] == 0 && r.active() == 3, "mask follows keys (not the sustain pedal)");

    struct C { std::vector<uint8_t> n; const char* name; };
    const C cases[] = {
        {{60, 64, 67}, "C"}, {{64, 67, 72}, "C/E"}, {{57, 60, 64}, "Am"}, {{60, 64, 67, 70}, "C7"}, {{48, 64, 70}, "C7"},
        {{55, 59, 62, 65}, "G7"}, {{59, 62, 65}, "Bdim"}, {{60, 63, 66, 69}, "Cdim7"}, {{60, 67}, "C5"}, {{50, 54, 57, 60, 64}, "D9"},
        {{66}, "F#"}, {{60, 64, 67, 71}, "Cmaj7"}, {{62, 65, 69, 72}, "Dm7"}, {{60, 65, 67}, "Csus4"}, {{56, 60, 63}, "Ab"},
        {{60, 62, 64, 65}, ""}, {{}, ""},
    };
    int bad = 0;
    for (const auto& c : cases) {
        char buf[16];
        vk_chord_name(c.n.data(), static_cast<uint32_t>(c.n.size()), buf, sizeof(buf));
        if (std::strcmp(buf, c.name) != 0) { ++bad; std::printf("    chord mismatch: got \"%s\", want \"%s\"\n", buf, c.name); }
    }
    CHECK(bad == 0, "%zu chord cases (triads, inversions, sevenths, no-fifth, symmetric, power, ninths, clusters)", sizeof(cases) / sizeof(cases[0]));
    char small[3];
    const uint8_t cmaj7[] = {60, 64, 67, 71};
    vk_chord_name(cmaj7, 4, small, sizeof(small));
    CHECK(std::strlen(small) == 2, "output truncated safely to the buffer");
}

static void testPerformanceRealtime() {
    std::puts("Phase 4B real-time safety");
    Rig r; r.set(VK_P_GLOBAL_MPE_MODE, 1); r.set(VK_P_GLOBAL_ZONE_MODE, 2); r.set(LFO1 + 6, 7);
    r.slot(0, MS_TIMBRE, VK_P_VCFB_CUTOFF, 30.f); r.slot(1, MS_ZONE, VK_P_VCO2_LEVEL, 50.f); r.slot(2, MS_LFO1, VK_MOD_DEST_PITCH, 2.f);
    r.hostTempo = 128; r.hostPlay = true;
    r.run(0.1, false);
    gAllocs = 0; gTrack = true;
    for (int b = 0; b < 300; ++b) {
        const int ch = 1 + b % 6;
        if (b % 5 == 0) r.on(48 + b % 24, 100, ch);
        if (b % 5 == 3) r.off(48 + (b - 3) % 24, ch);
        r.bend(ch, std::sin(b * 0.1f)); r.midi(static_cast<uint8_t>(0xD0 | ch), static_cast<uint8_t>(b % 128), 0);
        r.midi(static_cast<uint8_t>(0xB0 | ch), 74, static_cast<uint8_t>((b * 3) % 128));
        uint32_t m[4]; r.k->heldNotes(m); (void)r.k->slotMeter(0);
        r.run(BLK / SR, false);
    }
    gTrack = false;
    CHECK(gAllocs == 0, "zero allocations with MPE streams, layered zones, host transport and meters");
}

int main() {
    testParameterTable();
    testModulatedValue();
    testShaping();
    testLfoShapes();
    testLfoToCutoff();
    testControllers();
    testMacrosViaSlewAmp();
    testDestinations();
    testKeyedLfoAndDelay();
    testLfoRackModule();
    testMonoLegatoUnison();
    testGlideAndBend();
    testRealtimeSafety();
    testTempoSync();
    testMpe();
    testZones();
    testMetersAndChords();
    testPerformanceRealtime();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
