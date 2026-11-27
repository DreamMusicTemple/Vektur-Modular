// StepSequencer.hpp — Phase 5 analogue-style step sequencer.
//
// Modelled on the large-format analogue sequencers of the era: 16 steps, three
// rows of step voltages (A, B, C) and a gate mode per step (Off / On / Tie /
// Skip). Row A plays notes (range 1–5 octaves, optionally quantised to a
// scale); all three rows are also global modulation sources ("Seq Row A/B/C")
// so the sequencer can drive any destination in the matrix, with or without
// playing notes. Per step: probability and 1–4× ratchets. Directions:
// forward, reverse, ping-pong, random, random walk. Swing and gate length are
// global. Skipped steps take no time, as on the hardware.
//
// Run modes: Run (always, on the shared clock), Transport (only while the
// host is playing), Key (while keys are held; restarts from step 1 and
// transposes by the last key relative to C4). Real-time safe.
#pragma once

#include "SeqCommon.hpp"
#include "../ParameterStore.hpp"

namespace vektur::seq {

class StepSequencer {
public:
    static constexpr uint64_t kBase = 37ull * VK_PARAMS_PER_MODULE;
    static constexpr int kSteps = 16;
    enum Run : int { Off = 0, Always = 1, Transport = 2, Key = 3 };
    enum Gate : int { GateOff = 0, GateOn = 1, GateTie = 2, GateSkip = 3 };

    static uint64_t stepAddr(int step, int field) noexcept { return kBase + 32 + static_cast<uint64_t>(step) * 8 + static_cast<uint64_t>(field); }

    // Called by the kernel once per span with the run condition already decided.
    // Returns true when the active state changed (the kernel flushes then).
    // songLocked: step 1 falls on the song's grid origin (host transport);
    // otherwise the pattern starts from step 1 at the next grid line.
    bool setActive(bool active, double beat, bool keyRestart, bool songLocked) noexcept {
        if (active == active_) return false;
        active_ = active;
        if (active) { restart(beat, keyRestart); songLocked_ = songLocked; }
        return true;
    }
    bool active() const noexcept { return active_; }

    void restart(double beat, bool keyRestart) noexcept {
        keyRestart_ = keyRestart;
        stepBeats_ = -1.0;
        anchor_ = keyRestart ? beat : 0.0;
        tick_ = 0; tickBase_ = 0; tickBeat_ = beat; walk_ = 0; lastNote_ = -1; songLocked_ = true;
    }

    double nextBeat() const noexcept { return std::min(active_ ? tickBeat_ : 1e300, queue_.nextBeat()); }

    template <NoteSink S>
    void relocate(double beat, S& sink) noexcept {
        queue_.flush(sink);
        if (active_) restart(beat, false);
    }
    template <NoteSink S>
    void flush(S& sink) noexcept { queue_.flush(sink); }
    void clear() noexcept { queue_.clear(); active_ = false; step_ = -1; }

    template <NoteSink S>
    void advance(double beat, const ParameterStore& p, int keyTranspose, S& sink) noexcept {
        queue_.emitUntil(beat + kEps, sink);
        if (!active_) return;
        const double stepBeats = divisionBeats(std::lround(p.get(kBase + 3)));
        if (stepBeats != stepBeats_) {
            const double slack = kEps;   // Run / Transport stay exactly on the grid
            if (!keyRestart_) {
                anchor_ = 0.0; tick_ = static_cast<int64_t>(std::ceil(beat / stepBeats - slack));
                if (stepBeats_ < 0.0) tickBase_ = songLocked_ ? 0 : tick_;
            }
            else if (stepBeats_ > 0.0) anchor_ = tickBeat_ - static_cast<double>(tick_) * stepBeats;
            stepBeats_ = stepBeats;
            tickBeat_ = std::max(beat, swungTick(anchor_, stepBeats_, tick_, p.get(kBase + 4)));
        }
        while (tickBeat_ <= beat + kEps) {
            playTick(p, keyTranspose, sink);
            ++tick_;
            tickBeat_ = swungTick(anchor_, stepBeats_, tick_, p.get(kBase + 4));
            queue_.emitUntil(beat + kEps, sink);
        }
    }

    // Current step (0…15, −1 before the first tick) and its row values (0…1).
    int step() const noexcept { return step_; }
    float row(int r) const noexcept { return rows_[std::clamp(r, 0, 2)]; }
    // Pitch offset of the current step in semitones (Row A, quantised, plus
    // transpose) — what chains into the arpeggiator.
    int transposeOut() const noexcept { return transposeOut_; }

    // Active (non-skipped) steps within the length, in order. Public for tests.
    int activeSteps(const ParameterStore& p, int* out) const noexcept {
        const int len = std::clamp(static_cast<int>(std::lround(p.get(kBase + 1))) + 1, 1, kSteps);
        int n = 0;
        for (int s = 0; s < len; ++s) if (std::lround(p.get(stepAddr(s, 3))) != GateSkip) out[n++] = s;
        return n;
    }

    // Index into the active list for grid tick t. Public for tests.
    // Pure for every direction except random walk (which returns −1).
    static int peekIndex(int64_t t, int n, int direction, uint64_t seed) noexcept {
        if (n <= 1) return 0;
        switch (direction) {
            case 1: return n - 1 - static_cast<int>(t % n);
            case 2: { const int64_t per = 2 * n - 2; const int ph = static_cast<int>(t % per); return ph < n ? ph : static_cast<int>(per) - ph; }
            case 3: return static_cast<int>(hash(seed, 0x5E1, static_cast<uint64_t>(t)) % static_cast<uint64_t>(n));
            case 4: return -1;
            default: return static_cast<int>(t % n);
        }
    }
    int indexFor(int64_t t, int n, int direction, uint64_t seed) noexcept {
        if (n <= 1) return 0;
        switch (direction) {
            case 4: {
                if (t > 0) walk_ += rand01(seed, 0x5E2, static_cast<uint64_t>(t)) < 0.5 ? -1 : 1;
                if (walk_ < 0) walk_ = 1;
                if (walk_ >= n) walk_ = n - 2;
                return walk_;
            }
            default: return peekIndex(t, n, direction, seed);
        }
    }

    static int pitchSemis(float rowA01, long rangeIndex) noexcept {
        static constexpr int kRange[4] = {12, 24, 36, 60};
        return static_cast<int>(std::lround(std::clamp(rowA01, 0.f, 1.f) * static_cast<float>(kRange[std::clamp(rangeIndex, 0L, 3L)])));
    }

private:
    static constexpr double kEps = 1e-9;

    template <NoteSink S>
    void playTick(const ParameterStore& p, int keyTranspose, S& sink) noexcept {
        int act[kSteps];
        const int n = activeSteps(p, act);
        if (n == 0) { step_ = -1; return; }
        const uint64_t seed = static_cast<uint64_t>(std::lround(p.get(kBase + 12)));
        const int64_t t = tick_ - tickBase_;
        const int idx = indexFor(t, n, static_cast<int>(std::lround(p.get(kBase + 2))), seed);
        const int s = act[std::clamp(idx, 0, n - 1)];
        step_ = s;
        for (int r = 0; r < 3; ++r) rows_[r] = p.get(stepAddr(s, r)) * 0.01f;

        const int root = static_cast<int>(std::lround(p.get(kBase + 8)));
        const Scale scale = makeScale(std::lround(p.get(kBase + 7)));
        const int base = 48 + root;
        const int raw = base + pitchSemis(rows_[0], std::lround(p.get(kBase + 6)));
        const int quant = quantise(raw, root, scale);
        const int transpose = static_cast<int>(std::lround(p.get(kBase + 9)));
        transposeOut_ = quant - base + transpose;
        const int note = std::clamp(quant + transpose + keyTranspose, 0, 127);

        const double start = tickBeat_;
        const double end = std::max(swungTick(anchor_, stepBeats_, tick_ + 1, p.get(kBase + 4)), start + 1e-4);
        const double stepLen = end - start;
        const double gate = p.get(kBase + 5) * 0.01;
        const int mode = static_cast<int>(std::lround(p.get(stepAddr(s, 3))));
        if (p.get(kBase + 10) < 0.5f) { lastNote_ = -1; return; }   // rows drive the matrix only

        // A note followed by a Tie step is held to the tie (and through a run of ties).
        const int dir = static_cast<int>(std::lround(p.get(kBase + 2)));
        const int nextIdx = peekIndex(t + 1, n, dir, seed);
        const bool tiedOn = nextIdx >= 0 && n > 1 && std::lround(p.get(stepAddr(act[nextIdx], 3))) == GateTie;
        const double hold = end + 0.01;   // replaced by the tie step's own end
        if (mode == GateTie) {
            if (lastNote_ >= 0 && queue_.sounding(lastNote_)) queue_.extend(lastNote_, tiedOn ? hold : start + stepLen * gate);
            return;
        }
        if (mode == GateOff) { lastNote_ = -1; return; }
        const double prob = p.get(stepAddr(s, 4)) * 0.01;
        if (prob < 1.0 && rand01(seed, 0x5E3, static_cast<uint64_t>(t)) >= prob) { lastNote_ = -1; return; }
        const int vel = p.get(kBase + 11) >= 0.5f ? 1 + static_cast<int>(std::lround(rows_[1] * 126.f)) : 100;
        const int r = static_cast<int>(std::lround(p.get(stepAddr(s, 5)))) + 1;
        const double sub = stepLen / r;
        const double len = r > 1 ? sub * std::min(gate, 0.95) : stepLen * gate;
        for (int h = 0; h < r; ++h) {
            const double on = start + h * sub;
            queue_.schedule(on, (tiedOn && h == r - 1) ? hold : on + len, note, vel, sink);
        }
        lastNote_ = note;
    }

    NoteQueue queue_;
    bool active_ = false, keyRestart_ = false, songLocked_ = true;
    int64_t tickBase_ = 0;
    double anchor_ = 0.0, tickBeat_ = 0.0, stepBeats_ = -1.0;
    int64_t tick_ = 0;
    int walk_ = 0, step_ = -1, lastNote_ = -1, transposeOut_ = 0;
    float rows_[3]{0.f, 0.5f, 0.5f};
};

} // namespace vektur::seq
