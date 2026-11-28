// PresetGenerator.cpp — Phase 5 generative preset creator (vk_preset_generate).
//
// Each category is a recipe of constrained ranges over the normalled voice
// (three VCOs, sub, noise, ring mod, filters A/B, two EGs, VCA, three LFOs,
// glide/voicing and up to four matrix slots), so results stay musical and
// recognisably "a bass", "a pad" … rather than random noise. Every candidate
// is auditioned offline in a private kernel (held C3 + C4). It must be
// finite and audible (loudest 50 ms above −40 dBFS); loudness is trimmed to peak
// around −3 dBFS. A failed candidate is re-rolled from a derived seed, and
// after eight failures a known-good voice for the category is returned, so
// the creator never yields silence. Deterministic: same seed + category →
// same sound. [control] thread only (allocates the audition kernel).
#include "VekturDSP.h"
#include "../Kernel.hpp"
#include "../ParamTable.hpp"
#include "SeqCommon.hpp"

#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

using vektur::seq::Rand;
using vektur::seq::hash;

constexpr uint64_t kMatrix = 60ull * VK_PARAMS_PER_MODULE;
constexpr uint64_t kSlotStride = 16;

// Parameters a sound preset owns. Performance settings (tune, polyphony,
// bend, oversampling, tempo, MPE, zones) and modules 36–38 are left alone.
bool presetOwns(uint64_t a) {
    const uint64_t m = a / VK_PARAMS_PER_MODULE;
    if (m == 36 || m == 37 || m == 38) return false;
    if (m == 0) {
        switch (a) {
            case VK_P_GLOBAL_MASTER_VOLUME: case VK_P_GLOBAL_ENGINE_MODE: case VK_P_GLOBAL_INSTABILITY:
            case VK_P_GLOBAL_GLIDE: case VK_P_GLOBAL_STEREO_SPREAD: case VK_P_GLOBAL_FILTER_ROUTING:
            case VK_P_GLOBAL_VOICE_MODE: case VK_P_GLOBAL_NOTE_PRIORITY: case VK_P_GLOBAL_GLIDE_MODE:
            case VK_P_GLOBAL_UNISON_VOICES: case VK_P_GLOBAL_UNISON_DETUNE: case VK_P_GLOBAL_GLIDE_CONSTANT_TIME:
                return true;
            default: return false;
        }
    }
    return true;
}

struct Draft {
    std::unordered_map<uint64_t, float> v;
    Rand r;
    int slots = 0;
    explicit Draft(uint64_t seed) : r(seed) {
        for (const auto& d : vektur::kParamTable) if (presetOwns(d.address)) v[d.address] = d.defaultValue;
    }
    void set(uint64_t a, float x) { v[a] = x; }
    float lin(float lo, float hi) { return lo + static_cast<float>(r.uniform()) * (hi - lo); }
    float lg(float lo, float hi) { return lo * std::pow(hi / lo, static_cast<float>(r.uniform())); }
    int pick(std::initializer_list<int> xs) { const int i = r.below(static_cast<int>(xs.size())); return *(xs.begin() + i); }
    bool chance(double p) { return r.chance(p); }
    void slot(int source, uint64_t dest, float amountPct, int via = 0) {
        if (slots >= 4) return;
        const uint64_t b = kMatrix + static_cast<uint64_t>(slots++) * kSlotStride;
        set(b + 0, 1.f); set(b + 1, static_cast<float>(source)); set(b + 2, static_cast<float>(via));
        set(b + 3, static_cast<float>(dest)); set(b + 4, amountPct);
    }
    void eg(int n, float a, float d, float s, float r_) {
        const uint64_t b = n == 1 ? VK_P_EG1_ATTACK : VK_P_EG2_ATTACK;
        set(b, a); set(b + 1, d); set(b + 2, s); set(b + 3, r_);
    }
};

enum Src { LFO1 = 1, LFO2 = 2, LFO3 = 3, EG1 = 4, VEL = 6, MODWHEEL = 8, AT = 9, RANDOM = 18, SPREAD = 19 };
enum Wave { SINE = 0, TRI = 1, SAW = 2, RSAW = 3, SQUARE = 4, PULSE = 5 };

void filterCommon(Draft& d, float cutLo, float cutHi, float resLo, float resHi, float envLo, float envHi) {
    const int routing = d.pick({0, 1, 1, 1, 2});
    d.set(VK_P_GLOBAL_FILTER_ROUTING, static_cast<float>(routing));
    const float cut = d.lg(cutLo, cutHi), res = d.lin(resLo, resHi), env = d.lin(envLo, envHi);
    d.set(VK_P_VCFA_MODE, 0.f);
    d.set(VK_P_VCFA_CUTOFF, routing == 2 ? std::min(cut * 3.f, 18000.f) : cut); d.set(VK_P_VCFA_RESONANCE, res * 0.8f); d.set(VK_P_VCFA_ENV_AMOUNT, env);
    d.set(VK_P_VCFB_CUTOFF, cut); d.set(VK_P_VCFB_RESONANCE, res); d.set(VK_P_VCFB_ENV_AMOUNT, env);
    d.set(VK_P_VCFB_DRIVE, d.lin(0.f, 6.f));
    d.set(VK_P_VCFA_KEY_TRACK, d.lin(30.f, 80.f)); d.set(VK_P_VCFB_KEY_TRACK, d.lin(30.f, 80.f));
}

void oscs(Draft& d, std::initializer_list<int> w1, std::initializer_list<int> w2, float l2lo, float l2hi, float detune) {
    d.set(VK_P_VCO1_WAVE, static_cast<float>(d.pick(w1)));
    d.set(VK_P_VCO1_PULSE_WIDTH, d.lin(20.f, 50.f));
    d.set(VK_P_VCO2_WAVE, static_cast<float>(d.pick(w2)));
    d.set(VK_P_VCO2_LEVEL, d.lin(l2lo, l2hi));
    d.set(VK_P_VCO2_FINE, d.lin(-detune, detune));
    d.set(VK_P_VCO2_PULSE_WIDTH, d.lin(20.f, 50.f));
}

void recipe(Draft& d, int cat) {
    d.set(VK_P_GLOBAL_MASTER_VOLUME, -6.f);
    d.set(VK_P_GLOBAL_INSTABILITY, d.lin(15.f, 50.f));
    d.set(VK_P_GLOBAL_STEREO_SPREAD, d.lin(20.f, 70.f));
    d.set(VK_P_VCA_VELOCITY, d.lin(20.f, 60.f));
    switch (cat) {
    case VK_PRESET_BASS:
        d.set(VK_P_GLOBAL_VOICE_MODE, static_cast<float>(d.pick({0, 1, 2, 2})));
        d.set(VK_P_GLOBAL_GLIDE, d.chance(0.4) ? d.lin(10.f, 90.f) : 0.f);
        oscs(d, {SAW, SQUARE, PULSE}, {SAW, SQUARE, TRI}, 30.f, 90.f, 10.f);
        d.set(VK_P_VCO1_OCTAVE, static_cast<float>(d.pick({2, 3})));
        d.set(VK_P_VCO2_OCTAVE, static_cast<float>(d.pick({2, 3})));
        d.set(VK_P_VCO1_SUB_LEVEL, d.lin(20.f, 80.f));
        filterCommon(d, 140.f, 900.f, 10.f, 55.f, 2.f, 5.f);
        d.eg(1, d.lg(0.5f, 6.f), d.lg(80.f, 500.f), d.lin(0.f, 30.f), d.lg(40.f, 220.f));
        d.eg(2, d.lg(0.5f, 3.f), d.lg(200.f, 900.f), d.lin(60.f, 100.f), d.lg(40.f, 200.f));
        if (d.chance(0.4)) d.slot(VEL, VK_P_VCFB_CUTOFF, d.lin(10.f, 30.f));
        break;
    case VK_PRESET_LEAD:
        d.set(VK_P_GLOBAL_VOICE_MODE, static_cast<float>(d.pick({1, 2, 2, 3, 0})));
        d.set(VK_P_GLOBAL_UNISON_VOICES, static_cast<float>(d.pick({0, 1, 2})));
        d.set(VK_P_GLOBAL_UNISON_DETUNE, d.lin(5.f, 20.f));
        d.set(VK_P_GLOBAL_GLIDE, d.lin(15.f, 150.f));
        oscs(d, {SAW, PULSE, SQUARE}, {SAW, PULSE}, 50.f, 100.f, 14.f);
        if (d.chance(0.3)) { d.set(VK_P_VCO2_SYNC_MODE, 1.f); d.set(VK_P_VCO2_COARSE, d.lin(4.f, 12.f)); }
        filterCommon(d, 700.f, 4000.f, 15.f, 60.f, 1.f, 4.f);
        d.eg(1, d.lg(0.5f, 20.f), d.lg(200.f, 900.f), d.lin(30.f, 70.f), d.lg(100.f, 500.f));
        d.eg(2, d.lg(1.f, 20.f), d.lg(300.f, 900.f), d.lin(80.f, 100.f), d.lg(100.f, 450.f));
        d.set(VK_P_LFO1_SHAPE, 1.f); d.set(VK_P_LFO1_RATE, d.lin(4.5f, 6.5f)); d.set(VK_P_LFO1_DELAY, d.lin(0.f, 400.f));
        d.slot(LFO1, VK_MOD_DEST_PITCH, d.lin(2.f, 5.f), MODWHEEL);
        if (d.chance(0.5)) d.slot(AT, VK_MOD_DEST_CUTOFFALL, d.lin(10.f, 25.f));
        break;
    case VK_PRESET_PAD:
        d.set(VK_P_GLOBAL_STEREO_SPREAD, d.lin(60.f, 100.f));
        d.set(VK_P_GLOBAL_INSTABILITY, d.lin(30.f, 70.f));
        oscs(d, {SAW, PULSE, TRI}, {SAW, PULSE}, 60.f, 100.f, 20.f);
        if (d.chance(0.6)) { d.set(VK_P_VCO3_WAVE, static_cast<float>(d.pick({SAW, TRI, SINE}))); d.set(VK_P_VCO3_OCTAVE, static_cast<float>(d.pick({3, 4})));
                             d.set(VK_P_VCO3_LEVEL, d.lin(25.f, 60.f)); d.set(VK_P_VCO3_FINE, d.lin(-12.f, 12.f)); }
        filterCommon(d, 500.f, 3000.f, 0.f, 40.f, 0.3f, 2.f);
        d.eg(1, d.lg(200.f, 2500.f), d.lg(1000.f, 4000.f), d.lin(40.f, 80.f), d.lg(800.f, 4000.f));
        d.eg(2, d.lg(150.f, 2000.f), d.lg(800.f, 3000.f), d.lin(70.f, 100.f), d.lg(1000.f, 5000.f));
        d.set(VK_P_LFO2_SHAPE, static_cast<float>(d.pick({0, 1, 6}))); d.set(VK_P_LFO2_RATE, d.lg(0.08f, 0.6f));
        d.slot(LFO2, VK_MOD_DEST_CUTOFFALL, d.lin(8.f, 22.f));
        d.set(VK_P_LFO3_SHAPE, 0.f); d.set(VK_P_LFO3_RATE, d.lg(0.05f, 0.3f));
        d.slot(LFO3, VK_P_VCO1_PULSE_WIDTH, d.lin(10.f, 30.f));
        break;
    case VK_PRESET_PLUCK:
        oscs(d, {SAW, PULSE, SQUARE}, {SAW, SQUARE, TRI}, 0.f, 60.f, 8.f);
        filterCommon(d, 250.f, 1500.f, 10.f, 50.f, 3.f, 6.f);
        d.eg(1, 0.5f, d.lg(90.f, 400.f), d.lin(0.f, 10.f), d.lg(90.f, 400.f));
        d.eg(2, 0.5f, d.lg(250.f, 1000.f), d.lin(0.f, 15.f), d.lg(150.f, 600.f));
        d.set(VK_P_EG1_VELOCITY, d.lin(20.f, 60.f));
        d.slot(VEL, VK_MOD_DEST_CUTOFFALL, d.lin(10.f, 30.f));
        break;
    case VK_PRESET_KEYS:
        oscs(d, {PULSE, SQUARE, TRI}, {SINE, TRI}, 20.f, 55.f, 4.f);
        d.set(VK_P_VCO2_OCTAVE, static_cast<float>(d.pick({4, 5})));
        filterCommon(d, 900.f, 5000.f, 0.f, 30.f, 1.f, 3.f);
        d.eg(1, d.lg(0.5f, 5.f), d.lg(300.f, 1200.f), d.lin(20.f, 50.f), d.lg(150.f, 600.f));
        d.eg(2, d.lg(0.5f, 5.f), d.lg(800.f, 2500.f), d.lin(30.f, 70.f), d.lg(200.f, 600.f));
        if (d.chance(0.5)) { d.set(VK_P_LFO1_SHAPE, 0.f); d.set(VK_P_LFO1_RATE, d.lin(3.f, 7.f)); d.slot(LFO1, VK_MOD_DEST_AMP, d.lin(5.f, 15.f), MODWHEEL); }
        break;
    case VK_PRESET_BRASS:
        d.set(VK_P_GLOBAL_VOICE_MODE, static_cast<float>(d.pick({0, 0, 1})));
        oscs(d, {SAW}, {SAW, PULSE}, 60.f, 100.f, 12.f);
        filterCommon(d, 280.f, 900.f, 5.f, 35.f, 2.f, 4.f);
        d.eg(1, d.lg(30.f, 120.f), d.lg(300.f, 800.f), d.lin(50.f, 75.f), d.lg(150.f, 400.f));
        d.eg(2, d.lg(20.f, 80.f), d.lg(300.f, 800.f), d.lin(80.f, 100.f), d.lg(150.f, 400.f));
        d.set(VK_P_LFO1_SHAPE, 1.f); d.set(VK_P_LFO1_RATE, d.lin(4.f, 6.f)); d.set(VK_P_LFO1_DELAY, d.lin(200.f, 700.f));
        d.slot(LFO1, VK_MOD_DEST_PITCH, d.lin(1.f, 3.f));
        break;
    case VK_PRESET_FX:
        oscs(d, {SAW, SQUARE, PULSE, SINE}, {SAW, SQUARE, SINE}, 30.f, 100.f, 50.f);
        d.set(VK_P_VCO2_COARSE, std::round(d.lin(-12.f, 12.f)));
        d.set(VK_P_RINGMOD_LEVEL, d.chance(0.6) ? d.lin(20.f, 80.f) : 0.f);
        d.set(VK_P_NOISE_LEVEL, d.chance(0.5) ? d.lin(5.f, 40.f) : 0.f);
        d.set(VK_P_NOISE_TYPE, static_cast<float>(d.pick({0, 1})));
        filterCommon(d, 300.f, 6000.f, 30.f, 85.f, -4.f, 6.f);
        d.set(VK_P_VCFB_SH_AMOUNT, d.chance(0.5) ? d.lin(-2.f, 2.f) : 0.f);
        d.set(VK_P_SAMPLEHOLD_RATE, d.lg(1.f, 16.f));
        d.set(VK_P_EG1_PITCH_AMOUNT, d.chance(0.4) ? std::round(d.lin(-12.f, 12.f)) : 0.f);
        d.eg(1, d.lg(0.5f, 500.f), d.lg(100.f, 2000.f), d.lin(0.f, 60.f), d.lg(100.f, 2000.f));
        d.set(VK_P_EG1_LOOP, d.chance(0.3) ? 1.f : 0.f);
        d.eg(2, d.lg(0.5f, 300.f), d.lg(200.f, 2000.f), d.lin(50.f, 100.f), d.lg(200.f, 2000.f));
        d.set(VK_P_LFO1_SHAPE, static_cast<float>(d.pick({0, 2, 4, 5, 6}))); d.set(VK_P_LFO1_RATE, d.lg(0.5f, 20.f));
        d.slot(LFO1, d.chance(0.5) ? VK_MOD_DEST_PITCH : VK_MOD_DEST_CUTOFFALL, d.lin(-40.f, 40.f));
        if (d.chance(0.5)) d.slot(RANDOM, VK_MOD_DEST_PITCH, d.lin(-20.f, 20.f));
        break;
    default:   // VK_PRESET_DRONE
        d.set(VK_P_GLOBAL_INSTABILITY, d.lin(40.f, 80.f));
        d.set(VK_P_GLOBAL_STEREO_SPREAD, d.lin(70.f, 100.f));
        oscs(d, {SAW, PULSE}, {SAW, PULSE}, 60.f, 100.f, 18.f);
        d.set(VK_P_VCO3_WAVE, static_cast<float>(SAW)); d.set(VK_P_VCO3_LEVEL, d.lin(40.f, 80.f));
        d.set(VK_P_VCO3_OCTAVE, static_cast<float>(d.pick({2, 3}))); d.set(VK_P_VCO3_COARSE, static_cast<float>(d.pick({0, 7, -5})));
        d.set(VK_P_NOISE_LEVEL, d.lin(0.f, 12.f));
        filterCommon(d, 300.f, 2000.f, 10.f, 55.f, 0.f, 1.f);
        d.eg(1, d.lg(500.f, 5000.f), d.lg(2000.f, 8000.f), d.lin(50.f, 100.f), d.lg(2000.f, 8000.f));
        d.eg(2, d.lg(200.f, 1500.f), 2000.f, 100.f, d.lg(2000.f, 8000.f));
        d.set(VK_P_LFO2_SHAPE, 6.f); d.set(VK_P_LFO2_RATE, d.lg(0.05f, 0.4f));
        d.slot(LFO2, VK_MOD_DEST_CUTOFFALL, d.lin(10.f, 30.f));
        d.set(VK_P_LFO3_SHAPE, 0.f); d.set(VK_P_LFO3_RATE, d.lg(0.03f, 0.2f));
        d.slot(LFO3, VK_MOD_DEST_PAN, d.lin(30.f, 70.f));
        break;
    }
    d.set(VK_P_GLOBAL_ENGINE_MODE, d.chance(0.75) ? 0.f : 1.f);
}

// The known-good voice per category (used only if every candidate failed).
void safeVoice(Draft& d, int cat) {
    d.set(VK_P_GLOBAL_MASTER_VOLUME, -6.f);
    d.set(VK_P_GLOBAL_FILTER_ROUTING, 1.f);
    d.set(VK_P_VCO1_WAVE, SAW); d.set(VK_P_VCO1_LEVEL, 100.f);
    d.set(VK_P_VCFB_CUTOFF, cat == VK_PRESET_BASS ? 600.f : 2000.f); d.set(VK_P_VCFB_RESONANCE, 20.f);
    d.eg(2, 3.f, 300.f, 100.f, 250.f);
}

struct Audition { double rmsDb; float peak; bool finite; };

Audition audition(const Draft& d) {
    constexpr double sr = 48000.0;
    constexpr uint32_t blk = 256;
    auto k = std::make_unique<vektur::Kernel>();
    if (k->allocate(sr, 2, blk) != 0) return {-200.0, 0.f, false};
    for (const auto& [a, x] : d.v) k->setParam(a, x);
    // Deterministic audition: no host transport, no generators.
    std::vector<float> L(blk), R(blk);
    float* ch[2] = {L.data(), R.data()};
    VKEvent on[2]{};
    for (int i = 0; i < 2; ++i) { on[i].type = VK_EVENT_MIDI1; on[i].length = 3; on[i].data[0] = 0x90; on[i].data[1] = static_cast<uint8_t>(48 + 12 * i); on[i].data[2] = 100; }
    // Loudest 50 ms window over the hold: plucks are judged on their attack,
    // pads on their swell, so a decaying or slow sound is not mistaken for silence.
    double win = 0.0, best = 0.0; size_t inWin = 0; float peak = 0.f; bool finite = true;
    const size_t winLen = static_cast<size_t>(0.05 * sr);
    const int blocks = static_cast<int>(0.9 * sr / blk);
    for (int b = 0; b < blocks; ++b) {
        k->render(ch, 2, blk, b == 0 ? on : nullptr, b == 0 ? 2 : 0);
        for (uint32_t i = 0; i < blk; ++i) {
            const float s = 0.5f * (L[i] + R[i]);
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) finite = false;
            peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
            win += static_cast<double>(s) * s;
            if (++inWin == winLen) { best = std::max(best, win / static_cast<double>(winLen)); win = 0.0; inWin = 0; }
        }
    }
    VKTelemetry t;
    while (k->popTelemetry(t)) if (t.flags & VK_TELEMETRY_FLAG_NONFINITE) finite = false;
    const double rms = std::sqrt(best);
    return {rms > 0.0 ? 20.0 * std::log10(rms) : -200.0, peak, finite};
}

} // namespace

extern "C" uint32_t vk_preset_generate(uint32_t seed, uint32_t category, VKParamValue* out, uint32_t capacity, VKPresetReport* report) {
    const int cat = static_cast<int>(std::min<uint32_t>(category, VK_PRESET_CATEGORY_COUNT - 1));
    std::unique_ptr<Draft> chosen;
    Audition res{};
    uint32_t attempts = 0;
    bool repaired = false;
    for (uint32_t attempt = 0; attempt < 8 && !chosen; ++attempt) {
        auto d = std::make_unique<Draft>(hash(seed, 0x9E5E7, static_cast<uint64_t>(cat), attempt));
        recipe(*d, cat);
        ++attempts;
        Audition a = audition(*d);
        if (!a.finite || a.rmsDb < -40.0) continue;
        if (a.peak > 0.708f) {   // trim to about −3 dBFS peak, then confirm
            const float trim = 20.f * std::log10(0.708f / a.peak);
            d->set(VK_P_GLOBAL_MASTER_VOLUME, std::max(-60.f, d->v[VK_P_GLOBAL_MASTER_VOLUME] + trim));
            a = audition(*d);
            if (!a.finite || a.rmsDb < -40.0 || a.peak > 1.0f) continue;
        }
        res = a; chosen = std::move(d);
    }
    if (!chosen) {
        chosen = std::make_unique<Draft>(hash(seed, 0x5AFE, static_cast<uint64_t>(cat)));
        safeVoice(*chosen, cat);
        res = audition(*chosen);
        repaired = true;
    }
    if (report) *report = {static_cast<float>(res.rmsDb), res.peak, attempts, repaired};
    // Emit in table order (stable output for tests and state diffs).
    uint32_t n = 0;
    for (const auto& def : vektur::kParamTable) {
        if (!presetOwns(def.address)) continue;
        if (out && n < capacity) out[n] = {def.address, std::clamp(chosen->v[def.address], def.minValue, def.maxValue)};
        ++n;
    }
    return out ? std::min(n, capacity) : n;
}
