// Generative.cpp — see Generative.hpp. Pure, allocation-free, bounded.
#include "Generative.hpp"

#include <climits>

namespace vektur::seq {

namespace {

// Functional-harmony transition weights between the seven diatonic degrees
// (row = from, column = to): I ii iii IV V vi vii°.
constexpr float kMarkov[7][7] = {
    {0.4f, 2.0f, 1.0f, 3.0f, 3.0f, 2.5f, 0.2f},   // I   → IV, V, vi, ii
    {0.5f, 0.2f, 0.3f, 1.0f, 5.0f, 0.5f, 1.0f},   // ii  → V
    {0.5f, 0.5f, 0.2f, 2.0f, 0.5f, 3.0f, 0.1f},   // iii → vi, IV
    {2.5f, 2.0f, 0.3f, 0.2f, 3.0f, 1.0f, 0.3f},   // IV  → V, I, ii
    {5.0f, 0.3f, 0.5f, 1.0f, 0.2f, 2.0f, 0.1f},   // V   → I, vi
    {0.5f, 3.0f, 1.0f, 3.0f, 2.0f, 0.2f, 0.1f},   // vi  → ii, IV, V
    {4.0f, 0.2f, 1.0f, 0.3f, 0.5f, 0.5f, 0.1f},   // vii°→ I
};

int pickWeighted(const float* w, int n, Rand& r) noexcept {
    float total = 0.f;
    for (int i = 0; i < n; ++i) total += w[i];
    if (total <= 0.f) return 0;
    float x = static_cast<float>(r.uniform()) * total;
    for (int i = 0; i < n; ++i) { x -= w[i]; if (x <= 0.f) return i; }
    return n - 1;
}

// Metric strength of a tick inside the bar (1 = downbeat). The half-bar
// accent exists for even meters (4/4, 6/8 counted in quarters, …).
float metric(int tickInBar, double stepBeats, double beatsPerBar = 4.0) noexcept {
    const double pos = tickInBar * stepBeats + 1e-7;
    auto on = [&](double unit) { return std::fmod(pos, unit) < 2e-7; };
    if (pos < 2e-7) return 1.0f;
    const double half = beatsPerBar * 0.5;
    if (std::fmod(beatsPerBar, 2.0) < 1e-9 && std::fabs(pos - half) < 2e-7) return 0.85f;
    if (on(1.0)) return 0.7f;
    if (on(0.5)) return 0.45f;
    if (on(0.25)) return 0.3f;
    return 0.2f;
}

struct Builder {
    const GenSettings& s;
    Scale sc;              // scale used for harmony and melody (chromatic → major)
    Phrase& out;
    int n = 7;

    // Nearest scale degree index (any octave) to a pitch.
    int toDeg(int pitch) const noexcept {
        const int o = (pitch - s.root) >= 0 ? (pitch - s.root) / 12 : -((s.root - pitch + 11) / 12);
        int best = INT_MAX, deg = 0;
        for (int oo = o - 1; oo <= o + 1; ++oo)
            for (int k = 0; k < n; ++k) {
                const int p = s.root + 12 * oo + sc.degrees[k];
                const int d = std::abs(p - pitch);
                if (d < best) { best = d; deg = oo * n + k; }
            }
        return deg;
    }
    int pitchOf(int deg) const noexcept { return degreePitch(s.root, sc, deg); }
    // Semitone offsets above the root of the chord on `deg` (3 or 4 tones).
    int chordTones(int deg, bool seventh, int* tones) const noexcept {
        const int count = seventh ? 4 : 3;
        for (int k = 0; k < count; ++k) tones[k] = pitchOf(deg + 2 * k) - s.root;
        return count;
    }
    // Chord tone nearest `target` (pitch), restricted to [lo, hi] when possible.
    int nearestChordTone(const int* tones, int count, int target, int lo, int hi) const noexcept {
        int best = target, bestD = INT_MAX;
        for (int k = 0; k < count; ++k)
            for (int o = -1; o <= 11; ++o) {
                const int p = s.root + tones[k] % 12 + 12 * o;
                if (p < lo || p > hi) continue;
                const int d = std::abs(p - target);
                if (d < bestD) { bestD = d; best = p; }
            }
        return std::clamp(best, 0, 127);
    }
    void add(int tick, int length, int note, int vel) noexcept {
        if (out.count >= Phrase::kMaxNotes || note < 0 || note > 127) return;
        out.notes[out.count++] = {static_cast<uint16_t>(tick), static_cast<uint16_t>(std::max(length, 1)),
                                  static_cast<uint8_t>(note), static_cast<uint8_t>(std::clamp(vel, 1, 127))};
    }
    int humanised(int vel, Rand& r) const noexcept {
        return std::clamp(vel + static_cast<int>(std::lround((r.uniform() * 2.0 - 1.0) * 25.0 * s.humanise)), 1, 127);
    }
};

int degreeWithOffset(const Scale& sc, int semis) noexcept {
    int best = 0, bestD = 99;
    for (int k = 0; k < sc.size; ++k) { const int d = std::abs(sc.degrees[k] - semis); if (d < bestD) { bestD = d; best = k; } }
    return best;
}

void progression(const GenSettings& s, const Scale& sc, uint32_t repeat, int* deg) noexcept {
    const int n = sc.size;
    uint32_t variant = 0;
    if (repeat > 0 && s.evolve > 0.f)
        for (uint32_t r = repeat; r >= 1 && r + 64 > repeat; --r)
            if (rand01(s.seed, 0xE8, r) < s.evolve / 3.0) { variant = r; break; }
    Rand r(hash(s.seed, 0xC0, variant));
    const int dominant = degreeWithOffset(sc, 7), subdominant = degreeWithOffset(sc, 5);
    deg[0] = 0;
    for (int b = 1; b < s.bars; ++b) {
        if (b == s.bars - 1) { deg[b] = r.chance(s.complexity * 0.3) ? subdominant : dominant; continue; }
        float w[12];
        const int from = deg[b - 1];
        for (int k = 0; k < n; ++k) {
            if (n == 7) w[k] = kMarkov[from][k];
            else {   // non-heptatonic: favour consonant roots relative to the tonic
                const int off = sc.degrees[k];
                w[k] = off == 0 ? 2.5f : off == 7 ? 3.f : off == 5 ? 2.5f : (off == 9 || off == 3 || off == 4) ? 2.f : 0.7f;
                if (k == from) w[k] *= 0.2f;
            }
            const bool colour = n == 7 && (k == 1 || k == 2 || k == 6);
            if (colour) w[k] *= 0.3f + 1.4f * s.complexity;
        }
        deg[b] = pickWeighted(w, n, r);
    }
}

} // namespace

uint32_t barVariant(const GenSettings& s, int bar, uint32_t repeat) noexcept {
    if (repeat == 0 || s.evolve <= 0.f) return 0;
    for (uint32_t r = repeat; r >= 1 && r + 64 > repeat; --r)
        if (rand01(s.seed, 0xE7, static_cast<uint64_t>(bar), r) < s.evolve) return r;
    return 0;
}

void generatePhrase(const GenSettings& sIn, uint32_t repeat, Phrase& out) noexcept {
    GenSettings s = sIn;
    s.bars = std::clamp(s.bars, 1, Phrase::kMaxBars);
    out.count = 0;
    out.bars = s.bars;
    out.ticksPerBar = std::clamp(static_cast<int>(std::floor(s.beatsPerBar / s.stepBeats + 1e-9)), 1, 48);
    const int tpb = out.ticksPerBar, total = tpb * s.bars;
    Builder B{s, makeScale(s.scale == 0 ? 1 : s.scale), out};
    B.n = B.sc.size;
    const float dens = s.density;
    if (dens <= 0.f) {
        for (int b = 0; b < s.bars; ++b) { out.chordDegree[b] = 0; out.chordRoot[b] = s.root; out.chordSeventh[b] = false; }
        return;
    }

    int degs[Phrase::kMaxBars];
    progression(s, B.sc, repeat, degs);
    for (int b = 0; b < s.bars; ++b) { out.chordDegree[b] = degs[b]; out.chordRoot[b] = ((B.pitchOf(degs[b]) % 12) + 12) % 12; }

    const int span = s.rangeOctaves * 6;
    int lo = std::clamp(s.registerNote - span, 0, 115), hi = std::clamp(s.registerNote + span, lo + 12, 127);

    // Onsets for the whole phrase first (lengths need the next onset).
    bool onset[Phrase::kMaxBars * 48]{};
    for (int b = 0; b < s.bars; ++b) {
        Rand r(hash(s.seed, 0xB0 + static_cast<uint64_t>(s.style), static_cast<uint64_t>(b), barVariant(s, b, repeat)));
        bool any = false;
        for (int i = 0; i < tpb; ++i) {
            const float m = metric(i, s.stepBeats, s.beatsPerBar);
            float pr;
            switch (s.style) {
                case 1: pr = dens * (0.15f + 0.85f * m) * 0.9f; break;                       // bass
                case 2: pr = 0.f; break;                                                     // chords: fixed below
                case 3: pr = m >= 0.7f ? dens * 0.3f : 0.f; break;                           // ambient: sparse, on beats
                case 4: pr = 0.35f + 0.65f * dens; break;                                    // broken chords
                default: pr = dens * (0.2f + 0.8f * m); break;                               // melody
            }
            bool hit = r.chance(pr);
            if (s.style == 2) {
                const double pos = i * s.stepBeats;
                hit = i == 0 || (dens > 0.45f && s.beatsPerBar >= 3.0 && std::fabs(pos - std::floor(s.beatsPerBar * 0.5 + 0.5)) < 1e-6)
                      || (dens > 0.7f && s.complexity > 0.6f && std::fabs(pos - (s.beatsPerBar - 0.5)) < 1e-6);
            }
            if (i == 0 && (b == 0 || s.style == 1)) hit = true;   // phrase (and every bass bar) starts on the one
            onset[b * tpb + i] = hit; any |= hit;
        }
        if (!any && s.style != 3 && dens > 0.1f) onset[b * tpb] = true;
    }
    auto lengthAt = [&](int t, int cap) { int k = t + 1; while (k < total && !onset[k]) ++k; return std::min(k - t, cap); };

    int cur;
    { int tones[4]; const int c = B.chordTones(degs[0], false, tones); cur = B.nearestChordTone(tones, c, s.registerNote, lo, hi); }
    int dir = 1;
    int prevVoicing[5]{}; int prevCount = 0;
    int arpIdx = 0;

    for (int b = 0; b < s.bars; ++b) {
        Rand r(hash(s.seed, 0xD0 + static_cast<uint64_t>(s.style), static_cast<uint64_t>(b), barVariant(s, b, repeat)));
        int tones[4];
        const bool seventh = rand01(s.seed, 0x7E, static_cast<uint64_t>(b), barVariant(s, b, repeat)) < s.complexity * 0.7;
        const int tc = B.chordTones(degs[b], seventh, tones);
        out.chordSeventh[b] = seventh;
        const int chordRootPc = (s.root + tones[0]) % 12;
        const uint32_t barStart = out.count;

        switch (s.style) {
        case 1: {   // bassline
            const int target = std::clamp(s.registerNote - 24, 28, 60);
            int rootNote = target - ((target - chordRootPc) % 12 + 12) % 12;
            if (target - rootNote > 6) rootNote += 12;
            int nextRoot = rootNote;
            if (b + 1 < s.bars) { int nt[4]; B.chordTones(degs[b + 1], false, nt); const int pc = (s.root + nt[0]) % 12;
                nextRoot = target - ((target - pc) % 12 + 12) % 12; if (target - nextRoot > 6) nextRoot += 12; }
            int lastOnset = -1;
            for (int i = 0; i < tpb; ++i) if (onset[b * tpb + i]) lastOnset = i;
            for (int i = 0; i < tpb; ++i) {
                if (!onset[b * tpb + i]) continue;
                int p = rootNote;
                if (i > 0) {
                    if (i == lastOnset && r.chance(s.complexity * 0.8)) p = B.pitchOf(B.toDeg(nextRoot) + (r.chance(0.5) ? -1 : 1));
                    else {
                        const float w[5] = {0.35f, 0.25f, 0.15f, 0.15f, 0.1f};
                        switch (pickWeighted(w, 5, r)) {
                            case 1: p = rootNote + (tones[2] - tones[0]); break;          // fifth
                            case 2: p = rootNote + 12; break;                             // octave
                            case 3: p = rootNote + (tones[1] - tones[0]); break;          // third
                            case 4: p = B.pitchOf(B.toDeg(rootNote) + (r.chance(0.5) ? 1 : -1)); break;
                            default: break;
                        }
                    }
                }
                B.add(b * tpb + i, lengthAt(b * tpb + i, tpb), p, B.humanised(i == 0 ? 104 : 86, r));
            }
            break;
        }
        case 2: {   // chords, voice-led
            int centre = s.registerNote;
            if (prevCount) { int sum = 0; for (int k = 0; k < prevCount; ++k) sum += prevVoicing[k]; centre = sum / prevCount; }
            int v[5]; int vc = 0;
            for (int k = 0; k < tc; ++k) {
                const int pc = (s.root + tones[k]) % 12;
                int p = centre - ((centre - pc) % 12 + 12) % 12;
                if (centre - p > 6) p += 12;
                p = std::clamp(p, 0, 127);
                bool dup = false; for (int j = 0; j < vc; ++j) dup |= v[j] == p;
                if (!dup) v[vc++] = p;
            }
            smallSort(v, vc);
            prevCount = vc; for (int k = 0; k < vc; ++k) prevVoicing[k] = v[k];
            const bool bassRoot = s.complexity > 0.3f;
            for (int i = 0; i < tpb; ++i) {
                if (!onset[b * tpb + i]) continue;
                const int len = lengthAt(b * tpb + i, tpb * 2);
                for (int k = 0; k < vc; ++k) B.add(b * tpb + i, len, v[k], B.humanised(i == 0 ? 84 : 74, r));
                if (bassRoot) { int br = v[0] - 12; while ((br % 12 + 12) % 12 != chordRootPc) --br; B.add(b * tpb + i, len, br, B.humanised(80, r)); }
            }
            break;
        }
        case 3: {   // ambient: long overlapping chord tones (+ add9)
            int ext[5]; int ec = tc; for (int k = 0; k < tc; ++k) ext[k] = tones[k];
            if (r.chance(s.complexity)) ext[ec++] = B.pitchOf(degs[b] + 8) - s.root;
            bool any = false;
            for (int i = 0; i < tpb; ++i) {
                if (!onset[b * tpb + i]) continue;
                any = true;
                const int target = lo + r.below(std::max(1, hi - lo));
                const int p = B.nearestChordTone(ext, ec, target, lo, hi);
                const int len = std::max(1, static_cast<int>(std::lround((2.0 + 6.0 * r.uniform()) / s.stepBeats)));
                B.add(b * tpb + i, std::min(len, total - (b * tpb + i) + tpb), p, B.humanised(62 + r.below(18), r));
            }
            if (!any && b == 0) B.add(0, tpb, B.nearestChordTone(tones, tc, s.registerNote, lo, hi), 70);
            break;
        }
        case 4: {   // broken chords across two octaves
            int spread[12]; int sn = 0;
            for (int o = -1; o <= 0; ++o)
                for (int k = 0; k < tc && sn < 12; ++k) {
                    const int pc = (s.root + tones[k]) % 12;
                    int p = s.registerNote + 12 * o - ((s.registerNote - pc) % 12 + 12) % 12;
                    if (p < s.registerNote + 12 * o) p += 12;
                    spread[sn++] = std::clamp(p, 0, 127);
                }
            smallSort(spread, sn);
            for (int i = 0; i < tpb; ++i) {
                if (!onset[b * tpb + i]) continue;
                int idx;
                if (s.complexity < 0.33f) idx = arpIdx % sn;
                else if (s.complexity < 0.66f) { const int per = std::max(1, 2 * sn - 2); const int ph = arpIdx % per; idx = ph < sn ? ph : per - ph; }
                else idx = static_cast<int>(hash(s.seed, 0xBC, static_cast<uint64_t>(b), static_cast<uint64_t>(i)) % static_cast<uint64_t>(sn));
                ++arpIdx;
                B.add(b * tpb + i, 1, spread[idx], B.humanised(metric(i, s.stepBeats, s.beatsPerBar) >= 0.7f ? 96 : 78, r));
            }
            break;
        }
        default: {  // melody
            int lastTick = -1;
            for (int t = total - 1; t >= 0; --t) if (onset[t]) { lastTick = t; break; }
            for (int i = 0; i < tpb; ++i) {
                const int t = b * tpb + i;
                if (!onset[t]) continue;
                const float m = metric(i, s.stepBeats, s.beatsPerBar);
                int p;
                if (t == lastTick) p = B.nearestChordTone(tones, tc, cur, lo, hi);           // cadence on a chord tone
                else if (m >= 0.7f) p = B.nearestChordTone(tones, tc, cur + dir * 2, lo, hi);
                else {
                    int d = B.toDeg(cur);
                    const int leap = r.chance(s.complexity * 0.35) ? 2 + r.below(3) : 1;
                    if (r.chance(0.25)) dir = -dir;
                    d += dir * leap;
                    p = B.pitchOf(d);
                    if (p > hi || p < lo) { dir = -dir; p = B.pitchOf(d + 2 * dir * leap); }
                    p = std::clamp(p, lo, hi);
                }
                if (p == cur && m < 0.7f && r.chance(0.5)) p = B.pitchOf(B.toDeg(cur) + dir);
                cur = std::clamp(p, 0, 127);
                B.add(t, lengthAt(t, tpb), cur, B.humanised(static_cast<int>(80 + 30 * m), r));
            }
            break;
        }
        }
        // Keep the bar sorted by tick (ambient picks positions freely).
        for (uint32_t i = barStart + 1; i < out.count; ++i) {
            const GenNote x = out.notes[i]; uint32_t j = i;
            while (j > barStart && out.notes[j - 1].tick > x.tick) { out.notes[j] = out.notes[j - 1]; --j; }
            out.notes[j] = x;
        }
    }
    if (out.count == 0) B.add(0, tpb, quantise(s.registerNote, s.root, B.sc), 90);
}

} // namespace vektur::seq
