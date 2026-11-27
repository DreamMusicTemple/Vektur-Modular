// test_sequencing.cpp — Phase 5: arpeggiator, step sequencer, generative
// engine, MIDI capture/export, preset creator; timing, host sync, stuck-note
// and real-time safety.
#include "VekturDSP.h"
#include "Kernel.hpp"
#include "ParamTable.hpp"
#include "seq/SeqCommon.hpp"
#include "seq/Generative.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <set>
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
// std::stable_sort's temporary buffer uses the nothrow forms; route them to the same heap.
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { if (gTrack.load(std::memory_order_relaxed)) gAllocs.fetch_add(1); return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return operator new(n, t); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...) do { ++gChecks; if (!(cond)) { ++gFailures; \
    std::printf("  FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("  ok   "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using namespace vektur;
constexpr double SR = 48000.0;
constexpr uint32_t BLK = 256;   // notes fall mid-block, so sample accuracy is exercised
constexpr uint64_t ARP = 36 * 256, SEQ = 37 * 256, GEN = 38 * 256, MATRIX = 60 * 256;
static uint64_t step(int s, int f) { return SEQ + 32 + static_cast<uint64_t>(s) * 8 + static_cast<uint64_t>(f); }

struct Rig {
    std::unique_ptr<Kernel> k = std::make_unique<Kernel>();
    std::vector<float> L = std::vector<float>(BLK), R = std::vector<float>(BLK);
    std::vector<VKEvent> pending;
    std::vector<VKNoteRecord> recs;
    double hostTempo = 0, hostBeat = 0; bool hostPlay = false;
    uint64_t frames = 0;
    Rig(bool capture = true) {
        k->allocate(SR, 2, 4096); pending.reserve(512); recs.reserve(20000);
        if (capture) k->postCommand({VK_CMD_CAPTURE, 1, 0.f});
    }
    void set(uint64_t a, float v) { k->setParam(a, v); }
    void midi(uint8_t a, uint8_t b, uint8_t c, uint32_t offset = 0) {
        VKEvent e{}; e.type = VK_EVENT_MIDI1; e.length = 3; e.data[0] = a; e.data[1] = b; e.data[2] = c; e.sampleOffset = offset;
        pending.push_back(e);
    }
    void on(int n, int vel = 100, uint32_t off = 0) { midi(0x90, static_cast<uint8_t>(n), static_cast<uint8_t>(vel), off); }
    void off(int n, uint32_t o = 0) { midi(0x80, static_cast<uint8_t>(n), 0, o); }
    void block() {
        float* ch[2] = {L.data(), R.data()};
        if (hostTempo > 0) { k->setTransport(hostTempo, hostBeat, hostPlay); if (hostPlay) hostBeat += BLK / SR * hostTempo / 60.0; }
        k->render(ch, 2, BLK, pending.empty() ? nullptr : pending.data(), static_cast<uint32_t>(pending.size()));
        pending.clear();
        frames += BLK;
        VKNoteRecord r; while (k->popNoteRecord(r)) recs.push_back(r);
    }
    void run(double secs) { const int b = std::max(1, static_cast<int>(std::lround(secs * SR / BLK))); for (int i = 0; i < b; ++i) block(); }
    std::vector<VKNoteRecord> ons(int source = -1) const {
        std::vector<VKNoteRecord> o; for (auto& r : recs) if (r.velocity > 0 && (source < 0 || r.source == source)) o.push_back(r); return o;
    }
    std::vector<int> notes(int source = -1) const { std::vector<int> n; for (auto& r : ons(source)) n.push_back(r.note); return n; }
    uint32_t held() const { uint32_t n = 0; for (uint32_t i = 0; i < VK_MAX_VOICES; ++i) n += k->voice(i).isHeld() ? 1u : 0u; return n; }
};

static bool balanced(const std::vector<VKNoteRecord>& recs) {
    std::map<int, int> open;
    for (auto& r : recs) { if (r.velocity) ++open[r.note]; else if (open[r.note] > 0) --open[r.note]; }
    for (auto& [n, c] : open) if (c) return false;
    return true;
}
static std::string fmt(const std::vector<int>& v, size_t n = 12) {
    std::string s; for (size_t i = 0; i < std::min(n, v.size()); ++i) { s += std::to_string(v[i]); s += ' '; } return s;
}

// --------------------------------------------------------------------------
static void testTables() {
    std::puts("Tables, scales, Euclid, counter RNG");
    CHECK(VK_PARAM_COUNT == 737 && VK_PARAM_COUNT == sizeof(kParamTable) / sizeof(kParamTable[0]), "%u parameters (Phase 5 added 149: arp 24, sequencer 109, generative 16)", VK_PARAM_COUNT);
    CHECK(VK_P_ARP_MODE == 9216 && VK_P_SEQ_RUN == 9472 && VK_P_GEN_MODE == 9728 && VK_P_SEQ_STEP16_RATCHET == 9472 + 32 + 15 * 8 + 5, "modules 36–38 at their reserved addresses");
    ParameterStore ps; for (const auto& d : kParamTable) ps.registerParam({d.address, d.minValue, d.maxValue, d.defaultValue, d.smoothingMs, d.flags});
    CHECK(!ps.isModulatable(VK_P_ARP_GATE) && !ps.isModulatable(VK_P_SEQ_STEP1_A) && !ps.isModulatable(VK_P_GEN_DENSITY), "sequencing parameters are not per-voice matrix destinations");
    CHECK(VK_MOD_SOURCE_COUNT == 26 && MS_SEQ_A == 23 && MS_SEQ_C == 25, "sources appended: Seq Row A/B/C = 23–25");
    const int sizes[16] = {12, 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 6, 7, 7};
    bool ok = true; for (int i = 0; i < 16; ++i) ok &= seq::makeScale(i).size == sizes[i] && (seq::makeScale(i).mask & 1);
    CHECK(ok, "16 scales, sizes match their definitions, all contain the root");
    const seq::Scale maj = seq::makeScale(1);
    CHECK(seq::quantise(61, 0, maj) == 60 && seq::quantise(66, 0, maj) == 65 && seq::quantise(70, 2, maj) == 69 && seq::quantise(64, 0, maj) == 64,
          "quantiser: C#→C, F#→F (ties down), in D major A#→A, in-scale unchanged");
    CHECK(seq::degreePitch(0, maj, 7) == 12 && seq::degreePitch(0, maj, -1) == -1 && seq::degreePitch(9, seq::makeScale(2), 2) == 12, "degree → pitch across octaves (A minor 3rd degree = C)");
    std::string e38, e516;
    for (int i = 0; i < 8; ++i) e38 += seq::euclidHit(8, 3, 0, i) ? 'x' : '.';
    for (int i = 0; i < 16; ++i) e516 += seq::euclidHit(16, 5, 0, i) ? 'x' : '.';
    int c516 = 0; for (char ch : e516) c516 += ch == 'x';
    CHECK(e38 == "x..x..x." && c516 == 5, "Euclid E(3,8) = %s, E(5,16) = %s", e38.c_str(), e516.c_str());
    std::string rot; for (int i = 0; i < 8; ++i) rot += seq::euclidHit(8, 3, 1, i) ? 'x' : '.';
    CHECK(rot == ".x..x..x", "rotation shifts the pattern right (%s)", rot.c_str());
    CHECK(seq::rand01(7, 1, 2) == seq::rand01(7, 1, 2) && seq::rand01(7, 1, 2) != seq::rand01(8, 1, 2), "counter RNG: same inputs → same value, seed changes it");
    CHECK(std::fabs(seq::swungTick(0, 0.25, 1, 66.6667f) - (0.25 + 0.0833333)) < 1e-5 && seq::swungTick(0, 0.25, 2, 75.f) == 0.5, "swing moves odd ticks only (66.7 %% → +1/3 step)");
}

static void testArpPatterns() {
    std::puts("Arpeggiator patterns");
    Rig r(false);
    r.set(ARP, 1); r.set(ARP + 2, 1);   // on, 2 octaves
    r.on(67); r.on(60); r.on(64); r.block();
    const auto& a = r.k->arpeggiator();
    int8_t out[256];
    auto pat = [&](int p, int om = 0, int oct = 2) { const uint32_t n = a.expand(p, oct, om, out); std::vector<int> v(out, out + n); return v; };
    CHECK(pat(0) == std::vector<int>({60, 64, 67, 72, 76, 79}), "Up over 2 octaves: %s", fmt(pat(0)).c_str());
    CHECK(pat(1) == std::vector<int>({79, 76, 72, 67, 64, 60}), "Down: %s", fmt(pat(1)).c_str());
    CHECK(pat(2) == std::vector<int>({60, 64, 67, 72, 76, 79, 76, 72, 67, 64}), "Up-Down without repeated ends (period 10)");
    CHECK(pat(4).size() == 12 && pat(4)[5] == 79 && pat(4)[6] == 79, "Up-Down repeating ends (period 12)");
    CHECK(pat(6, 0, 1) == std::vector<int>({67, 60, 64}), "As Played keeps key order: %s", fmt(pat(6, 0, 1)).c_str());
    CHECK(pat(9, 0, 1) == std::vector<int>({60, 67, 64}) && pat(9) == std::vector<int>({60, 79, 64, 76, 67, 72}), "Converge: outside in");
    CHECK(pat(10, 0, 1) == std::vector<int>({64, 67, 60}), "Diverge: middle out: %s", fmt(pat(10, 0, 1)).c_str());
    CHECK(pat(11, 0, 1) == std::vector<int>({60, 64, 60, 67}) && pat(12, 0, 1) == std::vector<int>({67, 60, 67, 64}), "Thumb and Pinky pedal notes");
    CHECK(pat(0, 1) == std::vector<int>({72, 76, 79, 60, 64, 67}) && pat(0, 2, 3).size() == 12, "octave direction Down / Up-Down (3 oct → 0 1 2 1)");
}

static void testArpTiming() {
    std::puts("Arpeggiator timing (free clock, key restart)");
    Rig r; r.set(VK_P_GLOBAL_TEMPO, 120); r.set(ARP, 1); r.set(ARP + 4, 4);   // 1/16 = 6000 samples at 120 BPM
    r.run(0.05);
    const double t0 = r.k->clockBeat();
    r.on(60, 100, 77); r.on(64, 100, 77); r.on(67, 100, 77);
    r.run(2.0);
    const auto o = r.ons(VK_SOURCE_ARP);
    const double spb = SR * 60.0 / 120.0;
    double maxErr = 0; bool grid = o.size() >= 15;
    const double first = (o.empty() ? 0 : o[0].beat);
    for (size_t i = 0; i < o.size(); ++i) {
        const double expect = first + 0.25 * static_cast<double>(i);
        maxErr = std::max(maxErr, std::fabs(o[i].beat - expect) * spb);
    }
    CHECK(!o.empty() && std::fabs((first - t0) * spb - 77.0) < 0.5, "first note on the key's own sample (offset %.2f samples, expect 77)", (first - t0) * spb);
    CHECK(grid && maxErr < 0.5, "%zu steps exactly 6000 samples apart (max error %.3f samples)", o.size(), maxErr);
    CHECK(r.notes(VK_SOURCE_ARP)[0] == 60 && r.notes(VK_SOURCE_ARP)[1] == 64 && r.notes(VK_SOURCE_ARP)[3] == 60, "Up order C E G C …");
    // Gate 50 %: note-off half a step later.
    double offAfter = -1; for (auto& x : r.recs) if (x.source == VK_SOURCE_ARP && x.velocity == 0) { offAfter = x.beat - first; break; }
    CHECK(std::fabs(offAfter - 0.125) < 1e-6, "gate 50 %% → note-off after 1/32 (%.4f beats)", offAfter);
    CHECK(r.held() <= 1, "keys are not sounded directly while the arp has them (held voices %u)", r.held());
    r.off(60); r.off(64); r.off(67); r.run(0.3);
    CHECK(balanced(r.recs) && r.held() == 0, "release: arp stops, every note-on has its note-off");
}

static void testArpHostSync() {
    std::puts("Arpeggiator host sync and relocation");
    Rig r; r.hostTempo = 128; r.hostBeat = 3.1; r.hostPlay = true;
    r.set(ARP, 1); r.set(ARP + 4, 3);   // 1/8
    r.run(0.02); r.on(48); r.on(55); r.run(1.5);
    auto o = r.ons(VK_SOURCE_ARP);
    double maxOff = 0; for (auto& x : o) maxOff = std::max(maxOff, std::fabs(x.beat * 2 - std::round(x.beat * 2)));
    const double samplesPerBeat = SR * 60 / 128;
    CHECK(o.size() >= 5 && maxOff * 0.5 * samplesPerBeat < 0.5, "%zu notes on the host's 1/8 grid (max %.3f samples off)", o.size(), maxOff * 0.5 * samplesPerBeat);
    // Locate: host jumps back to beat 1.0.
    const size_t before = r.recs.size();
    r.hostBeat = 1.0; r.run(0.6);
    std::vector<VKNoteRecord> after(r.recs.begin() + static_cast<long>(before), r.recs.end());
    bool onGrid = true; size_t n = 0;
    for (auto& x : after) if (x.velocity && x.source == VK_SOURCE_ARP) { ++n; onGrid &= std::fabs(x.beat * 2 - std::round(x.beat * 2)) < 1e-6; }
    CHECK(n >= 3 && onGrid && after[0].beat < 1.5, "after locating to beat 1 the arp continues on the grid from there (%zu notes)", n);
    CHECK(balanced(std::vector<VKNoteRecord>(r.recs.begin(), r.recs.begin() + static_cast<long>(before))) || r.recs[before].velocity == 0,
          "relocation flushes the note sounding at the locate");
    r.hostPlay = false; r.run(0.5);
    CHECK(r.ons(VK_SOURCE_ARP).size() > o.size() + n, "transport stopped: the arp keeps running on the free clock");
    r.off(48); r.off(55); r.run(0.3);
    CHECK(balanced(r.recs) && r.held() == 0, "no hanging notes across locate, stop and release");
}

static void testArpFeatures() {
    std::puts("Arpeggiator: gate, swing, Euclid, probability, ratchets, chaining, latch, velocity");
    auto make = [](auto&& cfg, std::vector<int> keys, double secs) {
        auto r = std::make_unique<Rig>(); r->set(VK_P_GLOBAL_TEMPO, 120); r->set(ARP, 1); r->set(ARP + 4, 4);
        cfg(*r); r->run(0.01); for (int kk : keys) r->on(kk); r->run(secs); return r;
    };
    { auto r = make([](Rig& x) { x.set(ARP + 5, 150); }, {60}, 1.0);
      bool continuous = true; for (int i = 0; i < 30; ++i) { r->run(0.01); continuous &= r->held() == 1; }
      CHECK(continuous && r->ons().size() > 8, "gate 150 %%: repeated notes overlap legato and are never cut short"); }
    { auto r = make([](Rig& x) { x.set(ARP + 6, 66.6667f); }, {60, 64}, 1.0);
      auto o = r->ons(); const double d1 = o[1].beat - o[0].beat, d2 = o[2].beat - o[1].beat;
      CHECK(std::fabs(d1 - 0.3333333) < 1e-4 && std::fabs(d2 - 0.1666667) < 1e-4, "swing 66.7 %%: long-short pairs (%.4f / %.4f beats)", d1, d2); }
    { auto r = make([](Rig& x) { x.set(ARP + 7, 1); x.set(ARP + 8, 7); x.set(ARP + 9, 3); x.set(ARP + 16, 1); }, {60, 64, 67}, 4.0);
      auto o = r->ons(); std::set<int> pos; for (auto& x : o) pos.insert(static_cast<int>(std::lround(x.beat / 0.25)) % 8);
      const auto ns = r->notes();
      CHECK(pos == std::set<int>({0, 3, 6}) && ns[0] == 60 && ns[1] == 64 && ns[2] == 67, "Euclid E(3,8) beat-locked: hits on steps 0, 3, 6; rests do not skip notes"); }
    { auto r1 = make([](Rig& x) { x.set(ARP + 11, 50); }, {60, 64, 67}, 10.0);
      auto r2 = make([](Rig& x) { x.set(ARP + 11, 50); }, {60, 64, 67}, 10.0);
      auto r3 = make([](Rig& x) { x.set(ARP + 11, 50); x.set(ARP + 17, 99); }, {60, 64, 67}, 10.0);
      const size_t n = r1->ons().size();
      CHECK(n > 28 && n < 52, "probability 50 %%: %zu of 80 steps", n);
      bool same = r1->ons().size() == r2->ons().size(); for (size_t i = 0; same && i < n; ++i) same = std::fabs(r1->ons()[i].beat - r2->ons()[i].beat - (r1->ons()[0].beat - r2->ons()[0].beat)) < 1e-9;
      CHECK(same && r3->notes() != r1->notes(), "seeded: identical on replay, a different seed gives a different pattern"); }
    { auto r = make([](Rig& x) { x.set(ARP + 12, 3); }, {60}, 0.26);
      auto o = r->ons();
      int offs = 0; for (auto& x : r->recs) offs += x.velocity == 0 && x.beat < o[0].beat + 0.25;
      CHECK(o.size() >= 4 && std::fabs(o[1].beat - o[0].beat - 0.0625) < 1e-6 && std::fabs(o[3].beat - o[0].beat - 0.1875) < 1e-6 && offs == 4,
            "ratchet 4×: four separately articulated hits per step, 1/64 apart (%d note-offs)", offs); }
    { auto r = make([](Rig& x) { x.set(ARP + 18, 1); x.set(ARP + 19, 1); }, {60, 64, 67}, 1.6);
      const auto ns = r->notes();
      CHECK(fmt(ns, 9) == "60 64 67 67 64 60 60 64 67 ", "chain Up → Down, one pass each: %s", fmt(ns, 9).c_str()); }
    { auto r = make([](Rig& x) { x.set(ARP, 2); }, {60, 67}, 0.3);
      r->off(60); r->off(67);
      const size_t atRelease = r->ons().size();
      r->run(1.5);
      const size_t latched = r->ons().size() - atRelease;
      CHECK(latched >= 11, "latch: keeps playing after the keys are released (%zu notes in 1.5 s)", latched);
      r->on(72); r->run(1.0); r->off(72); r->run(1.0);
      const auto ns = r->notes(); bool only72 = true; for (size_t i = ns.size() - 6; i < ns.size(); ++i) only72 &= ns[i] == 72;
      CHECK(only72, "latch: a new hand replaces the latched chord");
      r->set(ARP, 0); r->run(0.2);
      CHECK(r->held() == 0 && balanced(r->recs), "turning the arp off flushes every note"); }
    { auto r = make([](Rig& x) { x.set(ARP + 14, 1); x.set(ARP + 15, 33); }, {60}, 0.5);
      bool fixed = true; for (auto& x : r->ons()) fixed &= x.velocity == 33;
      auto ra = make([](Rig& x) { x.set(ARP + 14, 2); x.set(ARP + 16, 1); }, {60}, 1.0);
      int hi = 0, lo = 0; for (auto& x : ra->ons()) (std::fabs(x.beat - std::round(x.beat)) < 1e-6 ? hi : lo) += x.velocity;
      CHECK(fixed && hi > 0 && lo > 0, "fixed velocity 33; accent mode emphasises the beat"); }
    { auto r = make([](Rig&) {}, {60}, 0.05);
      r->midi(0xB0, 64, 127); r->on(64); r->run(0.05); r->off(64); r->off(60); r->run(1.0);
      const auto ns = r->notes(); bool has64 = std::count(ns.end() - 4, ns.end(), 64) == 2 && std::count(ns.end() - 4, ns.end(), 60) == 2;
      r->midi(0xB0, 64, 0); r->run(0.3);
      const size_t n = r->ons().size(); r->run(0.5);
      CHECK(has64 && r->ons().size() == n && balanced(r->recs), "sustain pedal holds released keys in the arp; pedal up lets them go"); }
}

static void testStepSequencer() {
    std::puts("Step sequencer");
    auto make = [](auto&& cfg, double secs, int run = 1) {
        auto r = std::make_unique<Rig>(); r->set(VK_P_GLOBAL_TEMPO, 120);
        r->set(SEQ + 1, 3); r->set(SEQ + 6, 0);   // 4 steps, 1-octave range
        const float a[4] = {0, 25, 50, 100};
        for (int s = 0; s < 4; ++s) r->set(step(s, 0), a[s]);
        cfg(*r); r->run(0.01); r->set(SEQ, static_cast<float>(run)); r->run(secs); return r;
    };
    { auto r = make([](Rig&) {}, 1.0);
      CHECK(fmt(r->notes(VK_SOURCE_SEQ), 8) == "48 51 54 60 48 51 54 60 ", "row A 0/25/50/100 %% over one octave: %s", fmt(r->notes(VK_SOURCE_SEQ), 8).c_str());
      auto o = r->ons(VK_SOURCE_SEQ); bool grid = true; for (auto& x : o) grid &= std::fabs(x.beat * 4 - std::round(x.beat * 4)) < 1e-6;
      CHECK(grid, "Run mode is locked to the 1/16 grid of the clock"); }
    { auto r = make([](Rig& x) { x.set(SEQ + 7, 1); x.set(SEQ + 8, 0); }, 0.5);
      CHECK(fmt(r->notes(), 4) == "48 50 53 60 ", "quantised to C major: %s", fmt(r->notes(), 4).c_str()); }
    { auto rr = make([](Rig& x) { x.set(SEQ + 2, 1); }, 0.5), rp = make([](Rig& x) { x.set(SEQ + 2, 2); }, 1.0);
      CHECK(fmt(rr->notes(), 4) == "60 54 51 48 " && fmt(rp->notes(), 8) == "48 51 54 60 54 51 48 51 ", "reverse %s / ping-pong %s", fmt(rr->notes(), 4).c_str(), fmt(rp->notes(), 8).c_str()); }
    { auto r = make([](Rig& x) { x.set(step(1, 3), 3); }, 0.8);
      const auto o = r->ons();
      CHECK(fmt(r->notes(), 6) == "48 54 60 48 54 60 " && std::fabs(o[1].beat - o[0].beat - 0.25) < 1e-6, "skip: step 2 takes no time (%s)", fmt(r->notes(), 6).c_str()); }
    { auto r = make([](Rig& x) { x.set(step(1, 3), 2); x.set(step(1, 0), 0); }, 0.55);
      double off0 = -1; for (auto& x : r->recs) if (!x.velocity && x.note == 48) { off0 = x.beat - r->ons()[0].beat; break; }
      CHECK(fmt(r->notes(), 3) == "48 54 60 " && std::fabs(off0 - 0.375) < 1e-6, "tie: step 1 held through step 2 (off after %.3f beats = 1.5 steps)", off0); }
    { auto r = make([](Rig& x) { x.set(step(0, 3), 0); x.set(step(2, 5), 2); }, 0.5);
      CHECK(fmt(r->notes(), 5) == "51 54 54 54 60 ", "gate off rests; ratchet 3× on step 3: %s", fmt(r->notes(), 5).c_str()); }
    { auto r = make([](Rig& x) { for (int s = 0; s < 4; ++s) x.set(step(s, 4), 50); }, 8.0);
      const size_t n = r->ons().size();
      CHECK(n > 20 && n < 44, "per-step probability 50 %%: %zu of 64", n); }
    { auto r = make([](Rig& x) { x.set(SEQ + 10, 0); for (int s = 0; s < 4; ++s) x.set(step(s, 1), 20.f * s); }, 0.01);
      r->run(0.13);   // first step at the next 1/16 line
      std::vector<float> seen; for (int i = 0; i < 40; ++i) { r->run(0.0625 / 2); seen.push_back(r->k->modFrame().global[MS_SEQ_B]); }
      std::set<int> vals; for (float v : seen) vals.insert(static_cast<int>(std::lround(v * 100)));
      CHECK(r->ons().empty() && vals == std::set<int>({0, 20, 40, 60}), "mod-only: no notes; Row B steps through 0/20/40/60 %% as a matrix source"); }
}

static void testSequencerRunModes() {
    std::puts("Sequencer run modes, arp chaining");
    { Rig r; r.hostTempo = 120; r.hostPlay = false; r.set(SEQ, 2); r.run(0.5);
      const size_t stopped = r.ons().size();
      r.hostPlay = true; r.run(0.5);
      const size_t playing = r.ons().size();
      r.hostPlay = false; r.run(0.3);
      CHECK(stopped == 0 && playing >= 4 && r.ons().size() == playing && balanced(r.recs), "Transport mode: silent when stopped, plays with the host, stops cleanly"); }
    { Rig r; r.set(VK_P_GLOBAL_TEMPO, 120); r.set(SEQ, 3); r.set(SEQ + 1, 1); r.set(step(1, 0), 50); r.set(SEQ + 6, 0);
      r.run(0.3); CHECK(r.ons().empty(), "Key mode waits for a key");
      r.on(62); r.run(0.26);
      CHECK(fmt(r.notes(), 2) == "50 56 " && r.held() <= 1, "Key mode: restarts at step 1, transposed by D4 − C4 = +2 (%s)", fmt(r.notes(), 2).c_str());
      r.off(62); r.run(0.3); const size_t n = r.ons().size(); r.run(0.3);
      CHECK(r.ons().size() == n && balanced(r.recs), "stops when the key is released"); }
    { Rig r; r.set(VK_P_GLOBAL_TEMPO, 120);
      r.set(SEQ, 1); r.set(SEQ + 10, 0); r.set(SEQ + 1, 1); r.set(SEQ + 3, 2); r.set(SEQ + 6, 0); r.set(step(1, 0), 25);   // 1/4 steps: 0, +3 st
      r.set(ARP, 1); r.set(ARP + 4, 4); r.set(ARP + 23, 1); r.set(ARP + 16, 1);
      r.run(0.01); r.on(60); r.run(1.0);
      const auto ns = r.notes(VK_SOURCE_ARP);
      CHECK(ns.size() >= 8 && ns[0] == 60 && ns[3] == 60 && ns[4] == 63 && ns[7] == 63, "arp transposed by the sequencer each beat: %s", fmt(ns, 8).c_str()); }
}

static void testGenerativeTheory() {
    std::puts("Generative engine: theory, determinism, evolve");
    seq::GenSettings s; s.scale = 2; s.root = 9; s.bars = 4; s.density = 0.7f; s.complexity = 0.5f;
    bool inScale = true, startsOn1 = true, nonEmpty = true, inRange = true, sorted = true;
    const seq::Scale am = seq::makeScale(2);
    for (int style = 0; style < 5; ++style)
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            s.style = style; s.seed = seed;
            seq::Phrase p; seq::generatePhrase(s, 0, p);
            nonEmpty &= p.count > 0;
            startsOn1 &= p.count > 0 && p.notes[0].tick == 0;
            for (uint32_t i = 0; i < p.count; ++i) {
                const int pc = ((p.notes[i].note - s.root) % 12 + 12) % 12;
                inScale &= (am.mask >> pc) & 1;
                if (style == 0) inRange &= p.notes[i].note >= s.registerNote - 12 && p.notes[i].note <= s.registerNote + 12;
                if (i) sorted &= p.notes[i].tick >= p.notes[i - 1].tick;
            }
        }
    CHECK(inScale && nonEmpty && startsOn1 && sorted, "5 styles × 40 seeds: every note in A minor, phrases start on the one, sorted, never empty");
    CHECK(inRange, "melody stays within the register ± range");
    s.style = 0; s.seed = 5;
    seq::Phrase a, b, c; seq::generatePhrase(s, 0, a); seq::generatePhrase(s, 0, b); s.seed = 6; seq::generatePhrase(s, 0, c);
    CHECK(a.count == b.count && std::memcmp(a.notes, b.notes, sizeof(seq::GenNote) * a.count) == 0 &&
          (a.count != c.count || std::memcmp(a.notes, c.notes, sizeof(seq::GenNote) * a.count) != 0), "deterministic per seed; a new seed gives a new phrase");
    CHECK(a.chordDegree[0] == 0 && a.chordRoot[0] == 9 && (a.chordDegree[3] == 4 || a.chordDegree[3] == 3), "progression: tonic first, dominant turnaround in the last bar");
    bool tonicDominant = true; for (uint32_t sd = 1; sd < 60; ++sd) { s.seed = sd; seq::Phrase q; seq::generatePhrase(s, 0, q); tonicDominant &= q.chordDegree[0] == 0 && (q.chordDegree[3] == 4 || q.chordDegree[3] == 3); }
    CHECK(tonicDominant, "60 seeds: always I … V (or plagal IV) loops");
    // Chord style: notes on each downbeat form the bar's chord.
    s.style = 2; s.seed = 11; s.complexity = 0.2f; seq::Phrase ch; seq::generatePhrase(s, 0, ch);
    bool chordOk = true;
    for (int bar = 0; bar < 4; ++bar) {
        std::set<int> pcs; for (uint32_t i = 0; i < ch.count; ++i) if (ch.notes[i].tick == bar * ch.ticksPerBar) pcs.insert(ch.notes[i].note % 12);
        for (int k = 0; k < 3; ++k) chordOk &= pcs.count(((s.root + seq::degreePitch(0, am, ch.chordDegree[bar] + 2 * k)) % 12 + 12) % 12) == 1;
    }
    CHECK(chordOk, "chord style: each bar voices its triad");
    // Evolve.
    s.style = 0; s.seed = 3; s.evolve = 0.f; seq::Phrase r0, r1; seq::generatePhrase(s, 0, r0); seq::generatePhrase(s, 5, r1);
    const bool stable = r0.count == r1.count && std::memcmp(r0.notes, r1.notes, sizeof(seq::GenNote) * r0.count) == 0;
    s.evolve = 1.f; seq::Phrase e1; seq::generatePhrase(s, 1, e1);
    const bool changed = e1.count != r0.count || std::memcmp(e1.notes, r0.notes, sizeof(seq::GenNote) * r0.count) != 0;
    s.evolve = 0.25f; int mutated = 0; for (uint32_t rep = 1; rep <= 8; ++rep) for (int bar = 0; bar < 4; ++bar) mutated += seq::barVariant(s, bar, rep) == rep;
    CHECK(stable && changed && mutated > 2 && mutated < 20, "evolve 0 repeats exactly; 100 %% re-rolls; 25 %% re-rolls %d of 32 bar-repeats", mutated);
}

static void testGenerativeLive() {
    std::puts("Generative engine: live playback = offline capture, MIDI export");
    Rig r; r.hostTempo = 100; r.hostBeat = 0; r.hostPlay = true;
    r.set(GEN + 1, 0); r.set(GEN + 8, 1); r.set(GEN + 14, 60); r.set(GEN + 11, 0);   // melody, 2 bars, swing 60 %
    r.set(GEN, 1);
    r.run(8 * 60.0 / 100.0 * 1.02);
    std::vector<VKNoteRecord> off(2048); double beats = 0;
    const uint32_t n = vk_kernel_generative_phrase(reinterpret_cast<const VKKernel*>(r.k.get()), 0, off.data(), 2048, &beats);
    std::vector<VKNoteRecord> live; for (auto& x : r.recs) if (x.source == VK_SOURCE_GEN && x.beat < beats - 1e-9) live.push_back(x);
    std::vector<VKNoteRecord> offOn; for (uint32_t i = 0; i < n; ++i) if (off[i].velocity) offOn.push_back(off[i]);
    std::vector<VKNoteRecord> liveOn; for (auto& x : live) if (x.velocity) liveOn.push_back(x);
    bool match = offOn.size() == liveOn.size() && !offOn.empty();
    double maxErr = 0;
    for (size_t i = 0; match && i < offOn.size(); ++i) {
        match = offOn[i].note == liveOn[i].note && offOn[i].velocity == liveOn[i].velocity;
        maxErr = std::max(maxErr, std::fabs(offOn[i].beat - liveOn[i].beat));
    }
    CHECK(match && maxErr * SR * 0.6 < 0.5 && beats == 8.0, "phrase 0 plays exactly as captured offline (%zu notes, max %.4f samples)", offOn.size(), maxErr * SR * 0.6);
    // Second phrase follows on the bar grid.
    bool second = false; for (auto& x : r.recs) second |= x.source == VK_SOURCE_GEN && x.velocity && x.beat >= 8.0 - 1e-9 && x.beat < 8.001;
    CHECK(second, "phrase 1 starts on bar 3 (beat 8)");
    VKSequencerStatus st; vk_kernel_sequencer_status(reinterpret_cast<const VKKernel*>(r.k.get()), &st);
    CHECK((st.flags & VK_SEQ_FLAG_GEN_RUNNING) && (st.flags & VK_SEQ_FLAG_HOST_PLAYING) && st.genPhrase == 1 && st.genBar >= 0, "status: running, phrase %d, bar %d", st.genPhrase, st.genBar);
    char names[8][12]; const uint32_t bars = vk_kernel_generative_chords(reinterpret_cast<const VKKernel*>(r.k.get()), 0, names);
    CHECK(bars == 2 && std::strncmp(names[0], "Am", 2) == 0 && (names[1][0] == 'E' || names[1][0] == 'D'), "chord readout: %s → %s", names[0], names[1]);

    // MIDI file.
    const uint32_t size = vk_midi_file_write(off.data(), n, 100.0, nullptr, 0);
    std::vector<uint8_t> f(size);
    const uint32_t w = vk_midi_file_write(off.data(), n, 100.0, f.data(), size);
    bool header = w == size && std::memcmp(f.data(), "MThd", 4) == 0 && f[9] == 0 && f[11] == 1 && (f[12] << 8 | f[13]) == 480 && std::memcmp(f.data() + 14, "MTrk", 4) == 0;
    // Parse back: count note-ons and check tempo.
    size_t i = 22; int noteOns = 0, noteOffs = 0; uint32_t tempo = 0; uint64_t tick = 0, lastOnTick = 0; bool parsed = true, eot = false;
    while (i < f.size() && parsed && !eot) {
        uint32_t d = 0; do { d = (d << 7) | (f[i] & 0x7F); } while (f[i++] & 0x80);
        tick += d;
        const uint8_t st2 = f[i++];
        if (st2 == 0xFF) { const uint8_t type = f[i++]; const uint8_t len = f[i++]; if (type == 0x51) tempo = static_cast<uint32_t>(f[i] << 16 | f[i + 1] << 8 | f[i + 2]); if (type == 0x2F) eot = true; i += len; }
        else if ((st2 & 0xF0) == 0x90) { ++noteOns; lastOnTick = tick; i += 2; }
        else if ((st2 & 0xF0) == 0x80) { ++noteOffs; i += 2; }
        else parsed = false;
    }
    CHECK(header && parsed && eot && tempo == 600000 && noteOns == static_cast<int>(offOn.size()) && noteOffs == noteOns, "SMF: format 0, 480 PPQ, 100 BPM, %d note pairs, end of track", noteOns);
    CHECK(std::llabs(static_cast<long long>(lastOnTick) - std::llround(offOn.back().beat * 480)) <= 0, "SMF timing in ticks matches the phrase");
    VKNoteRecord orphan[3] = {{0.0, 60, 90, 0, 0}, {0.5, 61, 0, 0, 0}, {1.0, 62, 80, 0, 0}};
    std::vector<uint8_t> g(vk_midi_file_write(orphan, 3, 120, nullptr, 0));
    vk_midi_file_write(orphan, 3, 120, g.data(), static_cast<uint32_t>(g.size()));
    int on2 = 0, off2 = 0; for (size_t k = 22; k + 2 < g.size(); ++k) { if (g[k] == 0x90) ++on2; if (g[k] == 0x80) ++off2; }
    CHECK(on2 == 2 && off2 == 2, "orphan note-offs dropped, open notes closed at the end");
}

static void testGenerativeFollow() {
    std::puts("Generative engine: follow keys, hold to play");
    Rig r; r.set(VK_P_GLOBAL_TEMPO, 120); r.set(GEN + 1, 4); r.set(GEN + 3, 0); r.set(GEN + 2, 1); r.set(GEN + 13, 2);   // broken chords, C major, hold
    r.set(GEN, 1); r.run(0.5);
    CHECK(r.ons().empty(), "hold to play: silent until a key is held");
    r.on(62); r.run(1.0);
    const auto ns = r.notes(VK_SOURCE_GEN);
    bool dMajor = !ns.empty(); const seq::Scale maj = seq::makeScale(1);
    for (int n : ns) dMajor &= (maj.mask >> (((n - 2) % 12 + 12) % 12)) & 1;
    CHECK(dMajor && r.held() <= 2, "holding D transposes the C major phrase to D major (%zu notes)", ns.size());
    r.off(62); r.run(0.5); const size_t n = r.ons().size(); r.run(0.5);
    CHECK(r.ons().size() == n && balanced(r.recs), "releasing the key stops it cleanly");
}

static void testCaptureAndStatus() {
    std::puts("Capture of played keys, status, panic");
    Rig r; r.run(0.01);
    r.on(60, 90, 10); r.run(0.1); r.off(60, 20); r.run(0.05);
    CHECK(r.recs.size() == 2 && r.recs[0].source == VK_SOURCE_KEYS && r.recs[0].velocity == 90 && r.recs[1].velocity == 0, "keys played directly are captured too");
    r.k->postCommand({VK_CMD_CAPTURE, 0, 0.f}); r.on(61); r.run(0.05); r.off(61); r.run(0.05);
    CHECK(r.recs.size() == 2, "capture disarmed: nothing recorded");
    r.set(SEQ, 1); r.set(ARP, 1); r.on(60); r.on(64); r.run(0.3);
    VKSequencerStatus st; vk_kernel_sequencer_status(reinterpret_cast<const VKKernel*>(r.k.get()), &st);
    CHECK((st.flags & VK_SEQ_FLAG_SEQ_RUNNING) && (st.flags & VK_SEQ_FLAG_ARP_RUNNING) && st.seqStep >= 0 && st.arpNote >= 60, "status: sequencer on step %d, arp note %d", st.seqStep, st.arpNote);
    r.k->postCommand({VK_CMD_PANIC, 0, 0.f}); r.run(0.05);
    CHECK(r.held() == 0 && r.k->arpeggiator().keyCount() == 0, "panic clears the arp's keys and silences the generators' notes");
}

static void testStuckNoteFuzz() {
    std::puts("Stuck-note fuzz");
    Rig r; r.hostTempo = 140; r.hostPlay = true;
    seq::Rand rnd(42);
    for (int b = 0; b < 3000; ++b) {
        if (rnd.chance(0.08)) r.on(36 + rnd.below(48), 1 + rnd.below(127), static_cast<uint32_t>(rnd.below(BLK)));
        if (rnd.chance(0.08)) r.off(36 + rnd.below(48), static_cast<uint32_t>(rnd.below(BLK)));
        if (rnd.chance(0.01)) r.set(ARP, static_cast<float>(rnd.below(3)));
        if (rnd.chance(0.01)) r.set(SEQ, static_cast<float>(rnd.below(4)));
        if (rnd.chance(0.01)) r.set(GEN, static_cast<float>(rnd.below(2)));
        if (rnd.chance(0.01)) r.set(GEN + 13, static_cast<float>(rnd.below(3)));
        if (rnd.chance(0.02)) r.set(ARP + 4, static_cast<float>(rnd.below(15)));
        if (rnd.chance(0.02)) r.set(SEQ + 3, static_cast<float>(rnd.below(15)));
        if (rnd.chance(0.02)) r.set(ARP + 5, 5.f + static_cast<float>(rnd.below(196)));
        if (rnd.chance(0.02)) r.set(ARP + 1, static_cast<float>(rnd.below(14)));
        if (rnd.chance(0.02)) r.set(SEQ + 2, static_cast<float>(rnd.below(5)));
        if (rnd.chance(0.02)) r.set(step(rnd.below(16), 3), static_cast<float>(rnd.below(4)));
        if (rnd.chance(0.01)) r.set(VK_P_GLOBAL_VOICE_MODE, static_cast<float>(rnd.below(5)));
        if (rnd.chance(0.005)) r.hostBeat = rnd.uniform() * 64;
        if (rnd.chance(0.005)) r.hostPlay = !r.hostPlay;
        if (rnd.chance(0.01)) r.midi(0xB0, 64, rnd.chance(0.5) ? 127 : 0);
        r.block();
    }
    // Everything off, keys up, pedal up.
    r.midi(0xB0, 64, 0); for (int n = 0; n < 128; ++n) r.off(n);
    r.set(ARP, 0); r.set(SEQ, 0); r.set(GEN, 0); r.set(VK_P_GLOBAL_VOICE_MODE, 0);
    r.run(0.1);
    CHECK(r.held() == 0, "after 3000 blocks of random keys, modes, rates, locates and pedal: no voice left held");
    if (!balanced(r.recs)) {
        std::map<std::pair<int, int>, int> open;
        for (auto& x : r.recs) { auto key = std::make_pair(int(x.source), int(x.note)); if (x.velocity) ++open[key]; else if (open[key] > 0) --open[key]; }
        for (auto& [kk, c] : open) if (c) std::printf("    open: source %d note %d ×%d\n", kk.first, kk.second, c);
    }
    CHECK(balanced(r.recs), "every captured note-on has its note-off (%zu records)", r.recs.size());
}

static void testRealtime() {
    std::puts("Real-time safety and load");
    Rig r; r.hostTempo = 126; r.hostPlay = true;
    r.set(ARP, 1); r.set(ARP + 12, 2); r.set(ARP + 7, 1); r.set(ARP + 11, 80);
    r.set(SEQ, 1); r.set(SEQ + 11, 1); r.set(MATRIX + 0, 1); r.set(MATRIX + 1, MS_SEQ_C); r.set(MATRIX + 3, VK_P_VCFB_CUTOFF); r.set(MATRIX + 4, 30);
    r.set(GEN, 1); r.set(GEN + 1, 2); r.set(GEN + 11, 60);
    r.run(0.2); r.on(48); r.on(55); r.on(60);
    gAllocs = 0; gTrack = true;
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    for (int b = 0; b < 2000; ++b) {
        if (b % 200 == 0) r.hostBeat = b * 0.1;           // relocations, incl. phrase regeneration
        if (b % 50 == 0) { r.off(48); r.on(48 + b % 12); }
        r.block();
    }
    const double secs = std::chrono::duration<double>(clk::now() - t0).count();
    gTrack = false;
    CHECK(gAllocs == 0, "zero allocations: arp (Euclid, ratchets), sequencer, generative (regenerating), capture, relocations");
    const double load = secs / (2000.0 * BLK / SR) * 100.0;
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    const double budget = 400.0;   // instrumented build: only a sanity bound
#else
    const double budget = 25.0;
#endif
    CHECK(load < budget, "all three generators + 8 voices: %.2f %% of one core (host build)", load);

    // TSan: UI thread drains capture and status while audio renders.
    Rig t; t.set(ARP, 1); t.set(SEQ, 1); t.set(GEN, 1); t.on(60); t.on(63);
    std::atomic<bool> stop{false}; std::atomic<long> got{0};
    std::thread ui([&] { VKSequencerStatus st; VKNoteRecord rec; while (!stop) { vk_kernel_sequencer_status(reinterpret_cast<const VKKernel*>(t.k.get()), &st); while (t.k->popNoteRecord(rec)) ++got; } });
    float* ch[2] = {t.L.data(), t.R.data()};
    for (int b = 0; b < 1500; ++b) t.k->render(ch, 2, BLK, nullptr, 0);
    stop = true; ui.join();
    CHECK(got > 50, "UI thread drained %ld records concurrently", got.load());
}

static void testPresetCreator() {
    std::puts("Generative preset creator");
    std::vector<VKParamValue> v(1024);
    const uint32_t n = vk_preset_generate(1, VK_PRESET_BASS, v.data(), 1024, nullptr);
    bool owns = n > 300;
    for (uint32_t i = 0; i < n; ++i) { const uint64_t m = v[i].address / 256; owns &= m != 36 && m != 37 && m != 38 && v[i].address != VK_P_GLOBAL_TEMPO && v[i].address != VK_P_GLOBAL_POLYPHONY; }
    CHECK(owns, "%u synthesis parameters; performance and sequencing settings untouched", n);
    std::vector<VKParamValue> v2(1024); vk_preset_generate(1, VK_PRESET_BASS, v2.data(), 1024, nullptr);
    bool same = true; for (uint32_t i = 0; i < n; ++i) same &= v[i].address == v2[i].address && v[i].value == v2[i].value;
    std::vector<VKParamValue> v3(1024); vk_preset_generate(2, VK_PRESET_BASS, v3.data(), 1024, nullptr);
    bool differ = false; for (uint32_t i = 0; i < n; ++i) differ |= v[i].value != v3[i].value;
    CHECK(same && differ, "deterministic per seed; seeds differ");
    bool inRange = true; for (uint32_t i = 0; i < n; ++i) for (auto& d : kParamTable) if (d.address == v[i].address) inRange &= v[i].value >= d.minValue && v[i].value <= d.maxValue;
    CHECK(inRange, "every value inside its parameter's range");

    // Never silent: 8 categories × 12 seeds, re-auditioned independently across the keyboard.
    int silent = 0, clipping = 0, repaired = 0, total = 0; float minDb = 0, maxPeak = 0; uint32_t maxAttempts = 0;
    std::set<int> modes;
    for (uint32_t cat = 0; cat < VK_PRESET_CATEGORY_COUNT; ++cat)
        for (uint32_t seed = 100; seed < 112; ++seed) {
            VKPresetReport rep{};
            const uint32_t m = vk_preset_generate(seed, cat, v.data(), 1024, &rep);
            ++total; repaired += rep.repaired; maxAttempts = std::max(maxAttempts, rep.attempts);
            Rig r(false);
            for (uint32_t i = 0; i < m; ++i) r.set(v[i].address, v[i].value);
            modes.insert(static_cast<int>(r.k->getParam(VK_P_GLOBAL_VOICE_MODE)));
            double worst = 0; float peak = 0;
            for (int note : {36, 60, 84}) {
                r.on(note, 100);
                double best = 0;
                for (int b = 0; b < 188; ++b) {   // 1 s hold, loudest 50 ms window (≈ 9 blocks)
                    r.block();
                    static double win = 0; static int nb = 0;
                    for (float x : r.L) { win += static_cast<double>(x) * x; peak = std::max(peak, std::fabs(x)); }
                    if (++nb == 9) { best = std::max(best, win / (9.0 * BLK)); win = 0; nb = 0; }
                }
                const double db = 10 * std::log10(best + 1e-30);
                worst = note == 36 ? db : std::min(worst, db);
                r.off(note); r.run(1.0);
            }
            if (worst < -50) ++silent;
            if (peak >= 1.0f) ++clipping;
            minDb = std::min(minDb, static_cast<float>(worst)); maxPeak = std::max(maxPeak, peak);
        }
    CHECK(silent == 0, "%d sounds, none silent on C2, C4 or C6 (quietest loudest-50 ms window %.1f dBFS)", total, minDb);
    CHECK(clipping == 0, "none clipping (loudest peak %.2f)", maxPeak);
    CHECK(repaired == 0 && maxAttempts <= 8, "no fallback needed (worst case %u audition attempts)", maxAttempts);
    CHECK(modes.size() >= 2, "categories use different voice modes (%zu)", modes.size());
}

static void testVoiceModeSameBuffer() {
    std::puts("Regression: voice-mode change and notes in the same buffer");
    Rig r(false); r.run(0.02);
    r.set(VK_P_GLOBAL_VOICE_MODE, 2); r.on(48); r.on(60); r.run(0.2);
    CHECK(r.held() == 1, "legato set just before the notes: the note still sounds (held voices %u)", r.held());
}

int main() {
    testTables();
    testArpPatterns();
    testArpTiming();
    testArpHostSync();
    testArpFeatures();
    testStepSequencer();
    testSequencerRunModes();
    testGenerativeTheory();
    testGenerativeLive();
    testGenerativeFollow();
    testCaptureAndStatus();
    testStuckNoteFuzz();
    testRealtime();
    testVoiceModeSameBuffer();
    testPresetCreator();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
