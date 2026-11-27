// test_phase10.cpp — Phase 9/10 engine additions: the recording tap
// (bit-exact, wait-free, drop accounting, zero allocation) and the HQ
// oscillator option (aliasing measured against Standard), plus a render
// benchmark table for docs/PERFORMANCE.md.
#include "VekturDSP.h"
#include "Kernel.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> gTrack{false};
static std::atomic<long> gAllocs{0};
void* operator new(std::size_t n) { if (gTrack.load(std::memory_order_relaxed)) gAllocs.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
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
#define CHECK(cond, ...) do { ++gChecks; if (!(cond)) { ++gFailures; std::printf("  FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("  ok   "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using namespace vektur;
constexpr double SR = 48000;

static VKEvent note(uint8_t s, uint8_t n, uint8_t v, uint32_t off = 0) { VKEvent e{}; e.type = VK_EVENT_MIDI1; e.length = 3; e.data[0] = s; e.data[1] = n; e.data[2] = v; e.sampleOffset = off; return e; }

static void testRecorder() {
    std::printf("Recording tap\n");
    auto k = std::make_unique<Kernel>(); k->allocate(SR, 2, 512);
    VKKernel* kk = reinterpret_cast<VKKernel*>(k.get());
    std::vector<float> L(256), R(256), out, rec(2 * 4096); float* ch[2] = {L.data(), R.data()};
    CHECK(vk_kernel_record_read(kk, rec.data(), 4096) == 0 && !vk_kernel_is_recording(kk), "nothing to read before recording");
    CHECK(vk_kernel_record_start(kk), "start (allocates the ring on the UI thread)");
    VKEvent on = note(0x90, 57, 100);
    std::vector<float> got; out.reserve(400 * 512); got.reserve(400 * 512 + 8192);
    gAllocs = 0; gTrack = true;
    for (int b = 0; b < 400; ++b) {
        k->render(ch, 2, 256, b == 0 ? &on : nullptr, b == 0 ? 1 : 0);
        for (int i = 0; i < 256; ++i) { out.push_back(L[i]); out.push_back(R[i]); }
        if (b % 8 == 7) { gTrack = false; const uint32_t n = vk_kernel_record_read(kk, rec.data(), 4096); got.insert(got.end(), rec.begin(), rec.begin() + 2 * n); gTrack = true; }
    }
    gTrack = false;
    vk_kernel_record_stop(kk);
    for (uint32_t n; (n = vk_kernel_record_read(kk, rec.data(), 4096)) > 0;) got.insert(got.end(), rec.begin(), rec.begin() + 2 * n);
    CHECK(gAllocs == 0, "render with the tap armed allocates nothing (%ld)", gAllocs.load());
    CHECK(got.size() == out.size() && std::equal(got.begin(), got.end(), out.begin()), "recording is bit-identical to the output (%zu frames)", got.size() / 2);
    CHECK(vk_kernel_record_dropped(kk) == 0, "no frames dropped when drained regularly");
    // A reader that never reads: the ring fills, the audio thread carries on, drops are counted.
    vk_kernel_record_start(kk);
    const int blocks = static_cast<int>(AudioRecorder::kCapacityFrames / 256) + 40;
    for (int b = 0; b < blocks; ++b) k->render(ch, 2, 256, nullptr, 0);
    CHECK(vk_kernel_record_dropped(kk) == 40ull * 256, "overflow drops whole frames and counts them (%llu)", static_cast<unsigned long long>(vk_kernel_record_dropped(kk)));
    uint64_t total = 0; for (uint32_t n; (n = vk_kernel_record_read(kk, rec.data(), 4096)) > 0;) total += n;
    CHECK(total == AudioRecorder::kCapacityFrames, "the full ring is still readable (%llu frames)", static_cast<unsigned long long>(total));
    // Concurrent reader (TSan run).
    vk_kernel_record_start(kk);
    std::atomic<bool> stop{false}; std::atomic<uint64_t> readN{0};
    std::thread reader([&] { std::vector<float> buf(2 * 2048); while (!stop) { readN += vk_kernel_record_read(kk, buf.data(), 2048); std::this_thread::yield(); } readN += vk_kernel_record_read(kk, buf.data(), 2048); });
    for (int b = 0; b < 2000; ++b) k->render(ch, 2, 256, nullptr, 0);
    vk_kernel_record_stop(kk);
    stop = true; reader.join();
    for (uint32_t n; (n = vk_kernel_record_read(kk, rec.data(), 4096)) > 0;) readN += n;
    CHECK(readN + vk_kernel_record_dropped(kk) == 2000ull * 256, "concurrent reader: every frame read or counted (%llu read)", static_cast<unsigned long long>(readN.load()));
}

// Energy between harmonics relative to the harmonics (dB) for a held note.
static double aliasing(bool hq, int midiNote) {
    auto k = std::make_unique<Kernel>(); k->allocate(SR, 2, 512);
    k->setParam(VK_P_GLOBAL_INSTABILITY, 0); k->setParam(VK_P_GLOBAL_FILTER_ROUTING, 0); k->setParam(VK_P_VCFA_CUTOFF, 20000);
    k->setParam(VK_P_VCFA_ENV_AMOUNT, 0); k->setParam(VK_P_VCFA_KEY_TRACK, 0); k->setParam(VK_P_GLOBAL_STEREO_SPREAD, 0);
    k->setParam(VK_P_GLOBAL_OSCILLATOR_QUALITY, hq ? 1.f : 0.f);
    std::vector<float> L(256), R(256), x; float* ch[2] = {L.data(), R.data()};
    VKEvent on = note(0x90, static_cast<uint8_t>(midiNote), 127);
    for (int b = 0; b < 100; ++b) { k->render(ch, 2, 256, b == 0 ? &on : nullptr, b == 0 ? 1 : 0); if (b >= 20) x.insert(x.end(), L.begin(), L.end()); }
    const size_t N = 16384; std::vector<std::complex<double>> a(N);
    for (size_t i = 0; i < N; ++i) a[i] = x[i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / N));
    for (size_t i = 1, j = 0; i < N; ++i) { size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= N; len <<= 1) { const std::complex<double> wl(std::cos(-2 * M_PI / len), std::sin(-2 * M_PI / len));
        for (size_t i = 0; i < N; i += len) { std::complex<double> w(1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } } }
    const double f0 = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0), bin = SR / N;
    double harm = 1e-30, alias = 1e-30;
    for (size_t b = 2; b < N / 2; ++b) {
        const double f = b * bin;
        if (f > 18000) break;                       // the audible band
        const double h = f / f0, dist = std::fabs(h - std::round(h)) * f0;
        const double e = std::norm(a[b]);
        if (dist < 4 * bin) harm += e; else alias += e;
    }
    return 10 * std::log10(alias / harm);
}

static void testHQ() {
    std::printf("HQ oscillators\n");
    for (int n : {96, 103}) {
        const double s = aliasing(false, n), h = aliasing(true, n);
        CHECK(h < s - 6, "note %d saw: inharmonic energy %.1f dB (standard) → %.1f dB (HQ)", n, s, h);
    }
    // HQ keeps pitch and level.
    auto lvl = [](bool hq) {
        auto k = std::make_unique<Kernel>(); k->allocate(SR, 2, 512); k->setParam(VK_P_GLOBAL_INSTABILITY, 0); k->setParam(VK_P_GLOBAL_OSCILLATOR_QUALITY, hq ? 1.f : 0.f);
        std::vector<float> L(256), R(256); float* ch[2] = {L.data(), R.data()}; VKEvent on = note(0x90, 60, 100); double e = 0;
        for (int b = 0; b < 200; ++b) { k->render(ch, 2, 256, b == 0 ? &on : nullptr, b == 0 ? 1 : 0); if (b > 50) for (float v : L) e += v * v; }
        return 10 * std::log10(e);
    };
    const double d = lvl(true) - lvl(false);
    CHECK(std::fabs(d) < 0.5, "same level at C4 (%.2f dB difference)", d);
}

static void testFFT() {
    std::printf("FFT (portable path here; vDSP on Apple via the same entry point)\n");
    for (uint32_t n : {64u, 512u, 2048u}) { const float e = vk_fft_selftest(n); CHECK(e < 1e-4f, "size %u: max error %.2e vs direct DFT", n, e); }
    CHECK(vk_fft_selftest(100) == 1.f, "non-power-of-two size rejected");
}

static void bench() {
    std::printf("Benchmark (this host, 48 kHz, 256-frame buffers, real-time factor = audio time / CPU time)\n");
    struct Cfg { const char* name; int voices; bool hq; int os; } cfgs[] = {{"8 voices, default patch", 8, false, 1}, {"16 voices", 16, false, 1},
        {"16 voices, HQ oscillators", 16, true, 1}, {"16 voices, filters 4×", 16, false, 2}, {"32 voices", 32, false, 1}};
    for (auto& c : cfgs) {
        auto k = std::make_unique<Kernel>(); k->allocate(SR, 2, 256);
        k->setParam(VK_P_GLOBAL_POLYPHONY, 8); k->setParam(VK_P_GLOBAL_OSCILLATOR_QUALITY, c.hq ? 1.f : 0.f); k->setParam(VK_P_GLOBAL_OVERSAMPLING, static_cast<float>(c.os));
        k->setParam(VK_P_VCO2_LEVEL, 80); k->setParam(VK_P_VCO3_LEVEL, 60);
        std::vector<VKEvent> ev; for (int v = 0; v < c.voices; ++v) ev.push_back(note(0x90, static_cast<uint8_t>(36 + v * 2), 100));
        std::vector<float> L(256), R(256); float* ch[2] = {L.data(), R.data()};
        k->render(ch, 2, 256, ev.data(), static_cast<uint32_t>(ev.size()));
        const int blocks = 750;   // 4 s
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) k->render(ch, 2, 256, nullptr, 0);
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), audio = blocks * 256 / SR;
        std::printf("    %-28s %6.1f× real time (%5.1f %% of one core)\n", c.name, audio / cpu, 100 * cpu / audio);
    }
}

int main(int argc, char** argv) {
    testRecorder();
    testHQ();
    testFFT();
    if (argc > 1 && std::string(argv[1]) == "--bench") bench();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
