// SeqCAPI.cpp — Phase 5 C entry points: capture, status, offline generative
// phrases, Standard MIDI File writer.
#include "VekturDSP.h"
#include "../Kernel.hpp"
#include "../ChordAnalyzer.hpp"
#include "Generative.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

using vektur::Kernel;
namespace sq = vektur::seq;

namespace {
sq::GenSettings settingsOf(const Kernel& k) {
    sq::GenSettings s = sq::GenSettings::from([&](uint64_t a) { return k.getParam(a); });
    s.beatsPerBar = k.beatsPerBar();
    return s;
}
}

extern "C" {

bool vk_kernel_pop_note_record(VKKernel* k, VKNoteRecord* out) {
    return k && out && reinterpret_cast<Kernel*>(k)->popNoteRecord(*out);
}

void vk_kernel_sequencer_status(const VKKernel* k, VKSequencerStatus* out) {
    if (!out) return;
    *out = VKSequencerStatus{-1, -1, -1, -1, 0, {0.f, 0.5f, 0.5f}, 0.0};
    if (k) reinterpret_cast<const Kernel*>(k)->sequencerStatus(*out);
}

uint32_t vk_kernel_generative_phrase(const VKKernel* kk, uint32_t repeat, VKNoteRecord* out, uint32_t capacity, double* phraseBeats) {
    if (!kk) return 0;
    const Kernel& k = *reinterpret_cast<const Kernel*>(kk);
    const sq::GenSettings s = settingsOf(k);
    auto phrase = std::make_unique<sq::Phrase>();
    sq::generatePhrase(s, repeat, *phrase);
    if (phraseBeats) *phraseBeats = s.beatsPerBar * phrase->bars;
    const float swing = k.getParam(sq::GenSettings::kBase + 14);
    const double gate = k.getParam(sq::GenSettings::kBase + 15) * 0.01;
    std::vector<VKNoteRecord> recs;
    recs.reserve(phrase->count * 2);
    for (uint32_t i = 0; i < phrase->count; ++i) {
        const sq::GenNote& n = phrase->notes[i];
        recs.push_back({sq::genTickBeat(s, n.tick, swing), n.note, n.velocity, VK_SOURCE_GEN, 0});
        recs.push_back({sq::genOffBeat(s, n, swing, gate), n.note, 0, VK_SOURCE_GEN, 0});
    }
    std::stable_sort(recs.begin(), recs.end(), [](const VKNoteRecord& a, const VKNoteRecord& b) {
        return a.beat < b.beat || (a.beat == b.beat && a.velocity == 0 && b.velocity != 0);
    });
    const uint32_t n = std::min<uint32_t>(capacity, static_cast<uint32_t>(recs.size()));
    if (out) std::memcpy(out, recs.data(), n * sizeof(VKNoteRecord));
    return out ? n : static_cast<uint32_t>(recs.size());
}

uint32_t vk_kernel_generative_chords(const VKKernel* kk, uint32_t repeat, char names[8][12]) {
    if (!kk || !names) return 0;
    const Kernel& k = *reinterpret_cast<const Kernel*>(kk);
    const sq::GenSettings s = settingsOf(k);
    auto phrase = std::make_unique<sq::Phrase>();
    sq::generatePhrase(s, repeat, *phrase);
    const sq::Scale sc = sq::makeScale(s.scale == 0 ? 1 : s.scale);
    for (int b = 0; b < 8; ++b) names[b][0] = 0;
    for (int b = 0; b < phrase->bars && b < 8; ++b) {
        uint8_t notes[4]; uint32_t c = phrase->chordSeventh[b] ? 4u : 3u;
        for (uint32_t t = 0; t < c; ++t) notes[t] = static_cast<uint8_t>(48 + sq::degreePitch(s.root, sc, phrase->chordDegree[b] + 2 * static_cast<int>(t)));
        vektur::chordName(notes, c, names[b], 12);
    }
    return static_cast<uint32_t>(phrase->bars);
}

uint32_t vk_midi_file_write(const VKNoteRecord* records, uint32_t count, double tempoBPM, uint8_t* out, uint32_t capacity) {
    constexpr uint32_t ppq = 480;
    std::vector<VKNoteRecord> ev(records ? records : nullptr, records ? records + count : nullptr);
    std::stable_sort(ev.begin(), ev.end(), [](const VKNoteRecord& a, const VKNoteRecord& b) {
        return a.beat < b.beat || (a.beat == b.beat && a.velocity == 0 && b.velocity != 0);
    });
    // Pair note-ons with note-offs; drop orphan offs, close open notes at the end.
    int open[128]{};
    std::vector<VKNoteRecord> clean;
    clean.reserve(ev.size() + 128);
    double last = 0.0;
    for (const auto& r : ev) {
        if (r.note > 127 || !(r.beat == r.beat)) continue;
        VKNoteRecord c = r; c.beat = std::max(0.0, r.beat);
        if (c.velocity > 0) ++open[c.note];
        else { if (open[c.note] == 0) continue; --open[c.note]; }
        last = std::max(last, c.beat);
        clean.push_back(c);
    }
    for (int n = 0; n < 128; ++n) while (open[n]-- > 0) clean.push_back({last, static_cast<uint8_t>(n), 0, 0, 0});

    std::vector<uint8_t> trk;
    auto vlq = [&](uint32_t v) {
        uint8_t buf[5]; int i = 0;
        buf[i++] = v & 0x7F;
        while ((v >>= 7)) buf[i++] = static_cast<uint8_t>(0x80 | (v & 0x7F));
        while (i) trk.push_back(buf[--i]);
    };
    const double bpm = tempoBPM > 0.0 && tempoBPM < 1000.0 ? tempoBPM : 120.0;
    const uint32_t usPerQuarter = static_cast<uint32_t>(std::lround(60'000'000.0 / bpm));
    vlq(0); trk.insert(trk.end(), {0xFF, 0x51, 0x03, static_cast<uint8_t>(usPerQuarter >> 16), static_cast<uint8_t>(usPerQuarter >> 8), static_cast<uint8_t>(usPerQuarter)});
    vlq(0); trk.insert(trk.end(), {0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08});
    const char* name = "Vektur Modular";
    vlq(0); trk.insert(trk.end(), {0xFF, 0x03, static_cast<uint8_t>(std::strlen(name))});
    trk.insert(trk.end(), name, name + std::strlen(name));
    uint64_t prev = 0;
    for (const auto& r : clean) {
        const uint64_t t = static_cast<uint64_t>(std::llround(r.beat * ppq));
        vlq(static_cast<uint32_t>(std::min<uint64_t>(t - std::min(t, prev), 0x0FFFFFFF)));
        prev = std::max(prev, t);
        if (r.velocity > 0) trk.insert(trk.end(), {0x90, r.note, r.velocity});
        else trk.insert(trk.end(), {0x80, r.note, 0x40});
    }
    vlq(0); trk.insert(trk.end(), {0xFF, 0x2F, 0x00});

    const uint32_t size = 14 + 8 + static_cast<uint32_t>(trk.size());
    if (!out || capacity < size) return size;
    uint8_t* p = out;
    auto be32 = [&](uint32_t v) { *p++ = static_cast<uint8_t>(v >> 24); *p++ = static_cast<uint8_t>(v >> 16); *p++ = static_cast<uint8_t>(v >> 8); *p++ = static_cast<uint8_t>(v); };
    auto be16 = [&](uint16_t v) { *p++ = static_cast<uint8_t>(v >> 8); *p++ = static_cast<uint8_t>(v); };
    std::memcpy(p, "MThd", 4); p += 4; be32(6); be16(0); be16(1); be16(static_cast<uint16_t>(ppq));
    std::memcpy(p, "MTrk", 4); p += 4; be32(static_cast<uint32_t>(trk.size()));
    std::memcpy(p, trk.data(), trk.size());
    return size;
}

} // extern "C"
