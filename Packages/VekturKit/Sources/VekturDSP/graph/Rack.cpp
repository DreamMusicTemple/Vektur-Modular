// Rack.cpp — rack contents and normalled connections. See Rack.hpp.
#include "Rack.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace vektur {

int Rack::inPort(uint32_t i, const char* key) const noexcept {
    for (int p = 0; p < inst[i].nIn; ++p) if (std::strcmp(inst[i].in[p].key, key) == 0) return p;
    return -1;
}
int Rack::outPort(uint32_t i, const char* key) const noexcept {
    for (int p = 0; p < inst[i].nOut; ++p) if (std::strcmp(inst[i].out[p].key, key) == 0) return p;
    return -1;
}

namespace {

using K = PortKind;

struct Builder {
    Rack r;

    InstanceDesc& add(Inst id, const char* key, const char* name, ModType t, uint32_t paramModule) {
        InstanceDesc& d = r.inst[id];
        d.key = key; d.name = name; d.type = t; d.paramModule = paramModule;
        if (id + 1u > r.count) r.count = id + 1u;
        return d;
    }
    static void in(InstanceDesc& d, const char* key, const char* name, K k, Atten a = {}) {
        d.in[d.nIn] = {key, name, k}; d.atten[d.nIn] = a; ++d.nIn;
    }
    static void out(InstanceDesc& d, const char* key, const char* name, K k) { d.out[d.nOut++] = {key, name, k}; }

    void normal(Inst s, const char* sp, Inst d, const char* dp, GainSpec g = {}) {
        const int so = r.outPort(s, sp), di = r.inPort(d, dp);
        if (so < 0 || di < 0) { std::fprintf(stderr, "Rack: bad normal %s.%s → %s.%s\n", r.inst[s].key, sp, r.inst[d].key, dp); std::abort(); }
        r.normals[r.normalCount++] = {s, static_cast<uint8_t>(so), d, static_cast<uint8_t>(di), g};
    }
    static GainSpec routing(float r0, float r1, float r2, float r3) {
        GainSpec g; g.kind = GainSpec::Kind::Routing; g.table[0] = r0; g.table[1] = r1; g.table[2] = r2; g.table[3] = r3; return g;
    }
};

Rack build() {
    Builder b;
    {   auto& d = b.add(I_KEYBOARD, "keyboard", "Keyboard", ModType::Keyboard, kNoParam);
        Builder::out(d, "pitch", "Pitch CV", K::CV); Builder::out(d, "gate", "Gate", K::Gate);
        Builder::out(d, "trigger", "Trigger", K::Trigger); Builder::out(d, "velocity", "Velocity", K::CV); }

    static const char* vcoKeys[9] = {"vco1", "vco2", "vco3", "vco4", "vco5", "vco6", "vco7", "vco8", "vco9"};
    static const char* vcoNames[9] = {"VCO 1", "VCO 2", "VCO 3", "VCO 4", "VCO 5", "VCO 6", "VCO 7", "VCO 8", "VCO 9"};
    static const uint64_t fmAddr[9] = {VK_P_VCO1_FM_AMOUNT, VK_P_VCO2_FM_AMOUNT, VK_P_VCO3_FM_AMOUNT, VK_P_VCO4_FM_AMOUNT,
                                       VK_P_VCO5_FM_AMOUNT, VK_P_VCO6_FM_AMOUNT, VK_P_VCO7_FM_AMOUNT, VK_P_VCO8_FM_AMOUNT, VK_P_VCO9_FM_AMOUNT};
    for (uint32_t v = 0; v < 9; ++v) {
        auto& d = b.add(static_cast<Inst>(I_VCO1 + v), vcoKeys[v], vcoNames[v], ModType::Vco, 1 + v);
        Builder::in(d, "pitch", "Pitch (1 V/oct)", K::CV);
        // pitchMod1: VCO 1/2 scaled by VCO 3's "→ VCO 1+2 pitch" knob (semitones per unit).
        Builder::in(d, "pitchMod1", "Pitch Mod 1", K::Audio, v < 2 ? Atten{VK_P_VCO3_PITCH_MOD_AMOUNT, 1.f / 60.f} : Atten{});
        // pitchMod2: VCO 1–3 scaled by EG 1's pitch amount.
        Builder::in(d, "pitchMod2", "Pitch Mod 2", K::CV, v < 3 ? Atten{VK_P_EG1_PITCH_AMOUNT, 1.f / 60.f} : Atten{});
        Builder::in(d, "fm", "FM", K::Audio, Atten{fmAddr[v], 0.01f});
        Builder::in(d, "pwm", "PWM", K::CV);
        Builder::in(d, "sync", "Sync", K::Trigger);
        Builder::out(d, "out", "Out", K::Audio); Builder::out(d, "sub", "Sub", K::Audio); Builder::out(d, "sync", "Sync Out", K::Trigger);
    }
    {   auto& d = b.add(I_NOISE, "noise", "Noise", ModType::Noise, 30); Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_RING, "ringMod", "Ring Modulator", ModType::Ring, 31);
        Builder::in(d, "carrier", "Carrier", K::Audio); Builder::in(d, "modulator", "Modulator", K::Audio); Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_MIXER, "mixer", "Voice Mixer", ModType::VoiceMixer, kNoParam);
        Builder::in(d, "in1", "In 1 (VCO 1 level)", K::Audio, {VK_P_VCO1_LEVEL, 0.01f});
        Builder::in(d, "in2", "In 2 (VCO 2 level)", K::Audio, {VK_P_VCO2_LEVEL, 0.01f});
        Builder::in(d, "in3", "In 3 (VCO 3 level)", K::Audio, {VK_P_VCO3_LEVEL, 0.01f});
        Builder::in(d, "in4", "In 4 (sub level)", K::Audio, {VK_P_VCO1_SUB_LEVEL, 0.01f});
        Builder::in(d, "in5", "In 5 (noise level)", K::Audio, {VK_P_NOISE_LEVEL, 0.01f});
        Builder::in(d, "in6", "In 6 (ring level)", K::Audio, {VK_P_RINGMOD_LEVEL, 0.01f});
        Builder::out(d, "out", "Out", K::Audio); }
    auto filterInputs = [](InstanceDesc& d, uint64_t env, uint64_t key, uint64_t sh, uint64_t fm) {
        Builder::in(d, "in", "In", K::Audio);
        Builder::in(d, "env", "Cutoff CV 1 (EG 1 amount)", K::CV, {env, 0.2f});
        Builder::in(d, "key", "Cutoff CV 2 (key track)", K::CV, {key, 0.01f});
        Builder::in(d, "cv", "Cutoff CV 3 (S&H amount)", K::CV, {sh, 0.2f});
        Builder::in(d, "fm", "Cutoff FM (audio rate, VCO 3 amount)", K::Audio, {fm, 0.2f});
    };
    {   auto& d = b.add(I_VCFA, "vcfA", "Filter A (multimode)", ModType::FilterSvf, 10);
        filterInputs(d, VK_P_VCFA_ENV_AMOUNT, VK_P_VCFA_KEY_TRACK, VK_P_VCFA_SH_AMOUNT, VK_P_VCFA_VCO3_AMOUNT);
        Builder::out(d, "out", "Out (mode)", K::Audio); Builder::out(d, "lp", "Low-pass", K::Audio); Builder::out(d, "bp", "Band-pass", K::Audio);
        Builder::out(d, "hp", "High-pass", K::Audio); Builder::out(d, "notch", "Notch", K::Audio); }
    {   auto& d = b.add(I_VCFB, "vcfB", "Filter B (ladder)", ModType::FilterLadder, 11);
        filterInputs(d, VK_P_VCFB_ENV_AMOUNT, VK_P_VCFB_KEY_TRACK, VK_P_VCFB_SH_AMOUNT, VK_P_VCFB_VCO3_AMOUNT);
        Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_ROUTE_B, "routeB", "Filter B input select", ModType::Sum, kNoParam); d.hidden = true;
        Builder::in(d, "in1", "Mixer", K::Audio); Builder::in(d, "in2", "Filter A", K::Audio); Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_ROUTE_OUT, "routeOut", "Filter output select", ModType::Sum, kNoParam); d.hidden = true;
        Builder::in(d, "in1", "Filter A", K::Audio); Builder::in(d, "in2", "Filter B", K::Audio); Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_VCA1, "vca", "VCA 1", ModType::Vca, 18);
        Builder::in(d, "in", "In", K::Audio); Builder::in(d, "cv1", "CV 1 (velocity scaled)", K::CV); Builder::in(d, "cv2", "CV 2", K::CV);
        Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_VCA2, "vca2", "VCA 2", ModType::Vca, 19);
        Builder::in(d, "in", "In", K::Audio); Builder::in(d, "cv1", "CV 1", K::CV, {VK_P_VCA2_CV_AMOUNT, 0.01f}); Builder::in(d, "cv2", "CV 2", K::CV);
        Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_EG1, "eg1", "EG 1", ModType::Envelope, 20);
        Builder::in(d, "gate", "Gate", K::Gate); Builder::in(d, "trigger", "Retrigger", K::Trigger);
        Builder::out(d, "out", "Out", K::CV); Builder::out(d, "inv", "Inverted", K::CV); }
    {   auto& d = b.add(I_EG2, "eg2", "EG 2", ModType::Envelope, 21);
        Builder::in(d, "gate", "Gate", K::Gate); Builder::in(d, "trigger", "Retrigger", K::Trigger);
        Builder::out(d, "out", "Out", K::CV); Builder::out(d, "inv", "Inverted", K::CV); }
    {   auto& d = b.add(I_OTA, "ota", "OTA Multimode Filter", ModType::Ota, 13);
        Builder::in(d, "in", "In", K::Audio); Builder::in(d, "cv1", "Cutoff CV 1", K::CV, {VK_P_OTA_CV1_AMOUNT, 0.2f});
        Builder::in(d, "cv2", "Cutoff CV 2", K::CV, {VK_P_OTA_CV2_AMOUNT, 0.2f});
        Builder::out(d, "out", "Out (mode)", K::Audio); Builder::out(d, "lp", "Low-pass", K::Audio);
        Builder::out(d, "bp", "Band-pass", K::Audio); Builder::out(d, "hp", "High-pass", K::Audio); }
    {   auto& d = b.add(I_COMB, "comb", "Comb Filter", ModType::Comb, 14);
        Builder::in(d, "in", "In", K::Audio); Builder::in(d, "cv", "Frequency CV", K::CV, {VK_P_COMB_CV_AMOUNT, 0.2f});
        Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_FORMANT, "formant", "Formant Filter", ModType::Formant, 15);
        Builder::in(d, "in", "In", K::Audio); Builder::in(d, "cv", "Vowel CV", K::CV, {VK_P_FORMANT_CV_AMOUNT, 0.01f});
        Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_VCBPF, "vcbpf", "VC Band-pass Filter", ModType::Vcbpf, 17);
        Builder::in(d, "in", "In", K::Audio); Builder::in(d, "cv1", "Centre CV 1", K::CV, {VK_P_VCBPF_CV1_AMOUNT, 0.2f});
        Builder::in(d, "cv2", "Centre CV 2", K::CV, {VK_P_VCBPF_CV2_AMOUNT, 0.2f}); Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_FBANK, "filterBank", "Fixed Filter Bank", ModType::FilterBank, 16);
        Builder::in(d, "in", "In", K::Audio); Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_SWITCH, "switch", "Analogue Switch", ModType::Switch, 57);
        Builder::in(d, "in1", "In 1", K::Audio); Builder::in(d, "in2", "In 2", K::Audio); Builder::in(d, "in3", "In 3", K::Audio);
        Builder::in(d, "in4", "In 4", K::Audio); Builder::in(d, "select", "Select CV", K::CV); Builder::in(d, "clock", "Step Clock", K::Trigger);
        Builder::out(d, "out", "Out", K::Audio); }
    for (Inst m : {I_MULT1, I_MULT2}) {
        auto& d = b.add(m, m == I_MULT1 ? "mult1" : "mult2", m == I_MULT1 ? "Multiple 1" : "Multiple 2", ModType::Mult, kNoParam);
        Builder::in(d, "in", "In", K::Audio);
        Builder::out(d, "out1", "Out 1", K::Audio); Builder::out(d, "out2", "Out 2", K::Audio);
        Builder::out(d, "out3", "Out 3", K::Audio); Builder::out(d, "out4", "Out 4", K::Audio);
    }
    {   auto& d = b.add(I_GATE_DELAY, "gateDelay", "Gate Delay", ModType::GateDelay, 58);
        Builder::in(d, "in", "Gate In", K::Gate); Builder::out(d, "out", "Gate Out", K::Gate); }
    {   auto& d = b.add(I_MIXER4, "mixer4", "Mixer (4-channel)", ModType::Mixer4, 56);
        Builder::in(d, "in1", "In 1", K::Audio); Builder::in(d, "in2", "In 2", K::Audio);
        Builder::in(d, "in3", "In 3", K::Audio); Builder::in(d, "in4", "In 4", K::Audio);
        Builder::out(d, "out", "Out", K::Audio); Builder::out(d, "inv", "Inverted Out", K::Audio); }
    {   auto& d = b.add(I_VPROC, "vproc", "Voltage Processor", ModType::VoltageProc, 35);
        Builder::in(d, "in1", "In 1", K::CV); Builder::in(d, "in2", "In 2", K::CV); Builder::out(d, "out", "Out", K::CV); }
    {   auto& d = b.add(I_SLEW, "slew", "Slew Limiter", ModType::Slew, 33);
        Builder::in(d, "in", "In", K::CV); Builder::out(d, "out", "Out", K::CV); }
    {   auto& d = b.add(I_FOLLOWER, "follower", "Envelope Follower", ModType::Follower, 34);
        Builder::in(d, "in", "In", K::Audio); Builder::out(d, "out", "Out", K::CV); }
    {   auto& d = b.add(I_SH, "sampleHold", "Sample & Hold", ModType::SampleHold, 32);
        Builder::in(d, "in", "Signal In (noise if unpatched)", K::Audio); Builder::in(d, "trigger", "Trigger", K::Trigger);
        Builder::out(d, "out", "Out", K::CV); }
    {   auto& d = b.add(I_OUTPUT, "output", "Voice Output", ModType::Output, kNoParam);
        Builder::in(d, "in", "In", K::Audio); Builder::in(d, "pan", "Pan CV", K::CV); }

    {   auto& d = b.add(I_DIODE, "diodeLadder", "Diode Ladder Filter", ModType::DiodeLadder, 12);
        Builder::in(d, "in", "In", K::Audio); Builder::in(d, "cv1", "Cutoff CV 1", K::CV, {VK_P_DIODELADDER_CV1_AMOUNT, 0.2f});
        Builder::in(d, "cv2", "Cutoff CV 2", K::CV, {VK_P_DIODELADDER_CV2_AMOUNT, 0.2f}); Builder::out(d, "out", "Out", K::Audio); }
    {   auto& d = b.add(I_TLADDER, "transistorLadder", "Transistor Ladder Filter", ModType::TransistorLadder, 59);
        Builder::in(d, "in", "In", K::Audio); Builder::in(d, "cv1", "Cutoff CV 1", K::CV, {VK_P_TRANSISTORLADDER_CV1_AMOUNT, 0.2f});
        Builder::in(d, "cv2", "Cutoff CV 2", K::CV, {VK_P_TRANSISTORLADDER_CV2_AMOUNT, 0.2f}); Builder::out(d, "out", "Out", K::Audio); }

    for (uint32_t k = 0; k < 3; ++k) {
        static const char* keys[3] = {"lfo1", "lfo2", "lfo3"};
        static const char* names[3] = {"LFO 1", "LFO 2", "LFO 3"};
        auto& d = b.add(static_cast<Inst>(I_LFO1 + k), keys[k], names[k], ModType::Lfo, 26 + k);
        Builder::out(d, "out", "Out", K::CV); Builder::out(d, "inv", "Inverted", K::CV); }

    // ---------------- normalled connections (the semi-modular voice) ----------------
    for (uint32_t v = 0; v < 9; ++v) b.normal(I_KEYBOARD, "pitch", static_cast<Inst>(I_VCO1 + v), "pitch");
    b.normal(I_VCO3, "out", I_VCO1, "pitchMod1");
    b.normal(I_VCO3, "out", I_VCO2, "pitchMod1");
    for (Inst v : {I_VCO1, I_VCO2, I_VCO3}) b.normal(I_EG1, "out", v, "pitchMod2");
    b.normal(I_VCO1, "out", I_VCO2, "fm");
    b.normal(I_VCO1, "sync", I_VCO2, "sync");
    b.normal(I_VCO1, "out", I_RING, "carrier");
    b.normal(I_VCO2, "out", I_RING, "modulator");
    b.normal(I_VCO1, "out", I_MIXER, "in1");
    b.normal(I_VCO2, "out", I_MIXER, "in2");
    b.normal(I_VCO3, "out", I_MIXER, "in3");
    b.normal(I_VCO1, "sub", I_MIXER, "in4");
    b.normal(I_NOISE, "out", I_MIXER, "in5");
    b.normal(I_RING, "out", I_MIXER, "in6");
    b.normal(I_MIXER, "out", I_VCFA, "in");
    b.normal(I_MIXER, "out", I_ROUTE_B, "in1", Builder::routing(1, 1, 0, 1));
    b.normal(I_VCFA, "out", I_ROUTE_B, "in2", Builder::routing(0, 0, 1, 0));
    b.normal(I_ROUTE_B, "out", I_VCFB, "in");
    for (Inst f : {I_VCFA, I_VCFB}) {
        b.normal(I_EG1, "out", f, "env");
        b.normal(I_KEYBOARD, "pitch", f, "key");
        b.normal(I_SH, "out", f, "cv");
        b.normal(I_VCO3, "out", f, "fm");
    }
    b.normal(I_VCFA, "out", I_ROUTE_OUT, "in1", Builder::routing(1, 0, 0, 0.5f));
    b.normal(I_VCFB, "out", I_ROUTE_OUT, "in2", Builder::routing(0, 1, 1, 0.5f));
    b.normal(I_ROUTE_OUT, "out", I_VCA1, "in");
    b.normal(I_EG2, "out", I_VCA1, "cv1");
    b.normal(I_VCA1, "out", I_OUTPUT, "in");
    for (Inst e : {I_EG1, I_EG2}) { b.normal(I_KEYBOARD, "gate", e, "gate"); b.normal(I_KEYBOARD, "trigger", e, "trigger"); }
    return b.r;
}

} // namespace

const Rack& rack() {
    static const Rack r = build();
    return r;
}

} // namespace vektur
