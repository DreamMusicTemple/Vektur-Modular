// Generative.hpp — Phase 5 generative composition engine.
//
// generatePhrase() writes a 1–8 bar phrase from the settings, a seed and a
// repeat number. It is theory-aware: a functional-harmony chord progression
// (tonic start, dominant turnaround back to the loop), chord tones on strong
// beats, stepwise scale motion with occasional leaps on weak beats, metric
// accent weighting, voice-led chord voicings and bass approach notes.
//
// Deterministic: the same settings, seed and repeat number always produce
// the same notes, which is what lets "Evolve" mutate a phrase gradually
// (each bar re-rolls with probability Evolve on each repeat, the progression
// at a third of that rate) and still be captured to MIDI offline exactly as
// it plays. Allocation-free and bounded, so the kernel can call it on the
// render thread at a phrase boundary.
#pragma once

#include "SeqCommon.hpp"
#include "../ParameterStore.hpp"

namespace vektur::seq {

struct GenSettings {
    int style = 0;            // 0 melody, 1 bassline, 2 chords, 3 ambient, 4 broken chords
    int scale = 2, root = 9;
    float density = 0.6f, complexity = 0.4f, evolve = 0.25f, humanise = 0.1f;
    int rangeOctaves = 2, registerNote = 60, bars = 4;
    double stepBeats = 0.25;
    double beatsPerBar = 4.0;   // from the host's time signature (Phase 7)
    uint32_t seed = 1;

    static constexpr uint64_t kBase = 38ull * VK_PARAMS_PER_MODULE;
    template <typename Get>   // Get: float(uint64_t address)
    static GenSettings from(Get get) noexcept {
        GenSettings g;
        auto i = [&](uint64_t k) { return static_cast<int>(std::lround(get(kBase + k))); };
        static constexpr int kBars[4] = {1, 2, 4, 8};
        g.style = std::clamp(i(1), 0, 4);
        g.scale = std::clamp(i(2), 0, 15); g.root = std::clamp(i(3), 0, 11);
        g.density = std::clamp(get(kBase + 4) * 0.01f, 0.f, 1.f);
        g.complexity = std::clamp(get(kBase + 5) * 0.01f, 0.f, 1.f);
        g.rangeOctaves = std::clamp(i(6), 0, 3) + 1;
        g.registerNote = std::clamp(i(7), 0, 127);
        g.bars = kBars[std::clamp(i(8), 0, 3)];
        g.stepBeats = divisionBeats(i(9));
        g.seed = static_cast<uint32_t>(std::clamp(i(10), 0, 65535));
        g.evolve = std::clamp(get(kBase + 11) * 0.01f, 0.f, 1.f);
        g.humanise = std::clamp(get(kBase + 12) * 0.01f, 0.f, 1.f);
        return g;
    }
};

struct GenNote { uint16_t tick, length; uint8_t note, velocity; };

struct Phrase {
    static constexpr uint32_t kMaxNotes = 512;
    static constexpr int kMaxBars = 8;
    GenNote notes[kMaxNotes]{};
    uint32_t count = 0;
    int bars = 0, ticksPerBar = 0;
    int chordRoot[kMaxBars]{};      // pitch class of each bar's chord
    int chordDegree[kMaxBars]{};    // scale degree (0-based) of each bar's chord
    bool chordSeventh[kMaxBars]{};  // the bar's chord carries its seventh
    int totalTicks() const noexcept { return bars * ticksPerBar; }
};

// Pure. Notes are sorted by tick. Guarantees at least one note when density > 0.
void generatePhrase(const GenSettings& s, uint32_t repeat, Phrase& out) noexcept;

// Variant of bar `bar` at repeat `repeat` under Evolve (0 = original).
uint32_t barVariant(const GenSettings& s, int bar, uint32_t repeat) noexcept;

// Beat (from phrase start) of grid tick `tick`, and the note-off beat of a
// note — shared by the player and the offline MIDI capture so they agree.
inline double genTickBeat(const GenSettings& s, int tick, float swingPct) noexcept { return swungTick(0.0, s.stepBeats, tick, swingPct); }
inline double genOffBeat(const GenSettings& s, const GenNote& n, float swingPct, double gate) noexcept {
    const double on = genTickBeat(s, n.tick, swingPct);
    return std::max(genTickBeat(s, n.tick + n.length - 1, swingPct) + s.stepBeats * gate, on + 0.01);
}

// ---------------------------------------------------------------------------
// GenerativePlayer — plays phrases on the shared clock inside the kernel.
// Phrase n occupies beats [n·L, (n+1)·L) of the clock (L = 4·bars), so it is
// locked to the host's bar grid; in Hold-to-Play mode the timeline restarts
// at the first held key.
// ---------------------------------------------------------------------------
class GenerativePlayer {
public:
    static constexpr uint64_t kBase = GenSettings::kBase;

    bool setActive(bool active, double beat, bool keyRestart) noexcept {
        if (active == active_) return false;
        active_ = active;
        if (active) restart(beat, keyRestart);
        return true;
    }
    bool active() const noexcept { return active_; }
    void restart(double beat, bool keyRestart) noexcept { anchor_ = keyRestart ? beat : 0.0; keyAnchored_ = keyRestart; phraseIndex_ = INT64_MIN; startBeat_ = beat; nextNoteBeat_ = beat; }

    double nextBeat() const noexcept {
        double t = queue_.nextBeat();
        if (active_) t = std::min(t, nextNoteBeat_);
        return t;
    }

    template <NoteSink S>
    void relocate(double beat, S& sink) noexcept { queue_.flush(sink); if (active_) restart(beat, false); }
    template <NoteSink S>
    void flush(S& sink) noexcept { queue_.flush(sink); }
    void clear() noexcept { queue_.clear(); active_ = false; }

    // beatsPerBar / barPhase: the meter and the beat position of a downbeat
    // (mod one bar), from the host; 4 and 0 otherwise.
    template <NoteSink S>
    void advance(double beat, const ParameterStore& p, int transpose, S& sink, double beatsPerBar = 4.0, double barPhase = 0.0) noexcept {
        queue_.emitUntil(beat + kEps, sink);
        if (!active_) return;
        for (int guard = 0; guard < 64; ++guard) {
            const double L = settings_.beatsPerBar * static_cast<double>(phrase_.bars > 0 ? phrase_.bars : 1);
            if (phraseIndex_ == INT64_MIN || beat + kEps >= phraseStart_ + L) {
                // New phrase: settings (and the meter) are read here, so edits land on the next phrase.
                settings_ = GenSettings::from([&](uint64_t a) { return p.get(a); });
                settings_.beatsPerBar = beatsPerBar;
                if (!keyAnchored_) anchor_ = barPhase - std::floor(barPhase / beatsPerBar) * beatsPerBar;
                const double Ln = settings_.beatsPerBar * settings_.bars;
                const int64_t idx = static_cast<int64_t>(std::floor((beat - anchor_ + kEps) / Ln));
                phraseIndex_ = idx;
                phraseStart_ = anchor_ + static_cast<double>(idx) * Ln;
                generatePhrase(settings_, static_cast<uint32_t>(std::max<int64_t>(idx, 0)), phrase_);
                cursor_ = 0;
                // Joining mid-phrase: skip notes that already started (except at a fresh start).
                const double from = beat - startBeat_ < 1e-6 ? beat - 0.125 * settings_.stepBeats : beat;
                while (cursor_ < phrase_.count && noteBeat(phrase_.notes[cursor_], p) < from - kEps) ++cursor_;
            }
            nextNoteBeat_ = cursor_ < phrase_.count ? std::max(noteBeat(phrase_.notes[cursor_], p), beat)
                                                     : phraseStart_ + settings_.beatsPerBar * phrase_.bars;
            if (nextNoteBeat_ > beat + kEps) break;
            if (cursor_ >= phrase_.count) continue;   // phrase boundary reached: generate the next one
            const GenNote& n = phrase_.notes[cursor_++];
            const double on = std::max(noteBeat(n, p), beat);
            const double off = phraseStart_ + genOffBeat(settings_, n, p.get(kBase + 14), p.get(kBase + 15) * 0.01);
            queue_.schedule(on, std::max(off, on + 0.01), std::clamp(static_cast<int>(n.note) + transpose, 0, 127), n.velocity, sink);
            queue_.emitUntil(beat + kEps, sink);
        }
    }

    int64_t phraseIndex() const noexcept { return phraseIndex_ == INT64_MIN ? -1 : phraseIndex_; }
    int currentBar(double beat) const noexcept {
        if (!active_ || phraseIndex_ == INT64_MIN) return -1;
        return std::clamp(static_cast<int>((beat - phraseStart_) / settings_.beatsPerBar), 0, std::max(phrase_.bars - 1, 0));
    }
    const Phrase& phrase() const noexcept { return phrase_; }

private:
    static constexpr double kEps = 1e-9;
    double noteBeat(const GenNote& n, const ParameterStore& p) const noexcept { return phraseStart_ + genTickBeat(settings_, n.tick, p.get(kBase + 14)); }

    NoteQueue queue_;
    Phrase phrase_;
    GenSettings settings_;
    bool active_ = false, keyAnchored_ = false;
    double anchor_ = 0.0, phraseStart_ = 0.0, nextNoteBeat_ = 1e300, startBeat_ = 0.0;
    int64_t phraseIndex_ = INT64_MIN;
    uint32_t cursor_ = 0;
};

} // namespace vektur::seq
