// SeqCommon.hpp — shared pieces of the Phase 5 note generators (arpeggiator,
// step sequencer, generative engine): musical divisions, scales, counter-based
// random numbers, Euclidean rhythms and a fixed-capacity note-event queue.
//
// Tables are PERMANENT and mirror tools/params.py (DIVISIONS, SCALES,
// ARP_PATTERNS). Everything here is allocation-free and real-time safe.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vektur::seq {

// Beats (quarter notes) per step for each DIVISIONS entry.
inline constexpr double kDivisionBeats[15] = {
    4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
    3.0, 1.5, 0.75, 0.375,
    4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0,
};
inline double divisionBeats(long index) noexcept { return kDivisionBeats[std::clamp(index, 0L, 14L)]; }

// Scales as 12-bit pitch-class masks (bit k = semitone k above the root).
struct Scale { uint16_t mask; uint8_t size; uint8_t degrees[12]; };
inline constexpr uint16_t kScaleMasks[16] = {
    0xFFF,                                  // chromatic
    0b101010110101,                         // major            0 2 4 5 7 9 11
    0b010110101101,                         // natural minor    0 2 3 5 7 8 10
    0b100110101101,                         // harmonic minor   0 2 3 5 7 8 11
    0b101010101101,                         // melodic minor    0 2 3 5 7 9 11
    0b011010101101,                         // dorian           0 2 3 5 7 9 10
    0b010110101011,                         // phrygian         0 1 3 5 7 8 10
    0b101011010101,                         // lydian           0 2 4 6 7 9 11
    0b011010110101,                         // mixolydian       0 2 4 5 7 9 10
    0b010101101011,                         // locrian          0 1 3 5 6 8 10
    0b001010010101,                         // major pentatonic 0 2 4 7 9
    0b010010101001,                         // minor pentatonic 0 3 5 7 10
    0b010011101001,                         // blues            0 3 5 6 7 10
    0b010101010101,                         // whole tone       0 2 4 6 8 10
    0b100111001101,                         // hungarian minor  0 2 3 6 7 8 11
    0b010110110011,                         // phrygian dom.    0 1 4 5 7 8 10
};
inline Scale makeScale(long index) noexcept {
    Scale s{kScaleMasks[std::clamp(index, 0L, 15L)], 0, {}};
    for (uint8_t k = 0; k < 12; ++k) if (s.mask & (1u << k)) s.degrees[s.size++] = k;
    return s;
}

// Nearest pitch in the scale (ties resolve downwards, like a hardware
// quantiser comparing against the lower threshold first).
inline int quantise(int note, int root, const Scale& s) noexcept {
    for (int d = 0; d < 12; ++d) {
        const int lo = note - d, hi = note + d;
        if (s.mask & (1u << (((lo - root) % 12 + 12) % 12))) return lo;
        if (s.mask & (1u << (((hi - root) % 12 + 12) % 12))) return hi;
    }
    return note;
}

// Pitch of scale degree `deg` (may be negative or ≥ size) above `rootNote`.
inline int degreePitch(int rootNote, const Scale& s, int deg) noexcept {
    const int n = s.size ? s.size : 1;
    const int oct = deg >= 0 ? deg / n : -((-deg + n - 1) / n);
    const int idx = deg - oct * n;
    return rootNote + 12 * oct + s.degrees[idx];
}

// Insertion sort for the tiny fixed arrays used here (std::sort on them
// trips a GCC 13 -Warray-bounds false positive).
template <typename T, typename Less>
inline void smallSort(T* a, int n, Less less) noexcept {
    for (int i = 1; i < n; ++i) { T x = a[i]; int j = i; while (j > 0 && less(x, a[j - 1])) { a[j] = a[j - 1]; --j; } a[j] = x; }
}
template <typename T>
inline void smallSort(T* a, int n) noexcept { smallSort(a, n, [](const T& x, const T& y) { return x < y; }); }

// Counter-based random numbers: the same (seed, a, b) always gives the same
// value, so patterns are reproducible and relocatable (host jumps, offline
// capture) without stored random state.
inline uint64_t mix64(uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
inline uint64_t hash(uint64_t seed, uint64_t a, uint64_t b = 0, uint64_t c = 0) noexcept {
    return mix64(mix64(mix64(seed ^ 0x5EEDull) ^ a) ^ (b * 0x632BE59BD9B4E019ull) ^ (c * 0xA24BAED4963EE407ull));
}
inline double unit(uint64_t h) noexcept { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }
inline double rand01(uint64_t seed, uint64_t a, uint64_t b = 0, uint64_t c = 0) noexcept { return unit(hash(seed, a, b, c)); }

// Sequential generator for code that draws many values in order.
struct Rand {
    uint64_t state;
    explicit Rand(uint64_t s) noexcept : state(mix64(s)) {}
    uint64_t next() noexcept { state = mix64(state); return state; }
    double uniform() noexcept { return unit(next()); }
    int below(int n) noexcept { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<uint64_t>(n)); }
    bool chance(double p) noexcept { return uniform() < p; }
};

// Euclidean rhythm (Bjorklund-equivalent bucket form): `pulses` onsets spread
// as evenly as possible over `steps`, rotated right by `rotate`.
inline bool euclidHit(int steps, int pulses, int rotate, int step) noexcept {
    if (steps <= 0 || pulses <= 0) return false;
    if (pulses >= steps) return true;
    const int i = (((step - rotate) % steps) + steps) % steps;
    return ((i * pulses) % steps) < pulses;
}

// Swung start time of grid tick t: odd ticks move late by (swing − 50 %) × 2
// of a step (50 % straight, 66.7 % triplet feel, 75 % dotted).
inline double swungTick(double anchor, double stepBeats, int64_t t, float swingPct) noexcept {
    const double sw = std::clamp((static_cast<double>(swingPct) - 50.0) / 50.0, 0.0, 0.5);
    return anchor + static_cast<double>(t) * stepBeats + ((t & 1) ? sw * stepBeats : 0.0);
}

// ---------------------------------------------------------------------------
// NoteQueue — pending note-ons/offs of one generator, ordered by beat time.
//
// Each note-on/off pair shares an id. When a note-on is emitted it becomes
// the pitch's current note; a note-off is only emitted for the current note,
// so an overlapping gate (> 100 %) or a retrigger never cuts the newer note
// short, while ratchets (off before the next on) still re-articulate.
// When full, the earliest event is emitted immediately to make room (a note
// may come early but never hangs).
// ---------------------------------------------------------------------------
struct NoteEvent { double beat; uint32_t gen; uint8_t note, velocity; bool on; };

template <typename Sink>
concept NoteSink = requires(Sink& s, int n, int v) { s.noteOn(n, v); s.noteOff(n); };

class NoteQueue {
public:
    static constexpr uint32_t kCapacity = 256;

    template <NoteSink S>
    void schedule(double onBeat, double offBeat, int note, int velocity, S& sink) noexcept {
        if (note < 0 || note > 127) return;
        const uint32_t g = ++nextId_;
        push({onBeat, g, static_cast<uint8_t>(note), static_cast<uint8_t>(std::clamp(velocity, 1, 127)), true}, sink);
        push({std::max(offBeat, onBeat + 1e-6), g, static_cast<uint8_t>(note), 0, false}, sink);
    }

    // Moves the pending note-off of the newest `note` (a tie) to `offBeat`.
    bool extend(int note, double offBeat) noexcept {
        if (note < 0 || note > 127) return false;
        for (uint32_t i = 0; i < count_; ++i)
            if (!ev_[i].on && ev_[i].note == note && ev_[i].gen == current_[note]) {
                NoteEvent e = ev_[i]; e.beat = offBeat;
                erase(i); insert(e); return true;
            }
        return false;
    }

    double nextBeat() const noexcept { return count_ ? ev_[0].beat : 1e300; }
    uint32_t size() const noexcept { return count_; }

    // Emits every event due at or before `beat`.
    template <NoteSink S>
    void emitUntil(double beat, S& sink) noexcept {
        while (count_ && ev_[0].beat <= beat) { const NoteEvent e = ev_[0]; erase(0); emit(e, sink); }
    }

    // Note-offs for everything sounding, pending note-ons discarded.
    template <NoteSink S>
    void flush(S& sink) noexcept {
        for (uint32_t i = 0; i < count_; ++i) if (!ev_[i].on && sounding_[ev_[i].note]) { sounding_[ev_[i].note] = false; sink.noteOff(ev_[i].note); }
        count_ = 0;
        for (int n = 0; n < 128; ++n) if (sounding_[n]) { sounding_[n] = false; sink.noteOff(n); }
    }
    void clear() noexcept { count_ = 0; for (auto& s : sounding_) s = false; }
    bool sounding(int note) const noexcept { return note >= 0 && note < 128 && sounding_[note]; }

private:
    template <NoteSink S>
    void emit(const NoteEvent& e, S& sink) noexcept {
        if (e.on) {
            // Same pitch still sounding (overlapping gate): close it first so
            // every note-on downstream has exactly one note-off. The voice
            // retriggers in place, so this is inaudible.
            if (sounding_[e.note]) sink.noteOff(e.note);
            sounding_[e.note] = true; current_[e.note] = e.gen; sink.noteOn(e.note, e.velocity);
        }
        else if (e.gen == current_[e.note] && sounding_[e.note]) { sounding_[e.note] = false; sink.noteOff(e.note); }
    }
    template <NoteSink S>
    void push(const NoteEvent& e, S& sink) noexcept {
        if (count_ == kCapacity) { const NoteEvent first = ev_[0]; erase(0); emit(first, sink); }
        insert(e);
    }
    void insert(const NoteEvent& e) noexcept {
        uint32_t i = count_;
        // Stable: equal times keep insertion order, offs before ons at the same instant.
        while (i > 0 && (ev_[i - 1].beat > e.beat || (ev_[i - 1].beat == e.beat && ev_[i - 1].on && !e.on))) { ev_[i] = ev_[i - 1]; --i; }
        ev_[i] = e; ++count_;
    }
    void erase(uint32_t i) noexcept { for (uint32_t j = i + 1; j < count_; ++j) ev_[j - 1] = ev_[j]; --count_; }

    NoteEvent ev_[kCapacity]{};
    uint32_t count_ = 0;
    uint32_t current_[128]{};   // id of the latest emitted note-on per pitch
    uint32_t nextId_ = 0;
    bool sounding_[128]{};
};

// Clock seen by the generators for one span: the beat at the span start and
// whether the host transport is moving.
struct ClockState { double beat = 0.0; double tempo = 120.0; bool hostPlaying = false; bool hostPresent = false; };

} // namespace vektur::seq
