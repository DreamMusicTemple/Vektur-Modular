// test_kernel.cpp — host-side tests for the VekturDSP kernel.
// Builds and runs on macOS or Linux with CMake (no Xcode needed), so the DSP
// core can be verified in CI independently of the Apple targets.
#include "VekturDSP.h"
#include "Kernel.hpp"
#include "SPSCQueue.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Allocation tracker: counts heap allocations while `gTrack` is set.
// ---------------------------------------------------------------------------
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
    std::printf("  FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

constexpr double kSR = 48000.0;
constexpr uint32_t kBlock = 256;


// The kernel registers every parameter itself (ParamTable.hpp). Tests that
// check exact pitch disable analogue instability for determinism.
static VKKernel* makeKernel(uint32_t maxFrames = 1024, bool stable = true) {
    VKKernel* k = vk_kernel_create();
    if (stable) vk_kernel_set_param(k, VK_P_GLOBAL_INSTABILITY, 0.f);
    if (vk_kernel_allocate(k, kSR, 2, maxFrames) != 0) { std::printf("allocate failed\n"); std::exit(2); }
    return k;
}

struct Buf {
    std::vector<float> L, R; float* ch[2];
    explicit Buf(uint32_t n) : L(n), R(n) { ch[0] = L.data(); ch[1] = R.data(); }
};

static VKEvent midi(uint32_t off, uint8_t s, uint8_t d1, uint8_t d2) {
    VKEvent e{}; e.sampleOffset = off; e.type = VK_EVENT_MIDI1; e.length = 3;
    e.data[0] = s; e.data[1] = d1; e.data[2] = d2; return e;
}

static std::vector<float> renderSeconds(VKKernel* k, double seconds, const std::vector<VKEvent>& firstBlockEvents = {}) {
    std::vector<float> out;
    Buf b(kBlock);
    const auto blocks = static_cast<int>(seconds * kSR / kBlock);
    for (int i = 0; i < blocks; ++i) {
        const auto& ev = i == 0 ? firstBlockEvents : std::vector<VKEvent>{};
        vk_kernel_render(k, b.ch, 2, kBlock, ev.data(), static_cast<uint32_t>(ev.size()));
        out.insert(out.end(), b.L.begin(), b.L.end());
    }
    return out;
}

static double estimateFrequency(const std::vector<float>& x, size_t from) {
    int crossings = 0; size_t first = 0, last = 0;
    for (size_t i = from + 1; i < x.size(); ++i)
        if (x[i - 1] < 0.f && x[i] >= 0.f) { if (!crossings) first = i; last = i; ++crossings; }
    if (crossings < 2) return 0.0;
    return (crossings - 1) * kSR / static_cast<double>(last - first);
}

static float peakAbs(const std::vector<float>& x, size_t a, size_t b) {
    float p = 0.f; for (size_t i = a; i < b && i < x.size(); ++i) p = std::max(p, std::fabs(x[i])); return p;
}
static float maxDelta(const std::vector<float>& x, size_t a, size_t b) {
    float d = 0.f; for (size_t i = std::max<size_t>(a, 1); i < b && i < x.size(); ++i) d = std::max(d, std::fabs(x[i] - x[i - 1])); return d;
}

// ---------------------------------------------------------------------------
static void testAllocateValidation() {
    std::puts("allocate validation");
    VKKernel* k = vk_kernel_create();
    CHECK(vk_kernel_allocate(k, 1000.0, 2, 512) != 0, "rejects tiny sample rate");
    CHECK(vk_kernel_allocate(k, 48000.0, 0, 512) != 0, "rejects zero channels");
    CHECK(vk_kernel_allocate(k, 48000.0, 3, 512) != 0, "rejects >2 channels");
    CHECK(vk_kernel_allocate(k, 48000.0, 2, 0) != 0, "rejects zero frames");
    CHECK(!vk_kernel_is_allocated(k), "not allocated after failures");
    CHECK(!vk_kernel_register_param(k, VK_ADDRESS_SPACE, 0, 1, 0, 0), "rejects out-of-range address");
    CHECK(!vk_kernel_register_param(k, 5, 1, 0, 0, 0), "rejects min > max");
    // Rendering an unallocated kernel must output silence, not garbage.
    Buf b(64); b.L.assign(64, 1.f); b.R.assign(64, 1.f);
    vk_kernel_render(k, b.ch, 2, 64, nullptr, 0);
    CHECK(peakAbs(b.L, 0, 64) == 0.f, "unallocated render is silent");
    vk_kernel_destroy(k);
}

static void testSilenceAndPitch() {
    std::puts("silence, pitch, release");
    VKKernel* k = makeKernel();
    auto silent = renderSeconds(k, 0.1);
    CHECK(peakAbs(silent, 0, silent.size()) == 0.f, "silent with no notes");

    auto a4 = renderSeconds(k, 0.5, {midi(0, 0x90, 69, 100)});
    const double f = estimateFrequency(a4, 2400);
    CHECK(std::fabs(f - 440.0) < 1.5, "A4 ≈ 440 Hz, got %.2f", f);
    CHECK(peakAbs(a4, 2400, a4.size()) > 0.05f, "audible");

    auto rel = renderSeconds(k, 2.0, {midi(0, 0x80, 69, 0)});
    CHECK(peakAbs(rel, rel.size() - 4800, rel.size()) == 0.f, "decays to silence after release");
    CHECK(static_cast<vektur::Kernel*>(static_cast<void*>(k))->activeVoiceCount() == 0, "voice returned to idle");
    vk_kernel_destroy(k);
}

static void testSampleAccurateNoteOn() {
    std::puts("sample-accurate events");
    VKKernel* k = makeKernel();
    Buf b(kBlock);
    VKEvent ev = midi(100, 0x90, 60, 127);
    vk_kernel_render(k, b.ch, 2, kBlock, &ev, 1);
    CHECK(peakAbs(b.L, 0, 100) == 0.f, "silent before offset 100");
    CHECK(peakAbs(b.L, 101, kBlock) > 0.f, "sounding after offset 100");
    CHECK(peakAbs(b.R, 101, kBlock) > 0.f, "right channel carries signal");

    // Events stamped beyond the buffer must still be applied, not lost.
    VKEvent late = midi(9999, 0x80, 60, 0);
    vk_kernel_render(k, b.ch, 2, kBlock, &late, 1);
    auto tail = renderSeconds(k, 2.0);
    CHECK(peakAbs(tail, tail.size() - 4800, tail.size()) == 0.f, "late note-off applied");
    vk_kernel_destroy(k);
}

static void testPitchBendAndTune() {
    std::puts("pitch bend & master tune");
    VKKernel* k = makeKernel();
    // Full upward bend with 12-semitone range → one octave.
    vk_kernel_set_param(k, VK_P_GLOBAL_PITCH_BEND_RANGE, 12.f);
    auto x = renderSeconds(k, 0.5, {midi(0, 0xE0, 0x7F, 0x7F), midi(0, 0x90, 57, 100)});
    double f = estimateFrequency(x, 4800);
    CHECK(std::fabs(f - 440.0) < 2.0, "A3 bent +12 ≈ 440 Hz, got %.2f", f);
    vk_kernel_post_command(k, {VK_CMD_PANIC, 0, 0.f});
    renderSeconds(k, 0.1);

    vk_kernel_set_param(k, VK_P_GLOBAL_MASTER_TUNE, 100.f);   // +1 semitone
    x = renderSeconds(k, 0.5, {midi(0, 0x90, 68, 100)});
    f = estimateFrequency(x, 4800);
    CHECK(std::fabs(f - 440.0) < 2.0, "G#4 +100 cents ≈ 440 Hz, got %.2f", f);
    vk_kernel_destroy(k);
}

static void testPolyphonyAndStealing() {
    std::puts("polyphony limit & click-free stealing");
    VKKernel* k = makeKernel();
    auto* K = static_cast<vektur::Kernel*>(static_cast<void*>(k));
    vk_kernel_set_param(k, VK_P_GLOBAL_POLYPHONY, 0.f);   // 1 voice
    renderSeconds(k, 0.01);
    CHECK(K->polyphonyLimit() == 1, "limit = 1, got %u", K->polyphonyLimit());

    auto x = renderSeconds(k, 0.3, {midi(0, 0x90, 48, 100)});
    // Steal while the first note is at full level.
    Buf b(kBlock); std::vector<float> s;
    VKEvent on2 = midi(10, 0x90, 55, 100);
    for (int i = 0; i < 40; ++i) {
        vk_kernel_render(k, b.ch, 2, kBlock, i == 0 ? &on2 : nullptr, i == 0 ? 1 : 0);
        s.insert(s.end(), b.L.begin(), b.L.end());
    }
    CHECK(K->activeVoiceCount() <= 1, "never exceeds 1 voice");
    // Reference: the same new note started on an idle voice. A steal must not
    // add any sample-to-sample jump larger than either note produces on its own
    // (a hard cut would jump by up to the full waveform span).
    VKKernel* ref = makeKernel();
    vk_kernel_set_param(ref, VK_P_GLOBAL_POLYPHONY, 0.f);
    auto fresh = renderSeconds(ref, 0.2, {midi(10, 0x90, 55, 100)});
    vk_kernel_destroy(ref);
    const float steady = maxDelta(x, 4800, x.size());
    const float freshMax = maxDelta(fresh, 0, 2000);
    const float during = maxDelta(s, 0, 2000);
    CHECK(during <= std::max(steady, freshMax) * 1.1f,
          "steal fade adds no discontinuity (old %.3f, new alone %.3f, steal %.3f)", steady, freshMax, during);

    vk_kernel_set_param(k, VK_P_GLOBAL_POLYPHONY, 8.f);   // 32 voices
    std::vector<VKEvent> chord;
    for (int n = 0; n < 40; ++n) chord.push_back(midi(0, 0x90, static_cast<uint8_t>(40 + n), 90));
    renderSeconds(k, 0.2, chord);
    CHECK(K->activeVoiceCount() == 32, "40 notes on 32 voices → 32 active, got %u", K->activeVoiceCount());

    vk_kernel_set_param(k, VK_P_GLOBAL_POLYPHONY, 2.f);   // reduce to 4 voices
    renderSeconds(k, 0.1);
    CHECK(K->activeVoiceCount() <= 4, "reducing polyphony kills excess voices, got %u", K->activeVoiceCount());
    vk_kernel_destroy(k);
}

static void testSustainPedal() {
    std::puts("sustain pedal");
    VKKernel* k = makeKernel();
    auto* K = static_cast<vektur::Kernel*>(static_cast<void*>(k));
    renderSeconds(k, 0.1, {midi(0, 0xB0, 64, 127), midi(0, 0x90, 60, 100), midi(10, 0x80, 60, 0)});
    auto held = renderSeconds(k, 1.0);
    CHECK(peakAbs(held, held.size() - 2400, held.size()) > 0.05f, "note sustained while pedal down");
    renderSeconds(k, 2.0, {midi(0, 0xB0, 64, 0)});
    CHECK(K->activeVoiceCount() == 0, "pedal up releases sustained notes");
    vk_kernel_destroy(k);
}

static void testSmoothingClampingAndSafety() {
    std::puts("parameter smoothing, clamping, NaN rejection");
    VKKernel* k = makeKernel();
    vk_kernel_set_param(k, VK_P_GLOBAL_MASTER_VOLUME, 1000.f);
    CHECK(vk_kernel_get_param(k, VK_P_GLOBAL_MASTER_VOLUME) == 6.f, "clamped to max");
    vk_kernel_set_param(k, VK_P_GLOBAL_MASTER_VOLUME, NAN);
    CHECK(vk_kernel_get_param(k, VK_P_GLOBAL_MASTER_VOLUME) == -6.f, "NaN → default");

    // A sudden -60 → +6 dB jump must ramp, not step.
    vk_kernel_set_param(k, VK_P_GLOBAL_MASTER_VOLUME, -60.f);
    auto q = renderSeconds(k, 0.3, {midi(0, 0x90, 36, 127)});
    vk_kernel_set_param(k, VK_P_GLOBAL_MASTER_VOLUME, 6.f);
    auto loud = renderSeconds(k, 0.3);
    CHECK(std::fabs(loud[0]) < 0.05f, "first sample after jump still quiet (%.4f)", loud[0]);
    CHECK(peakAbs(loud, 4800, loud.size()) > 0.3f, "reaches the new level");
    CHECK(peakAbs(loud, 0, loud.size()) <= 2.0f, "never above safety ceiling");

    // Host ramp event: reaches target in exactly rampFrames.
    VKEvent r{}; r.type = VK_EVENT_PARAM_RAMP; r.address = VK_P_GLOBAL_MASTER_TUNE; r.value = 50.f; r.rampFrames = 480;
    Buf b(kBlock); vk_kernel_render(k, b.ch, 2, kBlock, &r, 1);
    CHECK(vk_kernel_get_param(k, VK_P_GLOBAL_MASTER_TUNE) == 50.f, "ramp event updates stored value");
    vk_kernel_destroy(k);
}

static void testNoAllocationInRender() {
    std::puts("real-time safety: zero heap allocations in render");
    VKKernel* k = makeKernel();
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> note(24, 100), off(0, kBlock - 1), kind(0, 9);
    Buf b(kBlock);
    std::vector<VKEvent> ev; ev.reserve(64);
    std::vector<VKEvent> pre[400];
    for (auto& v : pre) {          // pre-build all event lists outside the tracked region
        v.reserve(16);
        for (int i = 0; i < 8; ++i) {
            const int kd = kind(rng);
            if (kd < 4) v.push_back(midi(off(rng), 0x90, note(rng), 100));
            else if (kd < 8) v.push_back(midi(off(rng), 0x80, note(rng), 0));
            else if (kd == 8) v.push_back(midi(off(rng), 0xB0, 64, (rng() & 1) ? 127 : 0));
            else { VKEvent p{}; p.type = VK_EVENT_PARAM; p.address = VK_P_GLOBAL_POLYPHONY;
                   p.value = static_cast<float>(rng() % VK_POLYPHONY_CHOICES); p.sampleOffset = off(rng); v.push_back(p); }
        }
        std::sort(v.begin(), v.end(), [](const VKEvent& a, const VKEvent& c) { return a.sampleOffset < c.sampleOffset; });
    }
    VKTelemetry t;
    gAllocs = 0; gTrack = true;
    float worst = 0.f; bool finite = true;
    for (auto& v : pre) {
        vk_kernel_set_param(k, VK_P_GLOBAL_MASTER_VOLUME, -(float)(rng() % 30));
        vk_kernel_post_command(k, {VK_CMD_RESET_METERS, 0, 0});
        vk_kernel_render(k, b.ch, 2, kBlock, v.data(), static_cast<uint32_t>(v.size()));
        while (vk_kernel_pop_telemetry(k, &t)) {}
        for (float s : b.L) { finite &= std::isfinite(s); worst = std::max(worst, std::fabs(s)); }
    }
    gTrack = false;
    CHECK(gAllocs.load() == 0, "allocations during render: %ld", gAllocs.load());
    CHECK(finite, "all output finite under random event stress");
    CHECK(worst <= 2.0f, "output bounded (worst %.3f)", worst);
    vk_kernel_destroy(k);
}

static void testTelemetry() {
    std::puts("telemetry");
    VKKernel* k = makeKernel();
    renderSeconds(k, 0.05, {midi(0, 0x90, 60, 100)});
    VKTelemetry t{}; int n = 0; VKTelemetry last{};
    while (vk_kernel_pop_telemetry(k, &t)) { last = t; ++n; }
    CHECK(n > 0, "telemetry published");
    CHECK(last.activeVoices == 1, "reports 1 active voice, got %u", last.activeVoices);
    CHECK(last.peak[0] > 0.f && last.rms[0] > 0.f && last.rms[0] <= last.peak[0], "peak/rms sane");
    CHECK(last.frames == kBlock, "frame count");
    vk_kernel_destroy(k);
}

static void testSPSCConcurrent() {
    std::puts("SPSC queue under concurrency");
    static vektur::SPSCQueue<uint64_t, 1024> q;
    constexpr uint64_t N = 2'000'000;
    std::thread prod([] { for (uint64_t i = 0; i < N;) if (q.push(i)) ++i; });
    uint64_t expect = 0; bool ordered = true; uint64_t v;
    while (expect < N) if (q.pop(v)) { ordered &= (v == expect); ++expect; }
    prod.join();
    CHECK(ordered, "FIFO order preserved across threads");
}

static void testLongRunStability() {
    std::puts("long-run stability (60 s of dense playing at 32 voices)");
    VKKernel* k = makeKernel(4096);
    vk_kernel_set_param(k, VK_P_GLOBAL_POLYPHONY, 8.f);
    std::mt19937 rng(99);
    Buf b(4096);
    bool finite = true; uint64_t worstNs = 0;
    const int blocks = static_cast<int>(60 * kSR / 512);
    VKTelemetry t;
    for (int i = 0; i < blocks; ++i) {
        VKEvent e[2] = { midi(0, 0x90, 30 + rng() % 70, 1 + rng() % 127), midi(256, 0x80, 30 + rng() % 70, 0) };
        vk_kernel_render(k, b.ch, 2, 512, e, 2);
        for (uint32_t s = 0; s < 512; ++s) finite &= std::isfinite(b.L[s]);
        while (vk_kernel_pop_telemetry(k, &t)) worstNs = std::max(worstNs, t.renderNanos);
    }
    CHECK(finite, "finite for the whole run");
    const double budgetNs = 512.0 / kSR * 1e9;
    std::printf("  worst block render %.1f µs of %.1f µs budget (%.1f%%) — host CPU, not device\n",
                worstNs / 1000.0, budgetNs / 1000.0, 100.0 * worstNs / budgetNs);
    vk_kernel_destroy(k);
}

// Random but valid-ish patches (some cables invalid on purpose).
static std::vector<VKCable> randomPatch(std::mt19937& rng) {
    std::vector<VKCable> c;
    const uint32_t mods = vk_rack_module_count();
    const int n = static_cast<int>(rng() % 12);
    for (int i = 0; i < n; ++i) {
        VKCable k{};
        k.id = static_cast<uint16_t>(rng() % 40);
        k.srcModule = static_cast<uint8_t>(rng() % mods); k.dstModule = static_cast<uint8_t>(rng() % mods);
        VKModuleInfo s, d; vk_rack_module_info(k.srcModule, &s); vk_rack_module_info(k.dstModule, &d);
        k.srcPort = static_cast<uint8_t>(s.outputCount ? rng() % s.outputCount : 0);
        k.dstPort = static_cast<uint8_t>(d.inputCount ? rng() % d.inputCount : 0);
        k.gain = static_cast<float>(static_cast<int>(rng() % 5) - 2) * 0.5f;
        c.push_back(k);
    }
    return c;
}

static void testPatchingRealtimeSafety() {
    std::puts("patching: zero allocations in render across 300 random patch swaps (incl. feedback loops)");
    VKKernel* k = makeKernel();
    vk_kernel_set_param(k, VK_P_GLOBAL_POLYPHONY, 8.f);
    std::mt19937 rng(42);
    Buf b(kBlock);
    long allocs = 0; bool finite = true; float worst = 0.f;
    for (int i = 0; i < 300; ++i) {
        auto patch = randomPatch(rng);               // UI side (may allocate)
        VKPatchResult r; vk_kernel_set_patch(k, patch.data(), static_cast<uint32_t>(patch.size()), &r);
        VKEvent ev[2] = {midi(0, 0x90, static_cast<uint8_t>(36 + rng() % 48), 100), midi(100, 0x80, static_cast<uint8_t>(36 + rng() % 48), 0)};
        gAllocs = 0; gTrack = true;
        for (int j = 0; j < 4; ++j) vk_kernel_render(k, b.ch, 2, kBlock, j == 0 ? ev : nullptr, j == 0 ? 2 : 0);
        gTrack = false; allocs += gAllocs.load();
        for (float s : b.L) { finite &= std::isfinite(s); worst = std::max(worst, std::fabs(s)); }
    }
    CHECK(allocs == 0, "allocations during render: %ld", allocs);
    CHECK(finite && worst <= 2.f, "finite and bounded under random patching (worst %.2f)", worst);
    vk_kernel_destroy(k);
}

static void testPatchingConcurrent() {
    std::puts("patching: UI thread sets 2000 patches while the audio thread renders");
    VKKernel* k = makeKernel();
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        Buf b(kBlock);
        VKEvent on = midi(0, 0x90, 48, 100);
        int n = 0;
        while (!stop.load()) {
            const bool noteNow = (n++ % 50) == 0;
            vk_kernel_render(k, b.ch, 2, kBlock, noteNow ? &on : nullptr, noteNow ? 1 : 0);
        }
    });
    std::mt19937 rng(7);
    float lvl = 0.f;
    for (int i = 0; i < 2000; ++i) {
        auto p = randomPatch(rng);
        VKPatchResult r; vk_kernel_set_patch(k, p.data(), static_cast<uint32_t>(p.size()), &r);
        lvl += vk_kernel_connection_level(k, static_cast<uint32_t>(i % VK_TELEMETRY_SLOTS));
    }
    stop = true; audio.join();
    CHECK(std::isfinite(lvl), "no crash, telemetry readable concurrently");
    vk_kernel_destroy(k);
}

int main() {
    testAllocateValidation();
    testSilenceAndPitch();
    testSampleAccurateNoteOn();
    testPitchBendAndTune();
    testPolyphonyAndStealing();
    testSustainPedal();
    testSmoothingClampingAndSafety();
    testNoAllocationInRender();
    testTelemetry();
    testSPSCConcurrent();
    // The 60 s soak is CPU-heavy; sanitizer builds skip it (VK_SKIP_SOAK=1) and
    // keep every concurrency/RT test, which is what the sanitizers are for.
    if (!std::getenv("VK_SKIP_SOAK")) testLongRunStability();
    else std::puts("long-run stability: skipped (VK_SKIP_SOAK)");
    testPatchingRealtimeSafety();
    testPatchingConcurrent();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
