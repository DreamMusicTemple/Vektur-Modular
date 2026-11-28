// render_demo.cpp — renders a short audition of Phase 2 patches through the real
// kernel (same code as the app/AUv3) to a 48 kHz / 24-bit stereo WAV.
//   ./build/render_demo out.wav
#include "VekturDSP.h"
#include "fx/FxSpecs.hpp"
using namespace vektur::fx;
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <cstring>

struct Note { double t; int note; double len; int vel; };
static std::vector<float> L, R;

// "vco4.out>mixer.in1@0.5" style cable spec → VKCable
static std::vector<VKCable> gPatch;
static int findModule(const std::string& key) {
    for (uint32_t m = 0; m < vk_rack_module_count(); ++m) { VKModuleInfo i; vk_rack_module_info(m, &i); if (key == i.key) return static_cast<int>(m); }
    std::fprintf(stderr, "unknown module %s\n", key.c_str()); std::exit(2);
}
static int findPort(int m, const std::string& key, bool input) {
    VKModuleInfo mi; vk_rack_module_info(static_cast<uint32_t>(m), &mi);
    for (uint32_t p = 0; p < (input ? mi.inputCount : mi.outputCount); ++p) { VKPortInfo pi; vk_rack_port_info(static_cast<uint32_t>(m), p, input, &pi); if (key == pi.key) return static_cast<int>(p); }
    std::fprintf(stderr, "unknown port %s.%s\n", mi.key, key.c_str()); std::exit(2);
}
static std::vector<VKCable> patch(std::initializer_list<const char*> specs) {
    std::vector<VKCable> out; uint16_t id = 0;
    for (const char* sp : specs) {
        std::string s(sp); float gain = 1.f;
        if (auto at = s.find('@'); at != std::string::npos) { gain = std::stof(s.substr(at + 1)); s = s.substr(0, at); }
        const auto arrow = s.find('>'); const std::string a = s.substr(0, arrow), b = s.substr(arrow + 1);
        const int sm = findModule(a.substr(0, a.find('.'))), dm = findModule(b.substr(0, b.find('.')));
        out.push_back({id++, static_cast<uint8_t>(sm), static_cast<uint8_t>(findPort(sm, a.substr(a.find('.') + 1), false)),
                       static_cast<uint8_t>(dm), static_cast<uint8_t>(findPort(dm, b.substr(b.find('.') + 1), true)), gain});
    }
    return out;
}

static void section(const char* name, double secs, const std::vector<std::pair<uint64_t, float>>& patch,
                    const std::vector<Note>& notes, std::function<void(VKKernel*, double)> automate = {}) {
    std::printf("  %-34s %.1f s\n", name, secs);
    VKKernel* k = vk_kernel_create();
    for (auto& [a, v] : patch) vk_kernel_set_param(k, a, v);
    vk_kernel_allocate(k, 48000, 2, 256);
    if (!gPatch.empty()) {
        VKPatchResult r; vk_kernel_set_patch(k, gPatch.data(), static_cast<uint32_t>(gPatch.size()), &r);
        if (r.rejected) std::printf("    (%u cable(s) rejected)\n", r.rejected);
        std::vector<float> l(256), rr(256); float* c2[2] = {l.data(), rr.data()};
        vk_kernel_render(k, c2, 2, 256, nullptr, 0);   // let the patch swap settle
    }
    std::vector<float> l(256), r(256); float* ch[2] = {l.data(), r.data()};
    const int blocks = static_cast<int>(secs * 48000 / 256);
    for (int b = 0; b < blocks; ++b) {
        const double t0 = b * 256 / 48000.0, t1 = (b + 1) * 256 / 48000.0;
        if (automate) automate(k, t0);
        std::vector<VKEvent> ev;
        for (auto& n : notes) {
            auto push = [&](double t, uint8_t st, uint8_t vel) {
                if (t >= t0 && t < t1) { VKEvent e{}; e.type = VK_EVENT_MIDI1; e.length = 3; e.sampleOffset = static_cast<uint32_t>((t - t0) * 48000);
                    e.data[0] = st; e.data[1] = static_cast<uint8_t>(n.note); e.data[2] = vel; ev.push_back(e); }
            };
            push(n.t, 0x90, static_cast<uint8_t>(n.vel)); push(n.t + n.len, 0x80, 0);
        }
        std::sort(ev.begin(), ev.end(), [](auto& a, auto& c) { return a.sampleOffset < c.sampleOffset; });
        vk_kernel_render(k, ch, 2, 256, ev.data(), static_cast<uint32_t>(ev.size()));
        L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end());
    }
    vk_kernel_destroy(k);
    L.insert(L.end(), 24000, 0.f); R.insert(R.end(), 24000, 0.f);   // 0.5 s gap
}

static std::vector<Note> seq(const std::vector<int>& notes, double step, double gate, int vel = 100, double start = 0, int repeats = 1) {
    std::vector<Note> out; double t = start;
    for (int r = 0; r < repeats; ++r) for (int n : notes) { if (n > 0) out.push_back({t, n, step * gate, vel}); t += step; }
    return out;
}

static void phase3Demo() {
    gPatch = patch({"keyboard.pitch>comb.cv", "noise.out>vca2.in", "eg1.out>vca2.cv1", "vca2.out>comb.in", "comb.out>vca.in"});
    section("1 Plucked comb: noise burst → comb tracking the keyboard", 5.0,
            {{VK_P_COMB_FREQUENCY, 261.6f}, {VK_P_COMB_FEEDBACK, 97}, {VK_P_COMB_DAMPING, 35}, {VK_P_EG1_ATTACK, 0.5f}, {VK_P_EG1_DECAY, 25}, {VK_P_EG1_SUSTAIN, 0},
             {VK_P_EG2_DECAY, 1800}, {VK_P_EG2_SUSTAIN, 0}, {VK_P_EG2_RELEASE, 1500}, {VK_P_GLOBAL_STEREO_SPREAD, 70}},
            seq({60, 64, 67, 72, 67, 64, 60, 55}, 0.3, 0.8, 110, 0, 2));
    gPatch = patch({"mixer.out>formant.in", "vco3.out>formant.cv", "formant.out>vcfB.in"});
    section("2 Talking formant filter: VCO 3 sweeps the vowel A→U", 5.5,
            {{VK_P_VCO2_LEVEL, 80}, {VK_P_VCO2_FINE, 7}, {VK_P_VCO3_LOW_FREQ_MODE, 1}, {VK_P_VCO3_OCTAVE, 4}, {VK_P_VCO3_WAVE, 1}, {VK_P_FORMANT_CV_AMOUNT, 50},
             {VK_P_FORMANT_VOWEL, 50}, {VK_P_FORMANT_RESONANCE, 85}, {VK_P_VCFB_CUTOFF, 6000}, {VK_P_VCFB_ENV_AMOUNT, 0}, {VK_P_GLOBAL_POLYPHONY, 0}, {VK_P_GLOBAL_MASTER_VOLUME, 4}},
            seq({43, 46, 41, 38}, 1.3, 0.95, 100));
    gPatch = patch({"vco4.out>mixer4.in1", "vco5.out>mixer4.in2", "vco6.out>mixer4.in3", "vco7.out>mixer4.in4", "mixer4.out>mixer.in2", "vco8.out>mixer.in3"});
    section("3 Oscillator bank: VCO 1–2 + four bank VCOs detuned (supersaw)", 6.0,
            {{VK_P_VCO2_LEVEL, 70}, {VK_P_VCO3_LEVEL, 40}, {VK_P_VCO4_FINE, -11}, {VK_P_VCO5_FINE, 7}, {VK_P_VCO6_FINE, -5}, {VK_P_VCO7_FINE, 13}, {VK_P_VCO8_OCTAVE, 2},
             {VK_P_MIXER4_LEVEL, 55}, {VK_P_EG2_ATTACK, 400}, {VK_P_EG2_RELEASE, 1500}, {VK_P_VCFB_CUTOFF, 900}, {VK_P_VCFB_ENV_AMOUNT, 2}, {VK_P_EG1_ATTACK, 900},
             {VK_P_GLOBAL_STEREO_SPREAD, 90}, {VK_P_GLOBAL_MASTER_VOLUME, -14}},
            {{0, 45, 4.4, 95}, {0, 52, 4.4, 95}, {0, 57, 4.4, 95}, {0, 61, 4.4, 95}});
    gPatch = patch({"vca.out>mixer4.in1", "mixer.out>mixer4.in2", "mixer4.out>vcfB.in", "noise.out>mixer.in5"});
    section("4 Feedback patch: VCA output fed back into Filter B", 5.0,
            {{VK_P_MIXER4_GAIN1, 92}, {VK_P_VCFB_RESONANCE, 70}, {VK_P_VCFB_DRIVE, 12}, {VK_P_NOISE_LEVEL, 15}, {VK_P_VCFB_CUTOFF, 400}, {VK_P_VCFB_ENV_AMOUNT, 4}, {VK_P_EG1_DECAY, 900}, {VK_P_EG1_SUSTAIN, 10}},
            seq({33, 0, 36, 31}, 1.2, 0.9, 110));
    gPatch = patch({"keyboard.gate>gateDelay.in", "gateDelay.out>eg1.gate", "noise.out>vcbpf.in", "vcbpf.out>mixer.in5"});
    section("5 Gate delay: filter sweep fires 180 ms after each note; VC band-pass noise", 4.5,
            {{VK_P_GATEDELAY_DELAY, 180}, {VK_P_NOISE_LEVEL, 60}, {VK_P_VCBPF_CENTRE, 1800}, {VK_P_VCBPF_Q, 18}, {VK_P_VCFB_ENV_AMOUNT, 5}, {VK_P_VCFB_CUTOFF, 200},
             {VK_P_VCFB_RESONANCE, 55}, {VK_P_EG1_DECAY, 250}, {VK_P_EG1_SUSTAIN, 0}},
            seq({48, 55, 51, 58}, 0.5, 0.7, 110, 0, 2));
    gPatch.clear();
}

// Matrix slot helper: {enable, source, via, dest, amount, offset} params.
static std::vector<std::pair<uint64_t, float>> slot(uint32_t s, int src, uint64_t dest, float amount, float offset = 0, int via = 0) {
    const uint64_t b = 60 * 256 + s * 16;
    return {{b, 1}, {b + 1, static_cast<float>(src)}, {b + 2, static_cast<float>(via)}, {b + 3, static_cast<float>(dest)}, {b + 4, amount}, {b + 5, offset}};
}
static std::vector<std::pair<uint64_t, float>> join(std::initializer_list<std::vector<std::pair<uint64_t, float>>> parts) {
    std::vector<std::pair<uint64_t, float>> out; for (auto& p : parts) out.insert(out.end(), p.begin(), p.end()); return out;
}
enum { S_LFO1 = 1, S_LFO2, S_LFO3, S_EG1, S_EG2, S_VEL, S_KEY, S_WHEEL, S_AT, S_PAT, S_BEND, S_BREATH, S_EXPR, S_MACRO1 };

static void phase4Demo() {
    section("1 Delayed vibrato: LFO 1 → pitch, 700 ms fade-in", 7.0,
            join({{{VK_P_LFO1_SHAPE, 0}, {VK_P_LFO1_RATE, 5.5f}, {VK_P_LFO1_SYNC, 1}, {VK_P_LFO1_DELAY, 700},
                   {VK_P_VCO2_LEVEL, 60}, {VK_P_VCO2_FINE, 6}, {VK_P_VCFB_CUTOFF, 1800}, {VK_P_VCFB_ENV_AMOUNT, 1.5f}, {VK_P_EG2_RELEASE, 400},
                   {VK_P_GLOBAL_VOICE_MODE, 2}, {VK_P_GLOBAL_GLIDE, 60}, {VK_P_GLOBAL_GLIDE_MODE, 1}, {VK_P_GLOBAL_MASTER_VOLUME, -2}},
                  slot(0, S_LFO1, VK_MOD_DEST_PITCH, 2.5f)}),
            {{0, 64, 1.6, 100}, {1.5, 67, 0.9, 100}, {2.35, 69, 2.0, 100}, {4.3, 67, 0.55, 100}, {4.8, 64, 2.0, 100}});
    section("2 Random-step LFO 2 → resonant Filter B (sample & hold sweep)", 6.0,
            join({{{VK_P_LFO2_SHAPE, 5}, {VK_P_LFO2_RATE, 7}, {VK_P_VCFB_CUTOFF, 900}, {VK_P_VCFB_RESONANCE, 72}, {VK_P_VCFB_ENV_AMOUNT, 0},
                   {VK_P_VCO2_LEVEL, 70}, {VK_P_VCO2_FINE, -8}, {VK_P_EG2_RELEASE, 900}, {VK_P_GLOBAL_STEREO_SPREAD, 60}},
                  slot(0, S_LFO2, VK_P_VCFB_CUTOFF, 22), slot(1, S_LFO1, VK_MOD_DEST_PAN, 40)}),
            {{0, 45, 5.0, 100}, {0, 52, 5.0, 100}, {0, 57, 5.0, 100}, {0, 60, 5.0, 100}});
    section("3 Legato bass: overlapping notes slide, detached notes retrigger", 6.0,
            {{VK_P_GLOBAL_VOICE_MODE, 2}, {VK_P_GLOBAL_GLIDE_MODE, 1}, {VK_P_GLOBAL_GLIDE, 90}, {VK_P_VCO1_WAVE, 4}, {VK_P_VCO2_LEVEL, 80}, {VK_P_VCO2_OCTAVE, 2},
             {VK_P_VCFB_CUTOFF, 300}, {VK_P_VCFB_RESONANCE, 45}, {VK_P_VCFB_ENV_AMOUNT, 4}, {VK_P_EG1_DECAY, 220}, {VK_P_EG1_SUSTAIN, 15}},
            {{0, 33, 0.45, 110}, {0.5, 33, 0.3, 110}, {0.75, 45, 0.3, 110}, {1.0, 43, 0.55, 110}, {1.5, 36, 0.2, 110},
             {2.0, 33, 0.45, 110}, {2.5, 40, 0.6, 110}, {3.0, 38, 0.6, 110}, {3.5, 36, 0.25, 110}, {3.75, 31, 0.7, 110}, {4.5, 33, 1.0, 110}});
    section("4 Unison lead: 7 voices, 22 cents detune, glide", 6.0,
            {{VK_P_GLOBAL_VOICE_MODE, 3}, {VK_P_GLOBAL_UNISON_VOICES, 5}, {VK_P_GLOBAL_UNISON_DETUNE, 22}, {VK_P_GLOBAL_GLIDE, 70}, {VK_P_GLOBAL_STEREO_SPREAD, 100},
             {VK_P_VCFB_CUTOFF, 2500}, {VK_P_VCFB_ENV_AMOUNT, 1}, {VK_P_EG2_RELEASE, 500}, {VK_P_GLOBAL_MASTER_VOLUME, -8}},
            seq({69, 72, 76, 74, 72, 69, 67, 69}, 0.6, 1.05, 100));
    section("5 Macro 1 sweep: opens the filter and speeds up LFO 1 at once", 7.0,
            join({{{VK_P_LFO1_SHAPE, 1}, {VK_P_LFO1_RATE, 1.0f}, {VK_P_VCFB_CUTOFF, 250}, {VK_P_VCFB_RESONANCE, 50}, {VK_P_VCFB_ENV_AMOUNT, 0},
                   {VK_P_VCO2_LEVEL, 70}, {VK_P_VCO2_FINE, 9}, {VK_P_GLOBAL_STEREO_SPREAD, 50}},
                  slot(0, S_MACRO1, VK_P_VCFB_CUTOFF, 55), slot(1, S_MACRO1, VK_P_LFO1_RATE, 40), slot(2, S_LFO1, VK_P_VCFB_CUTOFF, 10)}),
            {{0, 41, 6.4, 100}, {0, 48, 6.4, 100}, {0, 53, 6.4, 100}},
            [](VKKernel* k, double t) { vk_kernel_set_param(k, VK_P_MACRO_MACRO1, static_cast<float>(std::min(100.0, t / 5.5 * 100.0))); });
    section("6 Velocity → VCO 2 level and cutoff, per voice (soft / loud)", 5.0,
            join({{{VK_P_VCO2_LEVEL, 0}, {VK_P_VCO2_OCTAVE, 4}, {VK_P_VCO2_WAVE, 4}, {VK_P_VCFB_CUTOFF, 500}, {VK_P_VCFB_ENV_AMOUNT, 2}, {VK_P_EG2_DECAY, 500}, {VK_P_EG2_SUSTAIN, 30}},
                  slot(0, S_VEL, VK_P_VCO2_LEVEL, 100), slot(1, S_VEL, VK_P_VCFB_CUTOFF, 35)}),
            {{0, 57, 0.35, 25}, {0.5, 57, 0.35, 127}, {1.0, 60, 0.35, 25}, {1.5, 60, 0.35, 127}, {2.0, 64, 0.35, 25}, {2.5, 64, 0.35, 127},
             {3.0, 57, 1.2, 40}, {3.0, 64, 1.2, 127}});
}

static void phase4bDemo() {
    section("1 Tempo-synced gate: LFO 1 square 1/16 → level, LFO 2 1 bar → cutoff (128 BPM)", 7.5,
            join({{{VK_P_GLOBAL_TEMPO, 128}, {VK_P_LFO1_SHAPE, 4}, {VK_P_LFO1_TEMPO_SYNC, 7}, {VK_P_LFO2_SHAPE, 1}, {VK_P_LFO2_TEMPO_SYNC, 3},
                   {VK_P_VCO2_LEVEL, 80}, {VK_P_VCO2_FINE, 8}, {VK_P_VCFB_CUTOFF, 700}, {VK_P_VCFB_RESONANCE, 55}, {VK_P_VCFB_ENV_AMOUNT, 0},
                   {VK_P_EG2_RELEASE, 300}, {VK_P_GLOBAL_STEREO_SPREAD, 70}, {VK_P_GLOBAL_MASTER_VOLUME, -9}},
                  slot(0, S_LFO1, VK_MOD_DEST_AMP, -45, 0), slot(1, S_LFO2, VK_P_VCFB_CUTOFF, 25)}),
            {{0, 50, 7.0, 100}, {0, 57, 7.0, 100}, {0, 62, 7.0, 100}, {0, 65, 7.0, 100}});
    section("2 Split keyboard: zone A bass (−12, square), zone B lead (saw, open filter)", 7.0,
            join({{{VK_P_GLOBAL_ZONE_MODE, 1}, {VK_P_GLOBAL_SPLIT_POINT, 60}, {VK_P_GLOBAL_ZONE_A_TRANSPOSE, -12},
                   {VK_P_VCO1_LEVEL, 0}, {VK_P_VCO2_LEVEL, 100}, {VK_P_VCO2_WAVE, 4}, {VK_P_VCFB_CUTOFF, 400}, {VK_P_VCFB_ENV_AMOUNT, 3}, {VK_P_EG1_DECAY, 300}},
                  slot(0, 22, VK_P_VCO1_LEVEL, 100), slot(1, 22, VK_P_VCO2_LEVEL, -100), slot(2, 22, VK_P_VCFB_CUTOFF, 20)}),
            [] { auto n = seq({45, 45, 52, 45, 48, 48, 55, 43}, 0.4, 0.8, 110); auto m = seq({72, 76, 79, 76, 74, 72, 74, 0}, 0.8, 0.9, 100);
                 n.insert(n.end(), m.begin(), m.end()); auto o = seq({45, 45, 52, 45, 48, 48, 55, 43}, 0.4, 0.8, 110, 3.2); n.insert(n.end(), o.begin(), o.end()); return n; }());
    section("3 Layer: zone B +12 with slow attack and vibrato over zone A", 6.0,
            join({{{VK_P_GLOBAL_ZONE_MODE, 2}, {VK_P_GLOBAL_ZONE_B_TRANSPOSE, 12}, {VK_P_LFO1_RATE, 5.5f}, {VK_P_LFO1_SYNC, 1},
                   {VK_P_VCO2_LEVEL, 60}, {VK_P_VCO2_FINE, 5}, {VK_P_VCFB_CUTOFF, 1200}, {VK_P_EG2_RELEASE, 900}, {VK_P_GLOBAL_STEREO_SPREAD, 80},
                   {VK_P_GLOBAL_MASTER_VOLUME, -9}},
                  slot(0, 22, VK_MOD_DEST_AMP, -55, 0), slot(1, 22, VK_P_EG2_ATTACK, 30), slot(2, S_LFO1, VK_MOD_DEST_PITCH, 1.5f, 0, 22)}),
            {{0, 57, 2.6, 100}, {0, 64, 2.6, 100}, {2.8, 55, 2.6, 100}, {2.8, 62, 2.6, 100}});
}

static std::vector<std::pair<uint64_t, float>> preset(uint32_t seed, uint32_t cat, std::vector<std::pair<uint64_t, float>> extra) {
    std::vector<VKParamValue> v(1024);
    const uint32_t n = vk_preset_generate(seed, cat, v.data(), 1024, nullptr);
    std::vector<std::pair<uint64_t, float>> out;
    for (uint32_t i = 0; i < n; ++i) out.push_back({v[i].address, v[i].value});
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

static void phase5Demo() {
    constexpr uint64_t ARP = 36 * 256, SEQ = 37 * 256, GEN = 38 * 256;
    auto st = [](int s, int f) { return SEQ + 32 + static_cast<uint64_t>(s) * 8 + static_cast<uint64_t>(f); };
    section("1 Arp: up-down 2 oct, 1/16, swing 58 %, 2× ratchets 25 %", 8.0,
            {{VK_P_GLOBAL_TEMPO, 118}, {ARP, 1}, {ARP + 1, 2}, {ARP + 2, 1}, {ARP + 6, 58}, {ARP + 12, 1}, {ARP + 13, 25}, {ARP + 5, 45},
             {VK_P_VCFB_CUTOFF, 900}, {VK_P_VCFB_RESONANCE, 45}, {VK_P_VCFB_ENV_AMOUNT, 3}, {VK_P_EG1_DECAY, 160}, {VK_P_EG1_SUSTAIN, 10},
             {VK_P_VCO2_LEVEL, 60}, {VK_P_VCO2_FINE, 7}, {VK_P_GLOBAL_MASTER_VOLUME, -9}},
            {{0, 57, 3.9, 100}, {0, 60, 3.9, 100}, {0, 64, 3.9, 100}, {0, 67, 3.9, 100}, {4, 53, 3.8, 100}, {4, 57, 3.8, 100}, {4, 60, 3.8, 100}, {4, 64, 3.8, 100}});
    section("2 Arp: Euclid E(5,8), 85 % probability, chain Up → Random", 8.0,
            preset(7, VK_PRESET_PLUCK, {{VK_P_GLOBAL_TEMPO, 118}, {ARP, 1}, {ARP + 2, 2}, {ARP + 7, 1}, {ARP + 8, 7}, {ARP + 9, 5}, {ARP + 11, 85},
                                        {ARP + 18, 1}, {ARP + 19, 7}, {ARP + 22, 1}, {ARP + 16, 1}}),
            {{0, 48, 3.9, 100}, {0, 55, 3.9, 100}, {0, 62, 3.9, 100}, {4, 43, 3.8, 100}, {4, 50, 3.8, 100}, {4, 59, 3.8, 100}});
    std::vector<std::pair<uint64_t, float>> sq = {{VK_P_GLOBAL_TEMPO, 118}, {SEQ, 1}, {SEQ + 6, 1}, {SEQ + 7, 11}, {SEQ + 8, 9}, {SEQ + 5, 60},
        {VK_P_VCFB_CUTOFF, 500}, {VK_P_VCFB_RESONANCE, 60}, {VK_P_VCFB_ENV_AMOUNT, 2}, {VK_P_EG1_DECAY, 200}, {VK_P_EG1_SUSTAIN, 0},
        {VK_P_VCO1_SUB_LEVEL, 50}, {VK_P_GLOBAL_VOICE_MODE, 2}, {VK_P_GLOBAL_GLIDE, 40}, {VK_P_GLOBAL_MASTER_VOLUME, -6},
        {60 * 256 + 0, 1}, {60 * 256 + 1, 25}, {60 * 256 + 3, VK_P_VCFB_CUTOFF}, {60 * 256 + 4, 35}};
    const float rowA[16] = {0, 0, 50, 21, 0, 29, 0, 42, 0, 0, 50, 21, 62, 42, 29, 21};
    const float rowC[16] = {10, 30, 60, 20, 90, 10, 40, 70, 15, 35, 65, 25, 100, 50, 20, 80};
    for (int s2 = 0; s2 < 16; ++s2) { sq.push_back({st(s2, 0), rowA[s2]}); sq.push_back({st(s2, 2), rowC[s2]}); }
    sq.push_back({st(3, 3), 2}); sq.push_back({st(9, 3), 0}); sq.push_back({st(13, 5), 1});
    section("3 Analogue sequencer: 16 steps, A minor pentatonic, row C → cutoff, tie, rest, ratchet", 8.2, sq, {});
    section("4 Generative melody (A minor, evolve 40 %) on a generated lead", 16.5,
            preset(21, VK_PRESET_LEAD, {{VK_P_GLOBAL_TEMPO, 118}, {GEN, 1}, {GEN + 4, 55}, {GEN + 5, 45}, {GEN + 11, 40}, {GEN + 14, 56}}), {});
    section("5 Generative broken chords (D major) on a generated keys sound", 16.5,
            preset(5, VK_PRESET_KEYS, {{VK_P_GLOBAL_TEMPO, 118}, {GEN, 1}, {GEN + 1, 4}, {GEN + 2, 1}, {GEN + 3, 2}, {GEN + 4, 70}, {GEN + 5, 55}}), {});
}

// Effect slot settings for a demo section: type, mix, bus and real-unit parameter values.
struct FxSet { uint32_t slot, type; float mix; uint32_t bus; std::vector<std::pair<uint32_t, float>> p; };
static std::vector<std::pair<uint64_t, float>> fx(std::vector<FxSet> slots, std::vector<std::pair<uint64_t, float>> extra = {}) {
    std::vector<std::pair<uint64_t, float>> out;
    for (auto& s : slots) {
        const uint64_t b = (39 + s.slot) * 256ull;
        out.push_back({b, static_cast<float>(s.type)}); out.push_back({b + 2, s.mix}); out.push_back({b + 4, static_cast<float>(s.bus)});
        VKFxTypeInfo ti; vk_fx_type_info(s.type, &ti);
        for (uint32_t k = 0; k < 12; ++k) { VKFxParamInfo pi; vk_fx_param_info(s.type, k, &pi); if (pi.name) out.push_back({b + 5 + k, pi.defaultNormalised}); }
        for (auto& [k, v] : s.p) out.push_back({b + 5 + k, vk_fx_unmap_value(s.type, k, v)});
    }
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}
static std::vector<std::pair<uint64_t, float>> cat(std::vector<std::pair<uint64_t, float>> a, const std::vector<std::pair<uint64_t, float>>& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

static void phase6Demo() {
    constexpr uint64_t BUS = 47 * 256, ARP = 36 * 256, SEQ = 37 * 256, GEN = 38 * 256;
    const std::vector<std::pair<uint64_t, float>> lead = {{VK_P_GLOBAL_TEMPO, 112}, {VK_P_VCO2_LEVEL, 70}, {VK_P_VCO2_FINE, 8}, {VK_P_VCFB_CUTOFF, 1600},
        {VK_P_VCFB_ENV_AMOUNT, 2}, {VK_P_GLOBAL_VOICE_MODE, 2}, {VK_P_GLOBAL_GLIDE, 60}, {VK_P_GLOBAL_MASTER_VOLUME, -8}};
    section("1 Tape echo insert + spring reverb on send A", 9.0,
            cat(lead, fx({{0, FxTapeEcho, 35, 0, {{1, 330}, {2, 60}, {3, 3}, {4, 40}, {5, 30}}}, {1, FxSpring, 100, 1, {{0, 2.5f}}}}, {{BUS + 4, 45}})),
            seq({64, 67, 69, 71, 72, 0, 71, 67}, 0.5, 0.7, 100, 0, 2));
    section("2 Pad: ensemble insert, hall reverb send (4.5 s)", 10.0,
            cat({{VK_P_VCO2_LEVEL, 80}, {VK_P_VCO2_FINE, 7}, {VK_P_EG2_ATTACK, 700}, {VK_P_EG2_RELEASE, 2500}, {VK_P_VCFB_CUTOFF, 900}, {VK_P_VCFB_ENV_AMOUNT, 1},
                 {VK_P_GLOBAL_MASTER_VOLUME, -4}},
                fx({{0, FxEnsemble, 60, 0, {}}, {1, FxHall, 100, 1, {{1, 4.5f}, {2, 90}}}}, {{BUS + 4, 50}})),
            {{0, 50, 4.5, 90}, {0, 57, 4.5, 90}, {0, 60, 4.5, 90}, {0, 64, 4.5, 90}, {4.8, 48, 3.5, 90}, {4.8, 55, 3.5, 90}, {4.8, 62, 3.5, 90}, {4.8, 65, 3.5, 90}});
    section("3 Crunch: amp → 4×12 cabinet → compressor", 7.0,
            cat({{VK_P_VCO1_WAVE, 4}, {VK_P_VCO2_LEVEL, 60}, {VK_P_VCO2_COARSE, 7}, {VK_P_VCFB_CUTOFF, 3000}, {VK_P_GLOBAL_VOICE_MODE, 1}, {VK_P_GLOBAL_MASTER_VOLUME, -6}},
                fx({{0, FxAmp, 100, 0, {{0, 2}, {1, 70}, {3, 3}, {6, -8}}}, {1, FxCabinet, 100, 0, {}}, {2, FxCompressor, 100, 0, {{0, -20}, {1, 3}, {6, 1}}}})),
            seq({40, 40, 43, 40, 45, 40, 47, 45}, 0.25, 0.6, 115, 0, 3));
    section("4 Arp through phaser + ping-pong delay (synced)", 9.0,
            cat({{VK_P_GLOBAL_TEMPO, 112}, {ARP, 1}, {ARP + 2, 1}, {ARP + 4, 4}, {VK_P_VCFB_CUTOFF, 1200}, {VK_P_VCFB_RESONANCE, 40}, {VK_P_EG1_DECAY, 180},
                 {VK_P_GLOBAL_MASTER_VOLUME, 2}},
                fx({{0, FxPhaser, 50, 0, {{0, 2}, {1, 0.2f}, {5, 60}}}, {1, FxPingPong, 35, 0, {{0, 11}, {2, 50}}}})),
            {{0, 57, 8.5, 100}, {0, 60, 8.5, 100}, {0, 64, 8.5, 100}});
    section("5 Spectral shimmer (+12 st, blur) and granular cloud on a sequence", 10.0,
            cat({{VK_P_GLOBAL_TEMPO, 112}, {SEQ, 1}, {SEQ + 1, 7}, {SEQ + 3, 3}, {SEQ + 7, 11}, {SEQ + 8, 9}, {VK_P_EG2_RELEASE, 400}, {VK_P_GLOBAL_MASTER_VOLUME, -3}},
                cat(fx({{0, FxSpectral, 100, 1, {{1, 85}, {2, 12}}}, {1, FxGranular, 100, 2, {{0, 120}, {1, 25}, {4, 300}, {5, 200}}}, {2, FxPlate, 100, 1, {}}},
                       {{BUS + 4, 40}, {BUS + 5, 45}}),
                    [] { std::vector<std::pair<uint64_t, float>> v; const float a[8] = {0, 25, 42, 50, 58, 50, 42, 17}; for (int s = 0; s < 8; ++s) v.push_back({SEQ + 32 + static_cast<uint64_t>(s) * 8, a[s]}); return v; }())),
            {});
    section("6 Generative melody: stutter + lo-fi, brick-wall limiter", 10.0,
            cat({{VK_P_GLOBAL_TEMPO, 112}, {GEN, 1}, {GEN + 4, 70}, {VK_P_VCFB_CUTOFF, 2500}, {VK_P_GLOBAL_MASTER_VOLUME, -4}},
                fx({{0, FxStutter, 100, 0, {{0, 2}, {1, 40}, {2, 1}, {4, 30}}}, {1, FxLoFi, 60, 0, {{0, 8}, {1, 11000}}}, {2, FxLimiter, 100, 0, {{0, 6}, {1, -1}}}})),
            {});
}

// Phase 7: raw-event sections (MIDI 1.0 bytes and MIDI 2.0 UMP) with an
// optional host transport, so clock sync, learn, RPN and UMP are audible.
using EventGen = std::function<void(VKKernel*, uint64_t blockStart, std::vector<VKEvent>&)>;
static VKEvent m1(uint32_t off, uint8_t a, uint8_t b = 0, uint8_t c = 0, uint8_t len = 3) {
    VKEvent e{}; e.type = VK_EVENT_MIDI1; e.sampleOffset = off; e.length = len; e.data[0] = a; e.data[1] = b; e.data[2] = c; return e;
}
static VKEvent u2(uint32_t off, uint32_t w0, uint32_t w1) { VKEvent e{}; e.type = VK_EVENT_UMP; e.sampleOffset = off; e.ump[0] = w0; e.ump[1] = w1; return e; }
static void sectionEv(const char* name, double secs, const std::vector<std::pair<uint64_t, float>>& params, const EventGen& gen,
                      const std::vector<VKMidiMapping>& maps = {}, std::function<void(VKKernel*, uint64_t)> transport = {}) {
    std::printf("  %-34s %.1f s\n", name, secs);
    VKKernel* k = vk_kernel_create();
    for (auto& [a, v] : params) vk_kernel_set_param(k, a, v);
    vk_kernel_allocate(k, 48000, 2, 256);
    if (!maps.empty()) vk_kernel_set_midi_mappings(k, maps.data(), static_cast<uint32_t>(maps.size()));
    std::vector<float> l(256), r(256); float* ch[2] = {l.data(), r.data()};
    std::vector<VKEvent> ev;
    const int blocks = static_cast<int>(secs * 48000 / 256);
    for (int b = 0; b < blocks; ++b) {
        ev.clear();
        gen(k, static_cast<uint64_t>(b) * 256, ev);
        std::stable_sort(ev.begin(), ev.end(), [](auto& a, auto& c) { return a.sampleOffset < c.sampleOffset; });
        if (transport) transport(k, static_cast<uint64_t>(b) * 256);
        vk_kernel_render(k, ch, 2, 256, ev.data(), static_cast<uint32_t>(ev.size()));
        L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end());
    }
    vk_kernel_destroy(k);
    L.insert(L.end(), 24000, 0.f); R.insert(R.end(), 24000, 0.f);
}
// Events due in [t, t + 256) at absolute sample s.
static void at(std::vector<VKEvent>& ev, uint64_t t, uint64_t s, VKEvent e) { if (s >= t && s < t + 256) { e.sampleOffset = static_cast<uint32_t>(s - t); ev.push_back(e); } }
static uint64_t sec(double x) { return static_cast<uint64_t>(x * 48000); }

static void phase7Demo() {
    constexpr uint64_t ARP = 36 * 256, GEN = 38 * 256;
    const std::vector<std::pair<uint64_t, float>> pluck = {{VK_P_VCFB_CUTOFF, 1100}, {VK_P_VCFB_RESONANCE, 50}, {VK_P_VCFB_ENV_AMOUNT, 3},
        {VK_P_EG1_DECAY, 150}, {VK_P_EG1_SUSTAIN, 5}, {VK_P_VCO2_LEVEL, 60}, {VK_P_VCO2_FINE, 6}, {VK_P_GLOBAL_MASTER_VOLUME, -1}};

    // 1 — the arpeggiator slaved to incoming MIDI clock: 100 BPM, then the
    // clock speeds up to 132 BPM; Start, Stop, Song Position (bar 3), Continue.
    {
        auto p = pluck; p.push_back({ARP, 1}); p.push_back({ARP + 2, 1}); p.push_back({ARP + 4, 4});
        double next = 0; int ticks = 0;
        sectionEv("1 Arp on MIDI clock 100 → 132 BPM, stop, SPP, continue", 11.0, p, [&](VKKernel*, uint64_t t, std::vector<VKEvent>& ev) {
            if (t == 0) { ev.push_back(m1(0, 0x90, 57, 100)); ev.push_back(m1(0, 0x90, 60, 100)); ev.push_back(m1(0, 0x90, 64, 100)); ev.push_back(m1(0, 0xFA, 0, 0, 1)); }
            at(ev, t, sec(6.0), m1(0, 0xFC, 0, 0, 1));
            at(ev, t, sec(6.8), m1(0, 0xF2, 32, 0, 3));          // song position: 32 sixteenths = beat 8
            at(ev, t, sec(7.0), m1(0, 0xFB, 0, 0, 1));
            while (next < static_cast<double>(t + 256)) {
                const double s = next;
                if (s >= static_cast<double>(t)) ev.push_back(m1(static_cast<uint32_t>(s - static_cast<double>(t)), 0xF8, 0, 0, 1));
                const double bpm = s < sec(3.0) ? 100.0 : std::min(132.0, 100.0 + (s - sec(3.0)) / 48000.0 * 32.0);
                next += 48000.0 * 60.0 / (bpm * 24.0); ++ticks;
            }
        });
    }
    // 2 — MIDI Learn mappings driven by a controller: CC 74 (absolute) sweeps
    // the cutoff on its taper, an endless encoder (relative, two's
    // complement) walks the resonance, a pad note toggles the delay-like
    // second oscillator level.
    {
        VKMidiMapping cut{VK_MIDI_SRC_CC, VK_MIDI_ANY_CHANNEL, 74, VK_P_VCFB_CUTOFF, 0.15f, 0.85f, VK_MIDI_MODE_ABSOLUTE, 0, false, 1.f / 128};
        VKMidiMapping res{VK_MIDI_SRC_CC, VK_MIDI_ANY_CHANNEL, 16, VK_P_VCFB_RESONANCE, 0.f, 0.9f, VK_MIDI_MODE_RELATIVE, VK_MIDI_REL_TWOS_COMPLEMENT, false, 1.f / 64};
        VKMidiMapping tog{VK_MIDI_SRC_NOTE, 9, 36, VK_P_VCO2_COARSE, 0.5f, 0.79f, VK_MIDI_MODE_TOGGLE, 0, false, 0};
        auto p = pluck; p.push_back({VK_P_EG1_DECAY, 400}); p.push_back({VK_P_GLOBAL_VOICE_MODE, 2}); p.push_back({VK_P_GLOBAL_GLIDE, 30});
        sectionEv("2 MIDI Learn: CC 74 cutoff, encoder resonance, pad toggle", 9.0, p, [&](VKKernel*, uint64_t t, std::vector<VKEvent>& ev) {
            const int bass[8] = {36, 48, 36, 43, 36, 46, 36, 41};
            for (int i = 0; i < 72; ++i) { at(ev, t, sec(i * 0.125), m1(0, 0x90, static_cast<uint8_t>(bass[i % 8]), 105)); at(ev, t, sec(i * 0.125 + 0.1), m1(0, 0x80, static_cast<uint8_t>(bass[i % 8]))); }
            if (t % 1024 == 0) {
                const double x = static_cast<double>(t) / 48000.0;
                ev.push_back(m1(0, 0xB0, 74, static_cast<uint8_t>(std::lround(63.5 + 63.5 * std::sin(x * 1.6)))));
                if (x > 2.0 && x < 5.0) ev.push_back(m1(0, 0xB0, 16, 1));        // encoder turned up
                if (x > 6.5 && x < 8.0) ev.push_back(m1(0, 0xB0, 16, 126));      // and back down, faster
            }
            at(ev, t, sec(4.5), m1(0, 0x99, 36, 100)); at(ev, t, sec(4.6), m1(0, 0x89, 36));   // toggle on (fifth up)
            at(ev, t, sec(7.0), m1(0, 0x99, 36, 100)); at(ev, t, sec(7.1), m1(0, 0x89, 36));   // and off
        }, {cut, res, tog});
    }
    // 3 — RPN 0 sets a 12-semitone bend range, then wide bends.
    sectionEv("3 RPN bend range 12 st, octave bends", 5.0,
              {{VK_P_VCO2_LEVEL, 80}, {VK_P_VCO2_FINE, 9}, {VK_P_VCFB_CUTOFF, 2200}, {VK_P_GLOBAL_VOICE_MODE, 1}, {VK_P_GLOBAL_MASTER_VOLUME, -9}},
              [](VKKernel*, uint64_t t, std::vector<VKEvent>& ev) {
        if (t == 0) { ev.push_back(m1(0, 0xB0, 101, 0)); ev.push_back(m1(0, 0xB0, 100, 0)); ev.push_back(m1(0, 0xB0, 6, 12)); ev.push_back(m1(0, 0xB0, 38, 0));
                      ev.push_back(m1(0, 0xB0, 101, 127)); ev.push_back(m1(0, 0xB0, 100, 127)); ev.push_back(m1(1, 0x90, 60, 100)); }
        const double x = static_cast<double>(t) / 48000.0;
        const int v = static_cast<int>(std::lround(8192 + 8191 * std::sin(x * 2.4) * std::min(1.0, x)));
        ev.push_back(m1(2, 0xE0, static_cast<uint8_t>(v & 127), static_cast<uint8_t>(v >> 7)));
        at(ev, t, sec(4.6), m1(0, 0x80, 60));
    });
    // 4 — MIDI 2.0: 16-bit velocity ramp (a smooth crescendo a 7-bit
    // keyboard can't make), then a chord where one note alone bends
    // (per-note pitch bend) and its timbre opens (per-note controller 74).
    sectionEv("4 MIDI 2.0: 16-bit velocity, per-note bend & timbre", 9.0,
              {{VK_P_EG1_VELOCITY, 80}, {VK_P_EG2_VELOCITY, 50}, {VK_P_VCFB_ENV_AMOUNT, 3}, {VK_P_VCFB_CUTOFF, 900}, {VK_P_EG1_DECAY, 300}, {VK_P_EG1_SUSTAIN, 20},
               {VK_P_VCO2_LEVEL, 70}, {VK_P_VCO2_FINE, 7}, {VK_P_GLOBAL_MPE_PITCH_RANGE, 12}, {VK_P_GLOBAL_MASTER_VOLUME, -6},
               {60 * 256 + 0, 1}, {60 * 256 + 1, 21}, {60 * 256 + 3, VK_P_VCFB_CUTOFF}, {60 * 256 + 4, 60}},
              [](VKKernel*, uint64_t t, std::vector<VKEvent>& ev) {
        auto note = [](uint32_t op, uint32_t n) { return 0x40000000u | op << 20 | n << 8; };
        for (int i = 0; i < 16; ++i) {
            const uint32_t vel = static_cast<uint32_t>(9000 + i * 3500);
            at(ev, t, sec(i * 0.25), u2(0, note(9, 64), vel << 16)); at(ev, t, sec(i * 0.25 + 0.2), u2(0, note(8, 64), 0));
        }
        for (uint32_t n : {57u, 60u, 64u}) { at(ev, t, sec(4.5), u2(0, note(9, n), 0xB000u << 16)); at(ev, t, sec(8.6), u2(0, note(8, n), 0)); }
        if (t >= sec(5.0) && t < sec(8.0) && t % 512 == 0) {
            const double x = (static_cast<double>(t) - sec(5.0)) / sec(3.0);
            const uint32_t bend = static_cast<uint32_t>(2147483648.0 + 2147483647.0 * (5.0 / 12.0) * std::sin(x * 3.14159265));   // up a fourth and back
            ev.push_back(u2(0, note(6, 64), bend));
            ev.push_back(u2(1, note(1, 64) | 74u, static_cast<uint32_t>(x * 4294967295.0)));
        }
    });
    // 5 — host time signature 3/4: a generative waltz phrase on bar lines of the host's meter.
    sectionEv("5 Host in 3/4: generative waltz", 12.0,
              preset(9, VK_PRESET_KEYS, {{GEN, 1}, {GEN + 1, 4}, {GEN + 4, 60}, {GEN + 8, 1}, {GEN + 2, 0}, {GEN + 3, 7}}),
              [](VKKernel*, uint64_t, std::vector<VKEvent>&) {}, {},
              [](VKKernel* k, uint64_t t) {
        const double beat = static_cast<double>(t) / 48000.0 * 132.0 / 60.0;
        vk_kernel_set_transport_ex(k, 132, beat, true, std::floor(beat / 3.0) * 3.0, 3, 4);
    });
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "vektur_phase2_demo.wav";
    if (argc > 2 && std::strcmp(argv[2], "--phase3") == 0) { std::puts("Rendering Phase 3 patching demo:"); phase3Demo(); goto write; }
    if (argc > 2 && std::strcmp(argv[2], "--phase7") == 0) { std::puts("Rendering Phase 7 MIDI demo:"); phase7Demo(); goto write; }
    if (argc > 2 && std::strcmp(argv[2], "--phase6") == 0) { std::puts("Rendering Phase 6 effects demo:"); phase6Demo(); goto write; }
    if (argc > 2 && std::strcmp(argv[2], "--phase5") == 0) { std::puts("Rendering Phase 5 sequencing & generative demo:"); phase5Demo(); goto write; }
    if (argc > 2 && std::strcmp(argv[2], "--phase4b") == 0) { std::puts("Rendering Phase 4B performance demo:"); phase4bDemo(); goto write; }
    if (argc > 2 && std::strcmp(argv[2], "--phase4") == 0) { std::puts("Rendering Phase 4 modulation demo:"); phase4Demo(); goto write; }
    std::puts("Rendering:");
    section("1 Ladder bass (default patch)", 4.2, {{VK_P_VCFB_RESONANCE, 45}, {VK_P_VCFB_CUTOFF, 300}, {VK_P_VCFB_ENV_AMOUNT, 4}, {VK_P_EG1_DECAY, 180}, {VK_P_EG1_SUSTAIN, 0}, {VK_P_GLOBAL_POLYPHONY, 0}, {VK_P_VCO1_SUB_LEVEL, 50}, {VK_P_GLOBAL_MASTER_VOLUME, -3}},
            seq({36, 36, 48, 36, 39, 36, 46, 43}, 0.125, 0.6, 110, 0, 4));
    section("2 Hard-sync lead, sync sweep", 4.5, {{VK_P_VCO1_LEVEL, 0}, {VK_P_VCO2_LEVEL, 100}, {VK_P_VCO2_SYNC_MODE, 1}, {VK_P_GLOBAL_FILTER_ROUTING, 0}, {VK_P_VCFA_CUTOFF, 5000}, {VK_P_GLOBAL_GLIDE, 60}, {VK_P_GLOBAL_POLYPHONY, 0}, {VK_P_GLOBAL_STEREO_SPREAD, 0}},
            seq({60, 63, 67, 70, 72, 70, 67, 63}, 0.5, 0.95, 100),
            [](VKKernel* k, double t) { vk_kernel_set_param(k, VK_P_VCO2_COARSE, static_cast<float>(12 * (0.5 + 0.5 * std::sin(t * 2.2)))); });
    section("3 Self-oscillating ladder sweep", 5.0, {{VK_P_VCFB_RESONANCE, 92}, {VK_P_VCFB_ENV_AMOUNT, 0}, {VK_P_VCFB_DRIVE, 6}, {VK_P_VCO1_WAVE, 4}},
            seq({33}, 4.8, 1.0, 100),
            [](VKKernel* k, double t) { vk_kernel_set_param(k, VK_P_VCFB_CUTOFF, static_cast<float>(80 * std::pow(60.0, 0.5 - 0.5 * std::cos(t * 1.3)))); });
    section("4 Sample & hold filter (S&H → Filter A)", 4.5, {{VK_P_GLOBAL_FILTER_ROUTING, 0}, {VK_P_VCFA_MODE, 1}, {VK_P_VCFA_RESONANCE, 75}, {VK_P_VCFA_CUTOFF, 900}, {VK_P_VCFA_SH_AMOUNT, 2.5f}, {VK_P_SAMPLEHOLD_RATE, 8}, {VK_P_VCO2_LEVEL, 70}, {VK_P_VCO2_FINE, 9}},
            seq({48}, 4.3, 1.0, 100));
    section("5 Analogue pad, Authentic drift", 7.5, {{VK_P_VCO2_LEVEL, 80}, {VK_P_VCO2_FINE, 8}, {VK_P_VCO3_LEVEL, 70}, {VK_P_VCO3_FINE, -7}, {VK_P_VCO3_OCTAVE, 2}, {VK_P_EG2_ATTACK, 900}, {VK_P_EG2_RELEASE, 2200}, {VK_P_EG1_ATTACK, 1500}, {VK_P_EG1_SUSTAIN, 60}, {VK_P_VCFB_ENV_AMOUNT, 2}, {VK_P_VCFB_CUTOFF, 700}, {VK_P_GLOBAL_INSTABILITY, 70}, {VK_P_GLOBAL_STEREO_SPREAD, 90}, {VK_P_GLOBAL_MASTER_VOLUME, -12}},
            {{0, 48, 5, 90}, {0, 55, 5, 90}, {0, 60, 5, 90}, {0, 63, 5, 90}, {0, 67, 5, 90}});
    section("6 Ring-mod bells", 5.0, {{VK_P_VCO1_LEVEL, 0}, {VK_P_RINGMOD_LEVEL, 100}, {VK_P_VCO1_WAVE, 0}, {VK_P_VCO2_WAVE, 0}, {VK_P_VCO2_COARSE, 7}, {VK_P_VCO2_FINE, 23}, {VK_P_EG2_DECAY, 1400}, {VK_P_EG2_SUSTAIN, 0}, {VK_P_EG2_RELEASE, 1400}, {VK_P_GLOBAL_FILTER_ROUTING, 0}, {VK_P_VCFA_CUTOFF, 9000}, {VK_P_GLOBAL_STEREO_SPREAD, 80}},
            seq({72, 79, 76, 84, 0, 79}, 0.6, 0.3, 110));
    section("7 Noise percussion + VCO3 pitch mod", 4.0, {{VK_P_VCO1_LEVEL, 30}, {VK_P_NOISE_LEVEL, 100}, {VK_P_NOISE_TYPE, 1}, {VK_P_EG2_ATTACK, 0.5f}, {VK_P_EG2_DECAY, 140}, {VK_P_EG2_SUSTAIN, 0}, {VK_P_EG2_RELEASE, 140}, {VK_P_EG1_PITCH_AMOUNT, 18}, {VK_P_EG1_DECAY, 60}, {VK_P_EG1_SUSTAIN, 0}, {VK_P_VCFB_CUTOFF, 2500}, {VK_P_VCFB_ENV_AMOUNT, 2}},
            seq({36, 0, 60, 36, 36, 0, 60, 0}, 0.125, 0.4, 120, 0, 4));

write:
    // 24-bit WAV
    FILE* f = std::fopen(path, "wb");
    const uint32_t n = static_cast<uint32_t>(L.size()), bytes = n * 6;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(48000); u32(48000 * 6); u16(6); u16(24);
    std::fwrite("data", 1, 4, f); u32(bytes);
    float peak = 0;
    for (uint32_t i = 0; i < n; ++i) for (float s : {L[i], R[i]}) {
        peak = std::max(peak, std::fabs(s));
        const int32_t v = static_cast<int32_t>(std::lround(std::max(-1.f, std::min(1.f, s)) * 8388607.f));
        std::fwrite(&v, 3, 1, f);
    }
    std::fclose(f);
    std::printf("Wrote %s (%.1f s, peak %.1f dBFS)\n", path, n / 48000.0, 20 * std::log10(peak));
}
