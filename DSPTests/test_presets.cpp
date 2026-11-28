// test_presets.cpp — Phase 9: every factory preset loads, plays, stays
// finite, is gain-staged (loudness −20 dBFS ± 6, peaks ≤ −0.5 dBFS), uses only
// cables the engine accepts, and is audibly distinct from every other preset
// (timbre + envelope fingerprint distance).
#include "preset_audition.hpp"
#include <cstdio>

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...) do { ++gChecks; if (!(cond)) { ++gFailures; std::printf("  FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("  ok   "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    std::printf("Factory presets\n");
    CHECK(kFactory.size() == 64, "64 presets (%zu)", kFactory.size());
    int bank0 = 0; for (auto& p : kFactory) bank0 += p.bank == 0;
    CHECK(bank0 == 32, "two banks of 32");
    std::vector<Audition> au;
    bool finite = true, staged = true, cablesOk = true;
    double lo = 0, hi = -200, worstPeak = -200;
    for (auto& p : kFactory) {
        au.push_back(audition(p));
        const Audition& a = au.back();
        finite = finite && a.finite;
        if (a.rms < -26 || a.rms > -14 || a.peak > -0.5) { staged = false; std::printf("    %s: loudness %.1f, peak %.1f\n", p.name, a.rms, a.peak); }
        if (a.rejectedCables) { cablesOk = false; std::printf("    %s: %d cable(s) rejected\n", p.name, a.rejectedCables); }
        lo = std::min(lo, a.rms); hi = std::max(hi, a.rms); worstPeak = std::max(worstPeak, a.peak);
    }
    CHECK(finite, "all presets finite");
    CHECK(staged, "gain-staged: loudness %.1f … %.1f dBFS, highest peak %.1f dBFS", std::min(lo, hi), hi, worstPeak);
    CHECK(cablesOk, "every preset cable accepted by the engine");
    double minD = 1e9; size_t mi = 0, mj = 0;
    for (size_t i = 0; i < au.size(); ++i) for (size_t j = i + 1; j < au.size(); ++j) {
        double d = 0; for (size_t f = 0; f < au[i].features.size(); ++f) d += (au[i].features[f] - au[j].features[f]) * (au[i].features[f] - au[j].features[f]);
        d = std::sqrt(d);
        if (d < minD) { minD = d; mi = i; mj = j; }
    }
    CHECK(minD > 0.25, "audibly distinct: closest pair “%s” / “%s” at distance %.2f", kFactory[mi].name, kFactory[mj].name, minD);
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
