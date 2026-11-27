// ChordAnalyzer.hpp — names the chord formed by a set of held notes.
//
// Pitch-class templates are matched against every candidate root present in
// the set. An exact match on the bass note wins, then an exact match on another
// root (written as a slash chord), then templates with the fifth omitted.
// Pure and allocation-free; used by the UI through vk_chord_name().
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace vektur {

struct ChordTemplate { const char* suffix; uint16_t mask; uint8_t rank; };

inline constexpr uint16_t pcs(std::initializer_list<int> l) {
    uint16_t m = 0; for (int i : l) m = static_cast<uint16_t>(m | (1u << (i % 12))); return m;
}

// Lower rank = preferred when several templates match the same set.
inline constexpr ChordTemplate kChordTemplates[] = {
    {"",      pcs({0, 4, 7}), 0},       {"m",     pcs({0, 3, 7}), 0},
    {"dim",   pcs({0, 3, 6}), 1},       {"aug",   pcs({0, 4, 8}), 2},
    {"sus4",  pcs({0, 5, 7}), 2},       {"sus2",  pcs({0, 2, 7}), 3},
    {"7",     pcs({0, 4, 7, 10}), 0},   {"maj7",  pcs({0, 4, 7, 11}), 0},
    {"m7",    pcs({0, 3, 7, 10}), 0},   {"mMaj7", pcs({0, 3, 7, 11}), 1},
    {"m7b5",  pcs({0, 3, 6, 10}), 1},   {"dim7",  pcs({0, 3, 6, 9}), 2},
    {"6",     pcs({0, 4, 7, 9}), 1},    {"m6",    pcs({0, 3, 7, 9}), 1},
    {"7sus4", pcs({0, 5, 7, 10}), 1},   {"add9",  pcs({0, 2, 4, 7}), 1},
    {"9",     pcs({0, 2, 4, 7, 10}), 0}, {"maj9", pcs({0, 2, 4, 7, 11}), 0},
    {"m9",    pcs({0, 2, 3, 7, 10}), 0}, {"5",    pcs({0, 7}), 0},
};

inline const char* pitchClassName(int pc) noexcept {
    static const char* names[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return names[((pc % 12) + 12) % 12];
}

inline uint16_t rotate(uint16_t mask, int root) noexcept {   // intervals relative to `root`
    uint16_t out = 0;
    for (int i = 0; i < 12; ++i) if (mask & (1u << i)) out = static_cast<uint16_t>(out | (1u << ((i - root + 12) % 12)));
    return out;
}

// Returns the root pitch class or -1; writes the name into out (always terminated).
inline int32_t chordName(const uint8_t* notes, uint32_t count, char* out, uint32_t cap) noexcept {
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    if (!notes || count == 0) return -1;
    uint16_t set = 0; int bass = 128;
    for (uint32_t i = 0; i < count; ++i) {
        if (notes[i] > 127) continue;
        set = static_cast<uint16_t>(set | (1u << (notes[i] % 12)));
        if (notes[i] < bass) bass = notes[i];
    }
    if (bass > 127) return -1;
    const int bassPc = bass % 12;
    int nPcs = 0; for (int i = 0; i < 12; ++i) nPcs += (set >> i) & 1;
    if (nPcs == 1) { std::snprintf(out, cap, "%s", pitchClassName(bassPc)); return bassPc; }

    // score: 0 exact on bass, 1 exact on other root (slash), 2/3 = same with fifth omitted
    int bestScore = 99, bestRank = 99, bestRoot = -1; const char* bestSuffix = nullptr;
    for (int root = 0; root < 12; ++root) {
        if (!(set & (1u << root))) continue;
        const uint16_t rel = rotate(set, root);
        for (const auto& t : kChordTemplates) {
            int score = -1;
            if (rel == t.mask) score = root == bassPc ? 0 : 1;
            else if ((t.mask & (1u << 7)) && nPcs == 3 && __builtin_popcount(t.mask) == 4 &&
                     rel == static_cast<uint16_t>(t.mask & ~(1u << 7)))
                score = root == bassPc ? 2 : 3;   // seventh chord with the fifth omitted (common voicing)
            if (score < 0) continue;
            if (score < bestScore || (score == bestScore && t.rank < bestRank)) {
                bestScore = score; bestRank = t.rank; bestRoot = root; bestSuffix = t.suffix;
            }
        }
    }
    if (bestRoot < 0) return -1;
    if (bestRoot == bassPc) std::snprintf(out, cap, "%s%s", pitchClassName(bestRoot), bestSuffix);
    else std::snprintf(out, cap, "%s%s/%s", pitchClassName(bestRoot), bestSuffix, pitchClassName(bassPc));
    return bestRoot;
}

} // namespace vektur
