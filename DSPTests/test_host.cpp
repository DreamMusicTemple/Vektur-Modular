// test_host.cpp — Phase 7 host simulation (the automated half of
// docs/DAW_TEST_MATRIX.md): load/unload ×50 at random sample rates and
// buffer sizes, odd/oversized buffers, sample-accurate automation and ramps,
// buffer-size invariance of generated-note timing, sample-rate changes,
// host time signatures (3/4, 6/8, pick-up bars), parameter state
// round-trips through the C API and allocation-free rendering throughout.
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

using namespace vektur;

static uint64_t gSeed = 12345;
static uint32_t rnd(uint32_t n) { gSeed = gSeed * 6364136223846793005ull + 1442695040888963407ull; return static_cast<uint32_t>((gSeed >> 33) % n); }

static VKEvent midiEv(uint8_t a, uint8_t b, uint8_t c, uint32_t off) {
    VKEvent e{}; e.type = VK_EVENT_MIDI1; e.length = 3; e.data[0] = a; e.data[1] = b; e.data[2] = c; e.sampleOffset = off; return e;
}
static VKEvent paramEv(uint64_t a, float v, uint32_t off, uint32_t ramp = 0) {
    VKEvent e{}; e.type = ramp ? VK_EVENT_PARAM_RAMP : VK_EVENT_PARAM; e.address = a; e.value = v; e.rampFrames = ramp; e.sampleOffset = off; return e;
}

// ---------------------------------------------------------------------------
static void testLifecycle() {
    std::printf("Load / unload ×50 at random sample rates and buffer sizes\n");
    const double rates[] = {22050, 44100, 48000, 88200, 96000, 176400, 192000};
    const uint32_t sizes[] = {16, 64, 128, 256, 441, 512, 1024, 2048, 4096};
    bool allOk = true, finite = true, silentWhenOff = true, oversizeSilent = true;
    long allocs = 0;
    std::vector<float> L(8192), R(8192);
    float* ch[2] = {L.data(), R.data()};
    for (int cycle = 0; cycle < 50; ++cycle) {
        VKKernel* k = vk_kernel_create();
        const double sr = rates[rnd(7)];
        uint32_t maxF = sizes[rnd(9)];
        // A host may render once before allocating (Logic validation does): silence, no crash.
        std::fill(L.begin(), L.end(), 1.f);
        vk_kernel_render(k, ch, 2, 64, nullptr, 0);
        silentWhenOff = silentWhenOff && L[0] == 0.f && L[63] == 0.f;
        allOk = allOk && vk_kernel_allocate(k, sr, 2, maxF) == 0 && vk_kernel_is_allocated(k);
        vk_kernel_set_param(k, VK_P_ARP_MODE, static_cast<float>(cycle % 2));
        vk_kernel_set_param(k, VK_P_GLOBAL_MIDI_CLOCK_OUT, 1);
        for (int rep = 0; rep < 3; ++rep) {   // re-allocate at new formats, as hosts do on SR/buffer changes
            gTrack = true;
            for (int b = 0; b < 60; ++b) {
                const uint32_t f = 1 + rnd(maxF);
                VKEvent ev[4]; uint32_t n = 0;
                if (b % 7 == 0) ev[n++] = midiEv(0x90, static_cast<uint8_t>(48 + rnd(24)), 100, rnd(f));
                if (b % 11 == 0) ev[n++] = midiEv(0xB0, 123, 0, f - 1);
                std::sort(ev, ev + n, [](const VKEvent& x, const VKEvent& y) { return x.sampleOffset < y.sampleOffset; });
                vk_kernel_set_transport(k, 90 + rnd(60), b * 0.1, b % 3 != 0);
                vk_kernel_render(k, ch, 2, f, ev, n);
                for (uint32_t i = 0; i < f; ++i) finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]);
            }
            std::fill(L.begin(), L.end(), 1.f);
            vk_kernel_render(k, ch, 2, maxF + 1, nullptr, 0);   // larger than promised: silence, no overrun
            oversizeSilent = oversizeSilent && L[0] == 0.f && L[maxF] == 0.f;
            gTrack = false;
            allocs += gAllocs.exchange(0);
            vk_kernel_deallocate(k);
            allOk = allOk && !vk_kernel_is_allocated(k);
            maxF = sizes[rnd(9)];
            allOk = allOk && vk_kernel_allocate(k, rates[rnd(7)], 2, maxF) == 0;
        }
        vk_kernel_destroy(k);
    }
    CHECK(allOk, "allocate / deallocate / reallocate succeed every cycle");
    CHECK(finite, "output finite at every rate and buffer size");
    CHECK(silentWhenOff, "render before allocate writes silence");
    CHECK(oversizeSilent, "a buffer larger than maxFrames renders silence");
    CHECK(allocs == 0, "no allocation in any render call (%ld)", allocs);
    VKKernel* k = vk_kernel_create();
    CHECK(vk_kernel_allocate(k, 0, 2, 512) != 0 && vk_kernel_allocate(k, 48000, 2, 0) != 0 && vk_kernel_allocate(k, 48000, 2, 100000) != 0,
          "invalid formats are refused");
    vk_kernel_destroy(k);
}

// Renders `total` frames in buffers of `blk`, notes at fixed absolute times; returns output.
struct Run { std::vector<float> out; std::vector<uint64_t> noteOns; };
static Run render(double sr, uint32_t blk, uint64_t total, bool arp, std::vector<std::pair<uint64_t, VKEvent>> abs = {}) {
    auto k = std::make_unique<Kernel>();
    k->allocate(sr, 2, 4096);
    k->setParam(VK_P_GLOBAL_INSTABILITY, 0);
    k->setParam(VK_P_GLOBAL_TEMPO, 120);
    k->setParam(VK_P_GLOBAL_MIDI_OUT, 1);
    if (arp) { k->setParam(VK_P_ARP_MODE, 1); k->setParam(VK_P_ARP_RATE, 4); }
    abs.insert(abs.begin(), {std::make_pair(uint64_t{0}, midiEv(0x90, 60, 100, 0)), std::make_pair(uint64_t{0}, midiEv(0x90, 67, 100, 0))});
    std::stable_sort(abs.begin(), abs.end(), [](auto& a, auto& b) { return a.first < b.first; });
    Run r; r.out.reserve(total);
    std::vector<float> L(blk), R(blk); float* ch[2] = {L.data(), R.data()};
    std::vector<VKEvent> ev;
    size_t next = 0;
    for (uint64_t t = 0; t < total; t += blk) {
        const uint32_t f = static_cast<uint32_t>(std::min<uint64_t>(blk, total - t));
        ev.clear();
        while (next < abs.size() && abs[next].first < t + f) { VKEvent e = abs[next].second; e.sampleOffset = static_cast<uint32_t>(abs[next].first - t); ev.push_back(e); ++next; }
        k->render(ch, 2, f, ev.empty() ? nullptr : ev.data(), static_cast<uint32_t>(ev.size()));
        r.out.insert(r.out.end(), L.begin(), L.begin() + f);
        const VKMidiOutEvent* o = nullptr; const uint32_t n = k->midiOutput(&o);
        for (uint32_t i = 0; i < n; ++i) if ((o[i].data[0] & 0xF0) == 0x90) r.noteOns.push_back(t + o[i].sampleOffset);
    }
    return r;
}

static void testAutomation() {
    std::printf("Sample-accurate automation\n");
    const uint64_t at = 10000 + 77;
    Run a = render(48000, 256, 24000, false);
    Run b = render(48000, 256, 24000, false, {{at, paramEv(VK_P_GLOBAL_MASTER_VOLUME, -40.f, 0)}});
    uint64_t first = 0;
    for (uint64_t i = 0; i < a.out.size(); ++i) if (a.out[i] != b.out[i]) { first = i; break; }
    CHECK(first >= at && first <= at + 1, "a parameter event mid-buffer takes effect at its sample (%llu vs %llu)", (unsigned long long)first, (unsigned long long)at);
    double ra = 0, rb = 0; for (uint64_t i = 20000; i < 24000; ++i) { ra += a.out[i] * a.out[i]; rb += b.out[i] * b.out[i]; }
    const double dB = 10 * std::log10(rb / ra);
    CHECK(std::fabs(dB - (-34.0)) < 1.0, "−6 → −40 dB master volume: %.2f dB", dB);

    auto k = std::make_unique<Kernel>(); k->allocate(48000, 2, 4096);
    std::vector<float> L(512), R(512); float* ch[2] = {L.data(), R.data()};
    VKEvent e = paramEv(VK_P_VCFA_RESONANCE, 80.f, 32, 400);
    k->render(ch, 2, 256, &e, 1);
    const float mid = k->params().modulated(VK_P_VCFA_RESONANCE, nullptr);
    k->render(ch, 2, 256, nullptr, 0);
    const float end = k->params().modulated(VK_P_VCFA_RESONANCE, nullptr);
    CHECK(mid > 30.f && mid < 60.f && std::fabs(end - 80.f) < 1e-3f, "parameter ramp over 400 frames: %.1f half-way, %.3f at the end", mid, end);
}

static void testBufferInvariance() {
    std::printf("Buffer-size and sample-rate behaviour\n");
    const Run r256 = render(48000, 256, 96000, true);
    bool same = r256.noteOns.size() >= 10;
    for (uint32_t blk : {17u, 64u, 441u, 1024u, 4096u}) {
        const Run r = render(48000, blk, 96000, true);
        same = same && r.noteOns == r256.noteOns;
    }
    CHECK(same, "arp note times identical for buffers of 17…4096 frames (%zu notes)", r256.noteOns.size());
    bool spaced = true; for (size_t i = 1; i < r256.noteOns.size(); ++i) spaced = spaced && r256.noteOns[i] - r256.noteOns[i - 1] == 6000;
    CHECK(spaced, "1/16 at 120 BPM: 6000 samples at 48 kHz");
    const Run r44 = render(44100, 512, 88200, true);
    bool sp44 = r44.noteOns.size() >= 10;
    for (size_t i = 0; i < r44.noteOns.size(); ++i) sp44 = sp44 && std::llabs(static_cast<long long>(r44.noteOns[i]) - std::llround(i * 5512.5)) <= 1;
    CHECK(sp44, "44.1 kHz: notes on the 5512.5-sample grid (±1)");
    const Run r96 = render(96000, 512, 192000, true);
    bool sp96 = r96.noteOns.size() >= 10;
    for (size_t i = 1; i < r96.noteOns.size(); ++i) sp96 = sp96 && r96.noteOns[i] - r96.noteOns[i - 1] == 12000;
    CHECK(sp96, "96 kHz: 12000 samples");
    double e1 = 0, e2 = 0; const Run a = render(48000, 64, 48000, false), b = render(48000, 1024, 48000, false);
    for (size_t i = 0; i < a.out.size(); ++i) { e1 += a.out[i] * a.out[i]; e2 += b.out[i] * b.out[i]; }
    CHECK(std::fabs(10 * std::log10(e1 / e2)) < 0.1, "level independent of buffer size (%.3f dB)", 10 * std::log10(e1 / e2));
}

// Host time signature: generative phrases and stutter grids follow the meter.
static void testMeter() {
    std::printf("Host time signature\n");
    auto phrase = [](double num, double den, double bars) {
        auto k = std::make_unique<Kernel>(); k->allocate(48000, 2, 512);
        k->setParam(VK_P_GEN_PHRASE_BARS, static_cast<float>(bars));
        std::vector<float> L(256), R(256); float* ch[2] = {L.data(), R.data()};
        k->setTransportEx(120, 0, true, 0, num, den);
        k->render(ch, 2, 256, nullptr, 0);
        double beats = 0;
        vk_kernel_generative_phrase(reinterpret_cast<VKKernel*>(k.get()), 0, nullptr, 0, &beats);
        return std::make_pair(beats, k->beatsPerBar());
    };
    const auto p44 = phrase(4, 4, 1), p34 = phrase(3, 4, 1), p68 = phrase(6, 8, 1), p78 = phrase(7, 8, 1);
    CHECK(p34.second == 3.0 && p68.second == 3.0 && p78.second == 3.5, "beats per bar: 3/4 = 3, 6/8 = 3, 7/8 = 3.5");
    CHECK(p34.first == 3.0 * p44.first / 4.0 && p78.first == 3.5 * p44.first / 4.0, "phrase length scales with the meter (%.1f vs %.1f beats)", p34.first, p44.first);

    // Playing generative in 3/4 with a pick-up: bars start at 0.5, 3.5, 6.5 …
    auto barAt = [](double num, double barStart, double untilBeat) {
        auto k = std::make_unique<Kernel>(); k->allocate(48000, 2, 512);
        k->setParam(VK_P_GEN_MODE, 1); k->setParam(VK_P_GEN_PHRASE_BARS, 2);
        std::vector<float> L(256), R(256); float* ch[2] = {L.data(), R.data()};
        double beat = 0;
        while (beat < untilBeat) {
            k->setTransportEx(120, beat, true, barStart + std::floor((beat - barStart) / num) * num, num, 4);
            k->render(ch, 2, 256, nullptr, 0);
            beat += 256.0 / 48000.0 * 2.0;
        }
        VKSequencerStatus s{}; k->sequencerStatus(s);
        return s.genBar;
    };
    CHECK(barAt(4, 0, 3.6) == 0 && barAt(3, 0, 3.6) == 1, "3/4: beat 3.6 is in bar 2 (4/4: still bar 1)");
    CHECK(barAt(3, 0.5, 3.4) == 0 && barAt(3, 0.5, 3.7) == 1, "pick-up bar: downbeat at 3.5 follows the host's bar position");

    auto k = std::make_unique<Kernel>(); k->allocate(48000, 2, 512);
    std::vector<float> L(256), R(256); float* ch[2] = {L.data(), R.data()};
    k->setTransportEx(120, 0, true, 0, 5, 4); k->render(ch, 2, 256, nullptr, 0);
    CHECK(k->beatsPerBar() == 5.0, "5/4 → 5 beats per bar");
    k->setTransport(120, 1, true); k->render(ch, 2, 256, nullptr, 0);
    CHECK(k->beatsPerBar() == 4.0, "host without a meter → 4/4");
    k->setTransportEx(120, 0, true, 0, 0, 0); k->render(ch, 2, 256, nullptr, 0);
    CHECK(k->beatsPerBar() == 4.0, "invalid meter ignored");
}

static void testState() {
    std::printf("Parameter state round trip\n");
    VKKernel* a = vk_kernel_create(); VKKernel* b = vk_kernel_create();
    vk_kernel_allocate(a, 48000, 2, 512); vk_kernel_allocate(b, 44100, 2, 1024);
    std::vector<float> want;
    for (const auto& p : kParamTable) {
        const float v = p.minValue + (p.maxValue - p.minValue) * static_cast<float>(rnd(10001)) / 10000.f;
        vk_kernel_set_param(a, p.address, v);
        want.push_back(v);
    }
    bool ok = true; size_t i = 0;
    for (const auto& p : kParamTable) { vk_kernel_set_param(b, p.address, vk_kernel_get_param(a, p.address)); ok = ok && vk_kernel_get_param(b, p.address) == want[i++]; }
    CHECK(ok, "all %zu parameters survive a save/restore into another instance", sizeof(kParamTable) / sizeof(kParamTable[0]));
    bool clamped = true;
    for (const auto& p : kParamTable) { vk_kernel_set_param(b, p.address, p.maxValue + 1000.f); clamped = clamped && vk_kernel_get_param(b, p.address) == p.maxValue; }
    CHECK(clamped, "out-of-range restored values clamp to the range");
    vk_kernel_set_param(b, VK_P_GLOBAL_MASTER_VOLUME, NAN);
    CHECK(std::isfinite(vk_kernel_get_param(b, VK_P_GLOBAL_MASTER_VOLUME)), "a NaN in a restored state is rejected");
    vk_kernel_destroy(a); vk_kernel_destroy(b);
}

int main() {
    testLifecycle();
    testAutomation();
    testBufferInvariance();
    testMeter();
    testState();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
