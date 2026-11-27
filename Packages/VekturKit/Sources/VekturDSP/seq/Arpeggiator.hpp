// Arpeggiator.hpp — Phase 5 arpeggiator.
//
// 14 patterns over 1–4 octaves, straight or Euclidean rhythm, step
// probability, ratchets, swing, gate 5–200 %, velocity modes, pattern chaining
// (up to four patterns, N passes each) and transposition by the step
// sequencer. Steps sit on a grid of the shared musical clock, so with a moving
// host transport the arp is locked to the song position; on a stopped
// transport it can restart from the first key instead.
//
// All randomness is counter-based (seed, tick), so a pattern is identical on
// every playback and after a host relocation. Real-time safe.
#pragma once

#include "SeqCommon.hpp"
#include "../ParameterStore.hpp"

namespace vektur::seq {

class Arpeggiator {
public:
    static constexpr uint64_t kBase = 36ull * VK_PARAMS_PER_MODULE;
    static constexpr uint32_t kMaxKeys = 32;
    static constexpr uint32_t kMaxSteps = kMaxKeys * 4;

    enum Pattern : int { Up, Down, UpDown, DownUp, UpDownRep, DownUpRep, AsPlayed, Random, RandomWalk,
                         Converge, Diverge, Thumb, Pinky, Chord, PatternCount };

    // --- keys (from the kernel's physical-key layer) -----------------------
    void keyDown(int note, int velocity, int mode, double beat, bool beatLocked) noexcept {
        if (mode == 2 && physical_ == 0) count_ = 0;        // latch: a fresh hand replaces the held set
        ++physical_;
        const bool wasEmpty = count_ == 0;
        remove(note);
        if (count_ == kMaxKeys) remove(keys_[0].note);
        keys_[count_++] = {static_cast<int8_t>(note), static_cast<uint8_t>(std::clamp(velocity, 1, 127)), ++order_};
        if (wasEmpty) restart(beat, beatLocked);
    }
    void keyUp(int note, int mode) noexcept {
        if (physical_ > 0) --physical_;
        if (mode != 2) remove(note);
    }
    void clearKeys() noexcept { count_ = 0; physical_ = 0; }
    uint32_t keyCount() const noexcept { return count_; }

    // --- clocking ------------------------------------------------------------
    // Earliest beat at which advance() has something to do.
    double nextBeat() const noexcept {
        const double t = running() ? tickBeat_ : 1e300;
        return std::min(t, queue_.nextBeat());
    }
    bool running() const noexcept { return count_ > 0 && started_; }

    // Host relocation / transport start: flush and re-align to the grid.
    template <NoteSink S>
    void relocate(double beat, S& sink) noexcept {
        queue_.flush(sink);
        if (count_ > 0) restart(beat, true);
    }

    template <NoteSink S>
    void flush(S& sink) noexcept { queue_.flush(sink); }
    void clear() noexcept { queue_.clear(); count_ = 0; physical_ = 0; started_ = false; }

    // Plays every step due at or before `beat`. `transpose` is the sequencer
    // offset in semitones (0 when not chained).
    template <NoteSink S>
    void advance(double beat, const ParameterStore& p, int transpose, S& sink) noexcept {
        queue_.emitUntil(beat + kEps, sink);
        const double stepBeats = divisionBeats(lround(p, 4));
        if (stepBeats != stepBeats_) {   // (re)start or rate change: keep the grid continuous
            // A fresh start snaps to a grid line passed less than 1/8 step ago,
            // so a key played slightly late still sounds at once.
            const double slack = stepBeats_ < 0.0 ? 0.125 : kEps;
            if (beatLocked_) { tick_ = static_cast<int64_t>(std::ceil(beat / stepBeats - slack)); anchor_ = 0.0; }
            else { anchor_ = tickBeat_ - static_cast<double>(tick_) * stepBeats; }
            stepBeats_ = stepBeats;
            tickBeat_ = std::max(beat, swungTick(anchor_, stepBeats_, tick_, p.get(kBase + 6)));
        }
        while (running() && tickBeat_ <= beat + kEps) {
            playTick(p, transpose, sink);
            ++tick_;
            tickBeat_ = swungTick(anchor_, stepBeats_, tick_, p.get(kBase + 6));
            queue_.emitUntil(beat + kEps, sink);
        }
    }

    int lastNote() const noexcept { return lastNote_; }
    uint32_t pendingEvents() const noexcept { return queue_.size(); }

    // Pattern expansion (public for tests): writes the step order for the
    // current keys into out[], returns its period.
    uint32_t expand(int pattern, int octaves, int octaveMode, int8_t* out) const noexcept {
        int8_t sorted[kMaxKeys], played[kMaxKeys];
        const uint32_t n = sortedKeys(sorted, played);
        if (n == 0) return 0;
        int octs[8]; uint32_t no = 0;
        if (octaveMode == 1) for (int o = octaves - 1; o >= 0; --o) octs[no++] = o;
        else { for (int o = 0; o < octaves; ++o) octs[no++] = o;
               if (octaveMode == 2) for (int o = octaves - 2; o >= 1; --o) octs[no++] = o; }
        int8_t e[kMaxSteps * 2]; uint32_t L = 0;
        const int8_t* src = pattern == AsPlayed ? played : sorted;
        for (uint32_t k = 0; k < no; ++k)
            for (uint32_t i = 0; i < n && L < kMaxSteps * 2; ++i) e[L++] = clampNote(src[i] + 12 * octs[k]);
        uint32_t m = 0;
        auto put = [&](int8_t v) { if (m < kMaxSteps * 2) out[m++] = v; };
        switch (pattern) {
            case Down: for (uint32_t i = L; i-- > 0;) put(e[i]); break;
            case UpDown: for (uint32_t i = 0; i < L; ++i) put(e[i]); for (uint32_t i = L - 1; i-- > 1;) put(e[i]); break;
            case DownUp: for (uint32_t i = L; i-- > 0;) put(e[i]); for (uint32_t i = 1; i + 1 < L; ++i) put(e[i]); break;
            case UpDownRep: for (uint32_t i = 0; i < L; ++i) put(e[i]); for (uint32_t i = L; i-- > 0;) put(e[i]); break;
            case DownUpRep: for (uint32_t i = L; i-- > 0;) put(e[i]); for (uint32_t i = 0; i < L; ++i) put(e[i]); break;
            case Converge: {
                uint32_t i = 0, j = L - 1;
                while (i < j) { put(e[i++]); put(e[j--]); }
                if (i == j) put(e[i]);
                break;
            }
            case Diverge: {
                const uint32_t mid = (L - 1) / 2;
                put(e[mid]);
                for (uint32_t d = 1; d < L; ++d) { if (mid + d < L) put(e[mid + d]); if (d <= mid) put(e[mid - d]); }
                break;
            }
            case Thumb: if (L == 1) put(e[0]); else for (uint32_t i = 1; i < L; ++i) { put(e[0]); put(e[i]); } break;
            case Pinky: if (L == 1) put(e[0]); else for (uint32_t i = 0; i + 1 < L; ++i) { put(e[L - 1]); put(e[i]); } break;
            default: for (uint32_t i = 0; i < L; ++i) put(e[i]); break;   // Up, AsPlayed, Random, RandomWalk, Chord
        }
        return m;
    }

private:
    static constexpr double kEps = 1e-9;
    struct Key { int8_t note; uint8_t velocity; uint32_t order; };

    static long lround(const ParameterStore& p, uint64_t i) noexcept { return std::lround(p.get(kBase + i)); }
    static int8_t clampNote(int n) noexcept { return static_cast<int8_t>(std::clamp(n, 0, 127)); }

    void remove(int note) noexcept {
        for (uint32_t i = 0; i < count_; ++i)
            if (keys_[i].note == note) { for (uint32_t j = i + 1; j < count_; ++j) keys_[j - 1] = keys_[j]; --count_; return; }
    }
    uint32_t sortedKeys(int8_t* sorted, int8_t* played) const noexcept {
        Key k[kMaxKeys];
        for (uint32_t i = 0; i < count_; ++i) k[i] = keys_[i];
        for (uint32_t i = 0; i < count_; ++i) played[i] = keys_[i].note;   // keys_ is kept in play order
        smallSort(k, static_cast<int>(count_), [](const Key& a, const Key& b) { return a.note < b.note; });
        for (uint32_t i = 0; i < count_; ++i) sorted[i] = k[i].note;
        return count_;
    }
    int velocityOf(int note) const noexcept {
        for (uint32_t i = 0; i < count_; ++i) {
            const int d = note - keys_[i].note;
            if (d >= 0 && d % 12 == 0) return keys_[i].velocity;
        }
        return count_ ? keys_[count_ - 1].velocity : 100;
    }

    void restart(double beat, bool beatLocked) noexcept {
        beatLocked_ = beatLocked;
        pos_ = 0; chainIdx_ = 0; chainSteps_ = 0; walk_ = 0;
        started_ = true;
        stepBeats_ = -1.0;   // forces grid set-up at the next advance()
        if (beatLocked) { anchor_ = 0.0; tick_ = 0; tickBeat_ = beat; }
        else { anchor_ = beat; tick_ = 0; tickBeat_ = beat; }
    }

    template <NoteSink S>
    void playTick(const ParameterStore& p, int transpose, S& sink) noexcept {
        const uint64_t seed = static_cast<uint64_t>(lround(p, 17));
        const double stepStart = tickBeat_;
        const double stepEnd = std::max(swungTick(anchor_, stepBeats_, tick_ + 1, p.get(kBase + 6)), stepStart + 1e-4);
        const double stepLen = stepEnd - stepStart;

        // Rhythm and probability (rests do not advance the note pattern).
        if (lround(p, 7) == 1) {
            const int steps = static_cast<int>(lround(p, 8)) + 1;
            if (!euclidHit(steps, static_cast<int>(lround(p, 9)), static_cast<int>(lround(p, 10)), static_cast<int>(tick_ % steps))) return;
        }
        const double prob = p.get(kBase + 11) * 0.01;
        if (prob < 1.0 && rand01(seed, 0xA1, static_cast<uint64_t>(tick_)) >= prob) return;

        // Pattern (with chaining).
        const int chainLen = static_cast<int>(lround(p, 18)) + 1;
        const int pats[4] = {static_cast<int>(lround(p, 1)), static_cast<int>(lround(p, 19)),
                             static_cast<int>(lround(p, 20)), static_cast<int>(lround(p, 21))};
        const int pattern = std::clamp(pats[chainIdx_ % chainLen], 0, PatternCount - 1);
        const int octaves = static_cast<int>(lround(p, 2)) + 1;
        int8_t order[kMaxSteps * 2];
        const uint32_t period = expand(pattern, octaves, static_cast<int>(lround(p, 3)), order);
        if (period == 0) return;

        int8_t notes[kMaxSteps * 2]; uint32_t nn = 0;
        if (pattern == Chord) {
            int8_t sorted[kMaxKeys], played[kMaxKeys];
            const uint32_t n = sortedKeys(sorted, played);
            for (int o = 0; o < octaves; ++o) for (uint32_t i = 0; i < n; ++i) notes[nn++] = clampNote(sorted[i] + 12 * o);
        } else {
            uint32_t idx;
            if (pattern == Random) idx = static_cast<uint32_t>(hash(seed, 0xA2, static_cast<uint64_t>(pos_)) % period);
            else if (pattern == RandomWalk) {
                if (pos_ > 0) walk_ += rand01(seed, 0xA3, static_cast<uint64_t>(pos_)) < 0.5 ? -1 : 1;
                if (walk_ < 0) walk_ = period > 1 ? 1 : 0;
                if (walk_ >= static_cast<int>(period)) walk_ = static_cast<int>(period) - (period > 1 ? 2 : 1);
                idx = static_cast<uint32_t>(walk_);
            } else idx = static_cast<uint32_t>(pos_ % period);
            notes[nn++] = order[idx];
        }
        ++pos_;
        if (chainLen > 1 && ++chainSteps_ >= period * (static_cast<uint32_t>(lround(p, 22)) + 1)) {
            chainSteps_ = 0; pos_ = 0; walk_ = 0; chainIdx_ = (chainIdx_ + 1) % chainLen;
        }

        // Velocity.
        const int vmode = static_cast<int>(lround(p, 14));
        const double beatFrac = stepStart - std::floor(stepStart + 1e-6);
        const bool onBeat = beatFrac < 1e-4 || beatFrac > 1.0 - 1e-4;

        // Ratchets: r evenly spaced hits inside the step.
        int r = static_cast<int>(lround(p, 12)) + 1;
        if (r > 1 && rand01(seed, 0xA4, static_cast<uint64_t>(tick_)) >= p.get(kBase + 13) * 0.01) r = 1;
        const double gate = p.get(kBase + 5) * 0.01;
        const double sub = stepLen / r;
        const double len = r > 1 ? sub * std::min(gate, 0.95) : stepLen * gate;
        for (uint32_t i = 0; i < nn; ++i) {
            const int note = std::clamp(notes[i] + transpose, 0, 127);
            int vel = velocityOf(notes[i]);
            if (vmode == 1) vel = static_cast<int>(std::lround(p.get(kBase + 15)));
            else if (vmode == 2) vel = onBeat ? std::min(127, vel + 20) : std::max(1, vel * 7 / 10);
            for (int h = 0; h < r; ++h) queue_.schedule(stepStart + h * sub, stepStart + h * sub + len, note, vel, sink);
            lastNote_ = note;
        }
    }

    Key keys_[kMaxKeys]{};
    uint32_t count_ = 0, physical_ = 0, order_ = 0;
    NoteQueue queue_;
    bool started_ = false, beatLocked_ = true;
    double anchor_ = 0.0, tickBeat_ = 0.0, stepBeats_ = -1.0;
    int64_t tick_ = 0;
    uint64_t pos_ = 0;
    uint32_t chainIdx_ = 0, chainSteps_ = 0;
    int walk_ = 0;
    int lastNote_ = -1;
};

} // namespace vektur::seq
