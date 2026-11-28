// test_midi.cpp — Phase 7: MIDI engine. RPN/NRPN/MCM, 14-bit CC, MIDI Learn
// (absolute range/invert/taper, toggle, relative encodings, pickup, note
// gates), learn reporting and parameter-change reporting, program change,
// receive channel, velocity curves, MIDI clock in (tempo, start/stop/SPP,
// sync source) and out (24 PPQN at exact offsets), MIDI out of generated
// notes, MIDI 2.0 UMP (hi-res velocity, 32-bit CC, RPN, per-note bend,
// per-note controllers, per-note reset, program+bank), floods, zero
// allocations and a UI/render concurrency run (for TSan).
#include "VekturDSP.h"
#include "Kernel.hpp"
#include "ParamTable.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
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
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

using namespace vektur;
constexpr double SR = 48000.0;
constexpr uint32_t BLK = 256;
constexpr uint64_t RES = VK_P_VCFA_RESONANCE, CUT = VK_P_VCFA_CUTOFF;

struct OutEv { uint64_t at; uint8_t len, a, b, c; };

struct Rig {
    std::unique_ptr<Kernel> k = std::make_unique<Kernel>();
    std::vector<float> L = std::vector<float>(BLK), R = std::vector<float>(BLK);
    std::vector<VKEvent> pending;
    std::vector<OutEv> out;
    uint64_t frames = 0;
    double hostTempo = 0, hostBeat = 0; bool hostPlay = false;
    Rig() { k->allocate(SR, 2, 4096); pending.reserve(4096); out.reserve(1 << 18); }
    void set(uint64_t a, float v) { k->setParam(a, v); }
    float get(uint64_t a) const { return k->getParam(a); }
    void midi(uint8_t a, uint8_t b = 0, uint8_t c = 0, uint32_t off = 0, uint8_t len = 0) {
        VKEvent e{}; e.type = VK_EVENT_MIDI1; e.sampleOffset = off;
        e.length = len ? len : (a >= 0xF8 || a == 0xFA || a == 0xFB || a == 0xFC) ? 1 : ((a & 0xF0) == 0xC0 || (a & 0xF0) == 0xD0) ? 2 : 3;
        e.data[0] = a; e.data[1] = b; e.data[2] = c;
        pending.push_back(e);
    }
    void cc(int ch, int n, int v, uint32_t off = 0) { midi(static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(n), static_cast<uint8_t>(v), off); }
    void on(int n, int vel = 100, int ch = 0, uint32_t off = 0) { midi(static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(n), static_cast<uint8_t>(vel), off); }
    void off(int n, int ch = 0, uint32_t off = 0) { midi(static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(n), 0, off); }
    void ump(uint32_t w0, uint32_t w1, uint32_t off = 0) {
        VKEvent e{}; e.type = VK_EVENT_UMP; e.sampleOffset = off; e.ump[0] = w0; e.ump[1] = w1; pending.push_back(e);
    }
    void block() {
        float* ch[2] = {L.data(), R.data()};
        if (hostTempo > 0) { k->setTransport(hostTempo, hostBeat, hostPlay); if (hostPlay) hostBeat += BLK / SR * hostTempo / 60.0; }
        for (size_t i = 1; i < pending.size(); ++i)   // stable insertion sort (no temporary buffer)
            for (size_t j = i; j > 0 && pending[j - 1].sampleOffset > pending[j].sampleOffset; --j) std::swap(pending[j - 1], pending[j]);
        k->render(ch, 2, BLK, pending.empty() ? nullptr : pending.data(), static_cast<uint32_t>(pending.size()));
        pending.clear();
        const VKMidiOutEvent* ev = nullptr;
        const uint32_t n = k->midiOutput(&ev);
        for (uint32_t i = 0; i < n; ++i) out.push_back({frames + ev[i].sampleOffset, ev[i].length, ev[i].data[0], ev[i].data[1], ev[i].data[2]});
        frames += BLK;
    }
    void run(int blocks) { for (int i = 0; i < blocks; ++i) block(); }
    void map(std::vector<VKMidiMapping> m) { k->setMidiMappings(m.data(), static_cast<uint32_t>(m.size())); }
    int voiceFor(int key) const { for (uint32_t i = 0; i < VK_MAX_VOICES; ++i) if (!k->voice(i).isIdle() && k->voice(i).key() == key) return static_cast<int>(i); return -1; }
    std::vector<VKMidiUIEvent> ui() { std::vector<VKMidiUIEvent> v; VKMidiUIEvent e; while (k->popMidiUi(e)) v.push_back(e); return v; }
    std::vector<uint64_t> changes() { uint64_t a[64]; const uint32_t n = k->takeMidiChanges(a, 64); return {a, a + n}; }
};

static VKMidiMapping M(uint8_t src, uint16_t num, uint64_t addr, float lo = 0.f, float hi = 1.f, uint8_t mode = VK_MIDI_MODE_ABSOLUTE,
                       uint8_t rel = 0, bool pickup = false, float sens = 1.f / 128.f, uint8_t ch = VK_MIDI_ANY_CHANNEL) {
    VKMidiMapping m{}; m.source = src; m.channel = ch; m.number = num; m.address = addr; m.minimum = lo; m.maximum = hi;
    m.mode = mode; m.relative = rel; m.pickup = pickup; m.sensitivity = sens; return m;
}
static uint32_t U4(uint32_t op, uint32_t ch, uint32_t idx, uint32_t low) { return 0x40000000u | op << 20 | ch << 16 | idx << 8 | low; }
static bool has(const std::vector<uint64_t>& v, uint64_t a) { return std::find(v.begin(), v.end(), a) != v.end(); }

// ---------------------------------------------------------------------------
static void testRpn() {
    std::printf("RPN / NRPN / MCM\n");
    Rig r;
    r.cc(0, 101, 0); r.cc(0, 100, 0); r.cc(0, 6, 12); r.cc(0, 38, 50); r.block();
    CHECK(near(r.get(VK_P_GLOBAL_PITCH_BEND_RANGE), 12.5, 1e-4), "RPN 0: bend range 12 semitones + 50 cents (%.3f)", r.get(VK_P_GLOBAL_PITCH_BEND_RANGE));
    auto ui = r.ui();
    CHECK(!ui.empty() && ui.back().kind == VK_MIDI_UI_PARAMS_CHANGED, "parameter change reported to the UI");
    CHECK(has(r.changes(), VK_P_GLOBAL_PITCH_BEND_RANGE), "changed address listed for the parameter tree");
    CHECK(r.changes().empty(), "change list is cleared once taken");

    r.cc(0, 101, 127); r.cc(0, 100, 127); r.cc(0, 6, 3); r.block();
    CHECK(near(r.get(VK_P_GLOBAL_PITCH_BEND_RANGE), 12.5, 1e-4), "RPN null (127/127) ignores data entry");

    r.cc(0, 101, 0); r.cc(0, 100, 1); r.cc(0, 6, 96); r.cc(0, 38, 0); r.block();
    CHECK(near(r.get(VK_P_GLOBAL_MASTER_TUNE), 50.0, 0.01), "RPN 1: fine tune +50 cents (%.2f)", r.get(VK_P_GLOBAL_MASTER_TUNE));

    // MPE Configuration Message on the lower zone master (channel 1).
    r.cc(0, 101, 0); r.cc(0, 100, 6); r.cc(0, 6, 7); r.block();
    CHECK(std::lround(r.get(VK_P_GLOBAL_MPE_MODE)) == 1 && std::lround(r.get(VK_P_GLOBAL_MPE_MEMBER_CHANNELS)) == 6, "MCM: lower zone, 7 member channels");
    r.cc(3, 101, 0); r.cc(3, 100, 0); r.cc(3, 6, 36); r.block();
    CHECK(near(r.get(VK_P_GLOBAL_MPE_PITCH_RANGE), 36, 1e-4), "RPN 0 on a member channel sets per-note bend range");
    r.cc(0, 101, 0); r.cc(0, 100, 0); r.cc(0, 6, 7); r.block();
    CHECK(near(r.get(VK_P_GLOBAL_PITCH_BEND_RANGE), 7, 1e-4), "RPN 0 on the master channel sets the zone bend range");
    r.cc(15, 101, 0); r.cc(15, 100, 6); r.cc(15, 6, 0); r.block();
    CHECK(std::lround(r.get(VK_P_GLOBAL_MPE_MODE)) == 1, "zone-off on the other master leaves the lower zone alone");
    r.cc(0, 101, 0); r.cc(0, 100, 6); r.cc(0, 6, 0); r.block();
    CHECK(std::lround(r.get(VK_P_GLOBAL_MPE_MODE)) == 0, "MCM zone off");
    r.cc(15, 101, 0); r.cc(15, 100, 6); r.cc(15, 6, 4); r.block();
    CHECK(std::lround(r.get(VK_P_GLOBAL_MPE_MODE)) == 2 && std::lround(r.get(VK_P_GLOBAL_MPE_MEMBER_CHANNELS)) == 3, "MCM: upper zone, 4 members");

    // NRPN: learn, absolute data, increment/decrement.
    Rig n;
    n.k->midiLearn(true);
    n.cc(0, 99, 1); n.cc(0, 98, 2); n.cc(0, 6, 64); n.cc(0, 38, 0); n.block();
    auto e = n.ui();
    CHECK(!e.empty() && e[0].kind == VK_MIDI_UI_LEARNED && e[0].source == VK_MIDI_SRC_NRPN && e[0].number == 130, "learn reports NRPN 130 (not the CCs carrying it)");
    n.map({M(VK_MIDI_SRC_NRPN, 130, RES)});
    n.cc(0, 99, 1); n.cc(0, 98, 2); n.cc(0, 6, 64); n.cc(0, 38, 0); n.block();
    CHECK(near(n.get(RES), 8192.0 / 16383.0 * 100.0, 1e-3), "NRPN 14-bit data → parameter (%.4f)", n.get(RES));
    n.cc(0, 38, 127); n.block();
    CHECK(near(n.get(RES), 8319.0 / 16383.0 * 100.0, 1e-3), "data-entry LSB refines the value");
    n.map({M(VK_MIDI_SRC_NRPN, 130, RES, 0, 1, VK_MIDI_MODE_RELATIVE, 0, false, 0.01f)});
    n.set(RES, 50); n.block();
    n.cc(0, 96, 1); n.cc(0, 96, 1); n.cc(0, 97, 1); n.block();
    CHECK(near(n.get(RES), 51.0, 1e-3), "NRPN increment ×2, decrement ×1 → +1 step (%.3f)", n.get(RES));
}

static void testMappings() {
    std::printf("MIDI Learn mappings\n");
    Rig r;
    // 14-bit CC pair 7/39.
    r.map({M(VK_MIDI_SRC_CC14, 7, RES)});
    r.block();
    r.cc(0, 7, 100); r.cc(0, 39, 64); r.block();
    CHECK(near(r.get(RES), (100.0 * 128 + 64) / 16383.0 * 100.0, 1e-3), "14-bit CC (MSB 7 + LSB 39)");
    // Range and invert, log taper.
    r.map({M(VK_MIDI_SRC_CC, 20, RES, 1.f, 0.f), M(VK_MIDI_SRC_CC, 21, RES, 0.25f, 0.75f), M(VK_MIDI_SRC_CC, 23, CUT)});
    r.block();
    r.cc(0, 20, 127); r.block();
    CHECK(near(r.get(RES), 0.0, 1e-4), "inverted range: 127 → minimum");
    r.cc(0, 20, 0); r.block();
    CHECK(near(r.get(RES), 100.0, 1e-4), "inverted range: 0 → maximum");
    r.cc(0, 21, 127); r.block();
    CHECK(near(r.get(RES), 75.0, 1e-3), "range 25–75 %%: 127 → 75");
    r.cc(0, 21, 0); r.block();
    CHECK(near(r.get(RES), 25.0, 1e-3), "range 25–75 %%: 0 → 25");
    r.cc(0, 23, 64); r.block();
    const double want = 20.0 * std::pow(1000.0, 64.0 / 127.0);
    CHECK(near(r.get(CUT), want, want * 1e-4), "log-tapered cutoff follows the taper (%.1f Hz vs %.1f)", r.get(CUT), want);
    CHECK(has(r.changes(), RES) && has(r.changes(), CUT) == false, "changes listed once (second take empty)");

    // Channel-specific mapping.
    r.map({M(VK_MIDI_SRC_CC, 24, RES, 0, 1, 0, 0, false, 0, 5)});
    r.block(); r.set(RES, 10); r.block();
    r.cc(0, 24, 127); r.block();
    CHECK(near(r.get(RES), 10, 1e-4), "mapping on channel 6 ignores channel 1");
    r.cc(5, 24, 127); r.block();
    CHECK(near(r.get(RES), 100, 1e-4), "mapping on channel 6 responds on channel 6");

    // Toggle (CC and note).
    r.map({M(VK_MIDI_SRC_CC, 25, RES, 0, 1, VK_MIDI_MODE_TOGGLE), M(VK_MIDI_SRC_NOTE, 36, CUT, 0, 1, VK_MIDI_MODE_TOGGLE)});
    r.block(); r.set(RES, 0); r.set(CUT, 20); r.block();
    r.cc(0, 25, 127); r.block();
    CHECK(near(r.get(RES), 100, 1e-4), "toggle: press flips to the other end");
    r.cc(0, 25, 0); r.block();
    CHECK(near(r.get(RES), 100, 1e-4), "toggle: release does nothing");
    r.cc(0, 25, 127); r.block();
    CHECK(near(r.get(RES), 0, 1e-4), "toggle: next press flips back");
    r.on(36); r.block(); r.off(36); r.block();
    CHECK(near(r.get(CUT), 20000, 1), "note toggle: on flips, off holds (%.0f)", r.get(CUT));
    r.on(36); r.block();
    CHECK(near(r.get(CUT), 20, 0.01), "note toggle: next note flips back");
    r.off(36); r.block();

    // Note gate (absolute).
    r.map({M(VK_MIDI_SRC_NOTE, 38, RES, 0.2f, 0.9f)});
    r.block();
    r.on(38); r.block();
    CHECK(near(r.get(RES), 90, 1e-3), "note gate: on → maximum");
    r.off(38); r.block();
    CHECK(near(r.get(RES), 20, 1e-3), "note gate: off → minimum");

    // Relative encodings.
    r.map({M(VK_MIDI_SRC_CC, 40, RES, 0, 1, VK_MIDI_MODE_RELATIVE, VK_MIDI_REL_TWOS_COMPLEMENT),
           M(VK_MIDI_SRC_CC, 41, RES, 0, 1, VK_MIDI_MODE_RELATIVE, VK_MIDI_REL_BINARY_OFFSET),
           M(VK_MIDI_SRC_CC, 42, RES, 0, 1, VK_MIDI_MODE_RELATIVE, VK_MIDI_REL_SIGNED_BIT)});
    r.block(); r.set(RES, 50); r.block();
    const double st = 100.0 / 128.0;
    r.cc(0, 40, 1); r.block();
    CHECK(near(r.get(RES), 50 + st, 1e-3), "two's complement +1");
    r.cc(0, 40, 126); r.block();
    CHECK(near(r.get(RES), 50 - st, 1e-3), "two's complement −2 (126)");
    r.cc(0, 41, 67); r.block();
    CHECK(near(r.get(RES), 50 + 2 * st, 1e-3), "binary offset +3 (67)");
    r.cc(0, 41, 64); r.block();
    CHECK(near(r.get(RES), 50 + 2 * st, 1e-3), "binary offset 64 = no change");
    r.cc(0, 42, 0x43); r.block();
    CHECK(near(r.get(RES), 50 - st, 1e-3), "signed bit −3 (0x43)");
    r.map({M(VK_MIDI_SRC_CC, 40, RES, 0.4f, 0.6f, VK_MIDI_MODE_RELATIVE, 0, false, 0.5f)});
    r.block(); r.cc(0, 40, 5); r.block();
    CHECK(near(r.get(RES), 60, 1e-3), "relative clamps to the mapping range");

    // Pickup.
    r.map({M(VK_MIDI_SRC_CC, 22, RES, 0, 1, VK_MIDI_MODE_ABSOLUTE, 0, true)});
    r.block(); r.set(RES, 50); r.block();
    r.cc(0, 22, 10); r.block();
    CHECK(near(r.get(RES), 50, 1e-4), "pickup: far below — ignored");
    r.cc(0, 22, 30); r.block();
    CHECK(near(r.get(RES), 50, 1e-4), "pickup: still below — ignored");
    r.cc(0, 22, 100); r.block();
    CHECK(near(r.get(RES), 100.0 * 100 / 127, 1e-3), "pickup: crossing the value picks up");
    r.cc(0, 22, 90); r.block();
    CHECK(near(r.get(RES), 100.0 * 90 / 127, 1e-3), "pickup: then follows");
    r.set(RES, 20); r.block();
    r.cc(0, 22, 95); r.block();
    CHECK(near(r.get(RES), 20, 1e-4), "pickup re-arms when something else moves the parameter");
    r.cc(0, 22, 25); r.block();
    CHECK(near(r.get(RES), 100.0 * 25 / 127, 1e-3), "pickup: reaching the new value picks up again");

    // Pitch bend and channel pressure as sources; bend still bends.
    r.map({M(VK_MIDI_SRC_PITCH_BEND, 0, RES), M(VK_MIDI_SRC_CHANNEL_PRESSURE, 0, CUT)});
    r.block();
    r.midi(0xE0, 0x7F, 0x7F); r.midi(0xD0, 127); r.block();
    CHECK(near(r.get(RES), 100, 1e-3) && near(r.get(CUT), 20000, 1), "pitch bend and pressure drive mappings");
    CHECK(near(r.k->controller(MS_AFTERTOUCH), 1.0, 1e-6), "pressure still reaches the modulation source");

    // Learn: reserved controllers are skipped; learning disarms after one.
    Rig l;
    l.k->midiLearn(true);
    l.cc(2, 64, 127); l.cc(2, 1, 50); l.cc(2, 74, 3); l.block();
    auto e = l.ui();
    CHECK(e.size() == 1 && e[0].kind == VK_MIDI_UI_LEARNED && e[0].source == VK_MIDI_SRC_CC && e[0].number == 1 && e[0].channel == 2,
          "learn: sustain skipped, CC 1 on channel 3 reported once");
    l.k->midiLearn(true); l.midi(0xE2, 0, 0x40); l.block();
    e = l.ui();
    CHECK(e.size() == 1 && e[0].source == VK_MIDI_SRC_PITCH_BEND, "learn: pitch bend");
    l.k->midiLearn(true); l.on(48, 90, 9); l.block();
    e = l.ui();
    CHECK(e.size() == 1 && e[0].source == VK_MIDI_SRC_NOTE && e[0].number == 48 && e[0].channel == 9, "learn: note 48 on channel 10");
    CHECK(l.voiceFor(48) >= 0, "the learnt note still plays");

    // Invalid mappings are rejected when published.
    Rig v;
    VKMidiMapping bad[3] = {M(VK_MIDI_SRC_CC, 30, VK_ADDRESS_SPACE + 5), M(9, 30, RES), M(VK_MIDI_SRC_CC, 30, RES, NAN, 1)};
    v.k->setMidiMappings(bad, 3); v.set(RES, 33); v.block();
    v.cc(0, 30, 127); v.block();
    CHECK(near(v.get(RES), 33, 1e-4), "out-of-range address, unknown source and NaN range are dropped");
}

static void testChannelVelocityProgram() {
    std::printf("Program change, receive channel, velocity curves\n");
    Rig r;
    r.cc(4, 0, 2); r.cc(4, 32, 3); r.midi(0xC4, 5); r.block();
    auto e = r.ui();
    CHECK(e.size() == 1 && e[0].kind == VK_MIDI_UI_PROGRAM && e[0].number == 5 && e[0].bankMSB == 2 && e[0].bankLSB == 3 && e[0].channel == 4,
          "program 6, bank 2/3 on channel 5 reported");
    r.set(VK_P_GLOBAL_PROGRAM_CHANGE, 0); r.midi(0xC4, 6); r.block();
    CHECK(r.ui().empty(), "program change ignored when disabled");

    r.set(VK_P_GLOBAL_RECEIVE_CHANNEL, 3); r.block();
    r.on(60, 100, 0); r.block();
    CHECK(r.voiceFor(60) < 0, "receive channel 3: channel 1 ignored");
    r.on(62, 100, 2); r.block();
    CHECK(r.voiceFor(62) >= 0, "receive channel 3: channel 3 plays");
    r.set(VK_P_GLOBAL_RECEIVE_CHANNEL, 0); r.k->postCommand({VK_CMD_PANIC, 0, 0.f}); r.run(4);

    struct C { int curve; double want; const char* name; } curves[] = {
        {0, 64.0 / 127.0, "linear"},
        {1, std::lround(std::pow(64.0 / 127.0, 0.6) * 127.0) / 127.0, "soft"},
        {2, std::lround(std::pow(64.0 / 127.0, 1.6) * 127.0) / 127.0, "hard"},
        {3, 100.0 / 127.0, "fixed"}};
    int note = 50;
    for (auto& c : curves) {
        Rig x; x.set(VK_P_GLOBAL_VELOCITY_CURVE, static_cast<float>(c.curve)); x.block();
        x.on(note, 64); x.block();
        const int vi = x.voiceFor(note);
        CHECK(vi >= 0 && near(x.k->voice(static_cast<uint32_t>(vi)).context().velocity, c.want, 1e-5), "velocity curve %s: 64 → %.4f", c.name, c.want);
    }
}

// MIDI clock in: start, 24 ppqn at a fixed interval, stop, SPP + continue.
static void testClockIn() {
    std::printf("MIDI clock in\n");
    Rig r;
    const uint64_t iv = 1250;   // 96 BPM
    uint64_t nextTick = 0; double ticks = 0;
    auto feed = [&](int blocks, bool tick = true) {
        for (int b = 0; b < blocks; ++b) {
            while (tick && nextTick < r.frames + BLK) { r.midi(0xF8, 0, 0, static_cast<uint32_t>(nextTick - r.frames)); nextTick += iv; ticks += 1; }
            if (!tick) nextTick = r.frames + BLK;
            r.block();
        }
    };
    r.midi(0xFA); feed(200);
    VKMidiStatus s{}; r.k->midiStatus(s);
    CHECK(near(s.clockTempo, 96.0, 0.01) && near(r.k->tempo(), 96.0, 0.01), "tempo measured from clock: %.3f BPM", s.clockTempo);
    CHECK(s.clockRunning && s.clockActive && s.syncSource == 2, "clock running, active, sync source = MIDI clock");
    CHECK(r.k->isPlaying(), "start → transport playing");
    const double expect = (static_cast<double>(r.frames) - 0.0) / iv / 24.0;
    CHECK(near(r.k->clockBeat(), expect, 1.0 / 24.0), "beat follows the ticks (%.4f vs %.4f)", r.k->clockBeat(), expect);
    // Jittered clock still averages to the tempo.
    uint64_t jitterSeed = 7;
    for (int b = 0; b < 400; ++b) {
        while (nextTick < r.frames + BLK) {
            jitterSeed = jitterSeed * 6364136223846793005ull + 1442695040888963407ull;
            const int j = static_cast<int>((jitterSeed >> 33) % 81) - 40;
            const uint64_t t = static_cast<uint64_t>(std::max<int64_t>(static_cast<int64_t>(r.frames), static_cast<int64_t>(nextTick) + j));
            r.midi(0xF8, 0, 0, static_cast<uint32_t>(std::min<uint64_t>(t - r.frames, BLK - 1))); nextTick += iv;
        }
        r.block();
    }
    r.k->midiStatus(s);
    CHECK(near(s.clockTempo, 96.0, 0.6), "±40-sample jitter smoothed (%.3f BPM)", s.clockTempo);

    r.midi(0xFC); feed(4);
    r.k->midiStatus(s);
    CHECK(!s.clockRunning && s.clockActive && !r.k->isPlaying(), "stop: clock still active, transport stopped");
    r.midi(0xF2, 16, 0); r.midi(0xFB, 0, 0, 1); feed(1);
    feed(20);
    const double since = static_cast<double>(r.frames) - 0;
    (void)since;
    CHECK(r.k->clockBeat() >= 4.0 && r.k->clockBeat() < 4.0 + 25.0 * BLK / iv / 24.0 + 1.0 / 24.0, "SPP 16 sixteenths + continue → beat 4 (%.3f)", r.k->clockBeat());

    r.set(VK_P_GLOBAL_SYNC_SOURCE, 1); feed(4);
    r.k->midiStatus(s);
    CHECK(s.syncSource == 0 && near(r.k->tempo(), r.get(VK_P_GLOBAL_TEMPO), 1e-6), "sync source Internal ignores the clock");
    r.set(VK_P_GLOBAL_SYNC_SOURCE, 0);
    r.hostTempo = 140; r.hostPlay = true; feed(4);
    r.k->midiStatus(s);
    CHECK(s.syncSource == 1 && near(r.k->tempo(), 140, 1e-9), "Auto prefers the host's transport over MIDI clock");
    r.hostTempo = 0; feed(4);
    r.k->midiStatus(s);
    CHECK(s.syncSource == 2, "host context gone → MIDI clock again");
    feed(110, false);
    r.k->midiStatus(s);
    CHECK(!s.clockActive && s.syncSource == 0, "no ticks for 0.5 s → clock inactive, internal tempo");
}

static void testClockOut() {
    std::printf("MIDI clock out\n");
    Rig r;
    r.set(VK_P_GLOBAL_TEMPO, 120); r.set(VK_P_GLOBAL_MIDI_CLOCK_OUT, 1);
    r.run(static_cast<int>(SR / BLK) + 1);   // ~1 s
    std::vector<uint64_t> t;
    int starts = 0;
    for (auto& e : r.out) { if (e.a == 0xF8) t.push_back(e.at); if (e.a == 0xFA) ++starts; }
    CHECK(starts == 1 && r.out.front().a == 0xFA && r.out.front().len == 1, "start sent first when enabled");
    bool even = t.size() > 40;
    for (size_t i = 1; i < t.size(); ++i) even = even && (t[i] - t[i - 1] == 1000 || t[i] - t[i - 1] == 999 || t[i] - t[i - 1] == 1001);
    CHECK(even && t.size() >= 48 && t.size() <= 49, "24 ppqn at 120 BPM: every 1000 samples (%zu ticks in ~1 s)", t.size());
    r.set(VK_P_GLOBAL_MIDI_CLOCK_OUT, 0); r.out.clear(); r.run(8);
    CHECK(!r.out.empty() && r.out.front().a == 0xFC && std::none_of(r.out.begin(), r.out.end(), [](const OutEv& e) { return e.a == 0xF8; }), "stop sent when disabled; no more ticks");

    // Locked to the host's beat grid.
    Rig h;
    h.set(VK_P_GLOBAL_MIDI_CLOCK_OUT, 1);
    h.hostTempo = 120; h.hostBeat = 0.1; h.hostPlay = true;
    h.run(40);
    std::vector<uint64_t> ht; for (auto& e : h.out) if (e.a == 0xF8) ht.push_back(e.at);
    bool grid = !ht.empty();
    for (size_t i = 0; i < ht.size(); ++i) grid = grid && (ht[i] + 1 >= 600 + 1000 * i && ht[i] <= 601 + 1000 * i);
    CHECK(grid, "host from beat 0.1: first tick at sample 600 (beat 0.125), then every 1000 (%zu)", ht.size());
    // Host loop back to beat 0: ticks re-align.
    h.out.clear(); h.hostBeat = 0.0; const uint64_t at = h.frames; h.run(8);
    ht.clear(); for (auto& e : h.out) if (e.a == 0xF8) ht.push_back(e.at - at);
    CHECK(!ht.empty() && ht[0] == 0 && (ht.size() < 2 || near(static_cast<double>(ht[1]), 1000, 1)), "relocation: tick on the downbeat at offset 0");
}

static void testMidiOut() {
    std::printf("MIDI out of notes\n");
    Rig r;
    r.set(VK_P_GLOBAL_TEMPO, 120); r.set(VK_P_GLOBAL_MIDI_OUT, 1); r.set(VK_P_GLOBAL_MIDI_OUT_CHANNEL, 3);
    r.set(VK_P_ARP_MODE, 1); r.set(VK_P_ARP_RATE, 4);   // 1/16 = 6000 samples
    r.block();
    r.on(60, 100, 0, 37); r.on(64, 100, 0, 37);
    r.run(200);
    std::vector<OutEv> ons, offs;
    for (auto& e : r.out) { if ((e.a & 0xF0) == 0x90) ons.push_back(e); if ((e.a & 0xF0) == 0x80) offs.push_back(e); }
    bool spaced = ons.size() >= 8;
    for (size_t i = 1; i < ons.size(); ++i) spaced = spaced && ons[i].at - ons[i - 1].at == 6000;
    CHECK(spaced, "arp notes out every 6000 samples, sample-accurate (%zu notes)", ons.size());
    CHECK(std::all_of(ons.begin(), ons.end(), [](const OutEv& e) { return e.a == 0x93; }) && !offs.empty() && offs[0].a == 0x83, "on MIDI channel 4");
    CHECK(ons.size() >= 2 && ons[0].b == 60 && ons[1].b == 64, "arp order C E");
    r.off(60); r.off(64); r.run(40);
    int balance = 0; for (auto& e : r.out) { if ((e.a & 0xF0) == 0x90) ++balance; if ((e.a & 0xF0) == 0x80) --balance; }
    CHECK(balance == 0, "every note-on sent has its note-off");

    Rig k;
    k.set(VK_P_GLOBAL_MIDI_OUT, 1); k.block();
    k.on(70, 90, 0, 100); k.block();
    CHECK(k.out.empty(), "Generated Notes: played keys are not echoed");
    k.set(VK_P_GLOBAL_MIDI_OUT, 2); k.on(72, 90, 0, 100); k.block(); k.off(72, 0, 50); k.block();
    CHECK(k.out.size() == 2 && k.out[0].a == 0x90 && k.out[0].b == 72 && k.out[0].c == 90 && k.out[0].at == BLK * 2 + 100
          && k.out[1].a == 0x80 && k.out[1].at == BLK * 3 + 50, "All Notes: keys echoed at their sample offsets");
}

static void testUmp() {
    std::printf("MIDI 2.0 UMP\n");
    Rig r;
    r.ump(U4(0x9, 0, 60, 0), 0x8000u << 16); r.block();
    int vi = r.voiceFor(60);
    CHECK(vi >= 0 && near(r.k->voice(static_cast<uint32_t>(vi)).context().velocity, 32768.0 / 65535.0, 1e-6),
          "16-bit velocity kept at full resolution (%.6f)", vi >= 0 ? r.k->voice(static_cast<uint32_t>(vi)).context().velocity : -1.f);
    r.ump(U4(0x9, 0, 61, 0), 0); r.block();
    CHECK(r.voiceFor(61) >= 0, "velocity 0 is a note-on in MIDI 2.0");
    r.ump(U4(0x8, 0, 61, 0), 0); r.block();
    CHECK(r.voiceFor(61) < 0 || !r.k->voice(static_cast<uint32_t>(r.voiceFor(61))).isHeld(), "MIDI 2.0 note-off releases");

    r.ump(U4(0x9, 0, 64, 0), 0xFFFFu << 16); r.block();
    r.ump(U4(0x6, 0, 64, 0), 0xC0000000u); r.block();
    const int a = r.voiceFor(60), b = r.voiceFor(64);
    CHECK(a >= 0 && b >= 0 && near(r.k->voice(static_cast<uint32_t>(b)).context().noteBendSemis, 0.5 * r.get(VK_P_GLOBAL_MPE_PITCH_RANGE), 1e-3)
          && r.k->voice(static_cast<uint32_t>(a)).context().noteBendSemis == 0.f, "per-note bend moves only note 64 (+24 st)");
    r.ump(U4(0x1, 0, 60, 74), 0x40000000u); r.block();
    CHECK(near(r.k->voice(static_cast<uint32_t>(a)).timbre(), 0.25, 1e-6) && near(r.k->voice(static_cast<uint32_t>(b)).timbre(), 0.5, 1e-6),
          "per-note controller 74 → timbre of note 60 only");
    r.ump(U4(0xF, 0, 64, 1), 0); r.ump(U4(0xF, 0, 60, 1), 0); r.block();
    CHECK(r.k->voice(static_cast<uint32_t>(b)).context().noteBendSemis == 0.f && near(r.k->voice(static_cast<uint32_t>(a)).timbre(), 0.5, 1e-6),
          "per-note management reset returns bend and timbre to default");
    r.ump(U4(0xA, 0, 64, 0), 0x80000000u); r.block();

    r.k->midiLearn(true);
    r.ump(U4(0xB, 2, 30, 0), 0x40000000u); r.block();
    auto e = r.ui();
    CHECK(e.size() == 1 && e[0].source == VK_MIDI_SRC_CC && e[0].number == 30 && e[0].channel == 2, "32-bit CC learnt as CC 30");
    r.map({M(VK_MIDI_SRC_CC, 30, RES)}); r.block();
    r.ump(U4(0xB, 2, 30, 0), 0x40000000u); r.block();
    CHECK(near(r.get(RES), 25.0, 1e-3), "32-bit controller value kept (25.000 %%, a 7-bit CC can't): %.4f", r.get(RES));
    r.ump(U4(0xB, 0, 1, 0), 0xFFFFFFFFu); r.block();
    CHECK(near(r.k->controller(MS_MODWHEEL), 1.0, 1e-6), "32-bit mod wheel");

    r.ump(U4(0x2, 0, 0, 0), (12u << 7) << 18); r.block();
    CHECK(near(r.get(VK_P_GLOBAL_PITCH_BEND_RANGE), 12, 1e-4), "MIDI 2.0 registered controller 0: bend range 12");
    r.map({M(VK_MIDI_SRC_NRPN, (3 << 7) | 9, CUT)}); r.block();
    r.ump(U4(0x3, 0, 3, 9), 0xFFFFFFFFu); r.block();
    CHECK(near(r.get(CUT), 20000, 1), "MIDI 2.0 assignable controller (NRPN 3/9) drives a mapping");
    r.ump(U4(0xC, 1, 0, 1), (9u << 24) | (4u << 8) | 2u); r.block();
    e = r.ui();
    CHECK(!e.empty() && e.back().kind == VK_MIDI_UI_PROGRAM && e.back().number == 9 && e.back().bankMSB == 4 && e.back().bankLSB == 2,
          "MIDI 2.0 program change with bank");
    r.ump(U4(0xE, 0, 0, 0), 0xFFFFFFFFu); r.block();
    r.ump(0x20000000u | (0x90u << 16) | (67u << 8) | 80u, 0); r.block();
    CHECK(r.voiceFor(67) >= 0, "MIDI 1.0 channel voice in UMP (type 2) plays");
    r.ump(0x10000000u | (0xFAu << 16), 0); r.block();
    VKMidiStatus s{}; r.k->midiStatus(s);
    CHECK(s.messagesIn > 15, "messages counted (%u)", s.messagesIn);

    // MPE zone: UMP is down-converted, hi-res velocity survives.
    Rig m;
    m.set(VK_P_GLOBAL_MPE_MODE, 1); m.block();
    m.ump(U4(0x9, 2, 60, 0), 0x4000u << 16); m.block();
    vi = m.voiceFor(60);
    CHECK(vi >= 0 && m.k->voice(static_cast<uint32_t>(vi)).channel() == 2 && near(m.k->voice(static_cast<uint32_t>(vi)).context().velocity, 16384.0 / 65535.0, 1e-6),
          "MPE member channel via UMP: per-channel note, hi-res velocity");
    m.map({M(VK_MIDI_SRC_CC, 74, RES), M(VK_MIDI_SRC_PITCH_BEND, 0, CUT)}); m.set(RES, 10); m.block();
    m.cc(2, 74, 127); m.midi(0xE2, 0x7F, 0x7F); m.block();
    CHECK(near(m.get(RES), 10, 1e-4) && near(m.k->voice(static_cast<uint32_t>(vi)).timbre(), 1.0, 1e-6), "MPE member CC 74 is per-note timbre, not mapped globally");
    m.cc(0, 74, 127); m.block();
    CHECK(near(m.get(RES), 100, 1e-4), "CC 74 on the master channel still drives its mapping");
    m.ump(U4(0xE, 2, 0, 0), 0xFFFFFFFFu); m.block();
    CHECK(near(m.k->voice(static_cast<uint32_t>(vi)).context().noteBendSemis, m.get(VK_P_GLOBAL_MPE_PITCH_RANGE) * 8191.0 / 8192.0, 1e-2),
          "MPE channel bend from UMP (%.3f st)", m.k->voice(static_cast<uint32_t>(vi)).context().noteBendSemis);
}

// Floods, RT safety, concurrency.
static void testRealtime() {
    std::printf("Real-time safety\n");
    Rig r;
    std::vector<VKMidiMapping> maps;
    for (int i = 0; i < 200; ++i) maps.push_back(M(static_cast<uint8_t>(i % 5), static_cast<uint16_t>(i % 120), RES, 0, 1, static_cast<uint8_t>(i % 3), static_cast<uint8_t>(i % 3), i % 2 == 0));
    r.map(maps);
    r.set(VK_P_GLOBAL_MIDI_OUT, 2); r.set(VK_P_GLOBAL_MIDI_CLOCK_OUT, 1); r.set(VK_P_ARP_MODE, 1);
    r.run(4);
    uint64_t seed = 99;
    auto rnd = [&](uint32_t n) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return static_cast<uint32_t>((seed >> 33) % n); };
    auto flood = [&](int n) {
        for (int i = 0; i < n; ++i) {
            const uint32_t off = rnd(BLK), kind = rnd(6);
            if (kind == 0) r.on(static_cast<int>(36 + rnd(48)), static_cast<int>(1 + rnd(127)), static_cast<int>(rnd(16)), off);
            else if (kind == 1) r.off(static_cast<int>(36 + rnd(48)), static_cast<int>(rnd(16)), off);
            else if (kind == 2) r.cc(static_cast<int>(rnd(16)), static_cast<int>(rnd(128)), static_cast<int>(rnd(128)), off);
            else if (kind == 3) r.midi(0xF8, 0, 0, off);
            else if (kind == 4) r.ump(U4(rnd(16), rnd(16), rnd(128), rnd(256)), static_cast<uint32_t>(rnd(0xFFFFFFFFu)), off);
            else r.midi(static_cast<uint8_t>(0xE0 | rnd(16)), static_cast<uint8_t>(rnd(128)), static_cast<uint8_t>(rnd(128)), off);
        }
    };
    flood(64); r.block(); r.ui();
    gAllocs = 0; gTrack = true;
    for (int b = 0; b < 300; ++b) { flood(64); r.block(); }
    gTrack = false;
    CHECK(gAllocs == 0, "no allocation while rendering MIDI floods with learn, MIDI out and clock (%ld)", gAllocs.load());
    bool finite = true; for (float x : r.L) finite = finite && std::isfinite(x);
    CHECK(finite, "output finite after a flood");
    r.k->postCommand({VK_CMD_PANIC, 0, 0.f}); r.run(4);
    flood(4000); r.block();
    const VKMidiOutEvent* ev = nullptr;
    CHECK(r.k->midiOutput(&ev) <= VK_MIDI_OUT_MAX, "MIDI out bounded at %u per buffer", VK_MIDI_OUT_MAX);

    // UI thread vs render thread (run under TSan).
    std::printf("  concurrency (UI ⇄ render)\n");
    auto k = std::make_unique<Kernel>();
    k->allocate(SR, 2, 4096);
    std::atomic<bool> stop{false};
    std::thread ui([&] {
        uint32_t i = 0; VKMidiUIEvent e; uint64_t a[64]; VKMidiStatus s{};
        while (!stop.load()) {
            VKMidiMapping mm[3] = {M(VK_MIDI_SRC_CC, static_cast<uint16_t>(i % 100), RES), M(VK_MIDI_SRC_NRPN, 5, CUT), M(VK_MIDI_SRC_NOTE, 40, RES, 0, 1, 1)};
            k->setMidiMappings(mm, 1 + i % 3);
            k->midiLearn(i % 2 == 0);
            while (k->popMidiUi(e)) {}
            k->takeMidiChanges(a, 64);
            k->midiStatus(s);
            k->setParam(VK_P_GLOBAL_MIDI_CLOCK_OUT, static_cast<float>(i % 2));
            ++i; std::this_thread::yield();
        }
    });
    std::vector<float> L(BLK), R(BLK); float* ch[2] = {L.data(), R.data()};
    std::vector<VKEvent> evs(64);
    for (int b = 0; b < 3000; ++b) {
        for (uint32_t i = 0; i < evs.size(); ++i) {
            VKEvent& x = evs[i]; x = VKEvent{}; x.type = VK_EVENT_MIDI1; x.length = 3; x.sampleOffset = i * 4;
            x.data[0] = static_cast<uint8_t>(i % 3 == 0 ? 0xB0 : i % 3 == 1 ? 0x90 : 0x80); x.data[1] = static_cast<uint8_t>((b + i) % 100); x.data[2] = static_cast<uint8_t>((b * 7 + i) % 128);
        }
        k->render(ch, 2, BLK, evs.data(), static_cast<uint32_t>(evs.size()));
    }
    stop = true; ui.join();
    CHECK(true, "3000 buffers with concurrent mapping publication, learn and status reads");
}

int main() {
    testRpn();
    testMappings();
    testChannelVelocityProgram();
    testClockIn();
    testClockOut();
    testMidiOut();
    testUmp();
    testRealtime();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
