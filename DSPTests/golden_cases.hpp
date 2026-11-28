// golden_cases.hpp — deterministic render cases shared by the golden generator
// (run once against the Phase 2 engine) and the regression test (run against
// every later engine). Instability is 0 so no randomness is involved.
#pragma once
#include "VekturDSP.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct GoldenNote { double t; int note; double len; int vel; };
struct GoldenCase {
    std::string name;
    std::vector<std::pair<uint64_t, float>> params;
    std::vector<GoldenNote> notes;
    double seconds = 0.6;
};

inline std::vector<GoldenCase> goldenCases() {
    const std::pair<uint64_t, float> stable = {VK_P_GLOBAL_INSTABILITY, 0.f};
    return {
        {"default", {stable}, {{0.0, 45, 0.4, 100}}},
        {"svf_lp_res", {stable, {VK_P_GLOBAL_FILTER_ROUTING, 0}, {VK_P_VCFA_RESONANCE, 60}, {VK_P_VCFA_CUTOFF, 800}, {VK_P_VCFA_ENV_AMOUNT, 2}}, {{0.0, 48, 0.4, 110}}},
        {"hard_sync", {stable, {VK_P_VCO1_LEVEL, 0}, {VK_P_VCO2_LEVEL, 100}, {VK_P_VCO2_SYNC_MODE, 1}, {VK_P_VCO2_COARSE, 7}, {VK_P_GLOBAL_FILTER_ROUTING, 0}, {VK_P_VCFA_CUTOFF, 6000}}, {{0.0, 57, 0.4, 100}}},
        {"fm", {stable, {VK_P_VCO1_LEVEL, 0}, {VK_P_VCO2_LEVEL, 100}, {VK_P_VCO2_FM_AMOUNT, 30}, {VK_P_VCO2_WAVE, 0}, {VK_P_VCO1_WAVE, 0}}, {{0.0, 52, 0.4, 100}}},
        {"a_to_b", {stable, {VK_P_GLOBAL_FILTER_ROUTING, 2}, {VK_P_VCFA_MODE, 2}, {VK_P_VCFA_CUTOFF, 200}, {VK_P_VCFB_RESONANCE, 70}}, {{0.0, 40, 0.4, 100}}},
        {"a_plus_b", {stable, {VK_P_GLOBAL_FILTER_ROUTING, 3}, {VK_P_VCFA_MODE, 1}, {VK_P_VCFA_RESONANCE, 80}}, {{0.0, 50, 0.4, 100}}},
        {"vco3_mod", {stable, {VK_P_VCO3_LOW_FREQ_MODE, 1}, {VK_P_VCO3_OCTAVE, 6}, {VK_P_VCO3_PITCH_MOD_AMOUNT, 2}, {VK_P_VCFB_VCO3_AMOUNT, 1.5f}, {VK_P_VCO3_WAVE, 1}}, {{0.0, 55, 0.5, 100}}},
        {"ring", {stable, {VK_P_VCO1_LEVEL, 0}, {VK_P_RINGMOD_LEVEL, 100}, {VK_P_VCO2_COARSE, 5}, {VK_P_VCO2_FINE, 17}, {VK_P_GLOBAL_FILTER_ROUTING, 0}, {VK_P_VCFA_CUTOFF, 9000}}, {{0.0, 60, 0.4, 100}}},
        {"chord_spread", {stable, {VK_P_GLOBAL_STEREO_SPREAD, 70}, {VK_P_VCO2_LEVEL, 60}, {VK_P_VCO2_FINE, 6}}, {{0.0, 48, 0.4, 90}, {0.0, 55, 0.4, 90}, {0.0, 64, 0.4, 90}}},
        {"glide_mono", {stable, {VK_P_GLOBAL_POLYPHONY, 0}, {VK_P_GLOBAL_GLIDE, 120}}, {{0.0, 48, 0.2, 100}, {0.2, 55, 0.2, 100}, {0.4, 43, 0.15, 100}}},
        {"eg_loop_linear", {stable, {VK_P_EG1_LOOP, 1}, {VK_P_EG1_ATTACK, 30}, {VK_P_EG1_DECAY, 40}, {VK_P_EG1_CURVE, 0}, {VK_P_EG2_CURVE, 2}, {VK_P_VCFB_ENV_AMOUNT, 4}}, {{0.0, 45, 0.45, 100}}},
        {"sub_pwm_svf_bp", {stable, {VK_P_VCO1_WAVE, 5}, {VK_P_VCO1_PULSE_WIDTH, 20}, {VK_P_VCO1_SUB_LEVEL, 70}, {VK_P_VCO1_SUB_OCTAVE, 1}, {VK_P_GLOBAL_FILTER_ROUTING, 0}, {VK_P_VCFA_MODE, 1}, {VK_P_VCFA_CUTOFF, 1500}}, {{0.0, 50, 0.4, 100}}},
    };
}

// Renders a case through the public C API. Returns interleaved L/R.
inline std::vector<float> renderGolden(const GoldenCase& c) {
    VKKernel* k = vk_kernel_create();
    for (auto& [a, v] : c.params) vk_kernel_set_param(k, a, v);
    vk_kernel_allocate(k, 48000, 2, 256);
    std::vector<float> l(256), r(256), out; float* ch[2] = {l.data(), r.data()};
    const int blocks = static_cast<int>(c.seconds * 48000 / 256);
    for (int b = 0; b < blocks; ++b) {
        const double t0 = b * 256 / 48000.0, t1 = (b + 1) * 256 / 48000.0;
        std::vector<VKEvent> ev;
        for (auto& n : c.notes) {
            auto push = [&](double t, uint8_t st, uint8_t vel) {
                if (t >= t0 && t < t1) { VKEvent e{}; e.type = VK_EVENT_MIDI1; e.length = 3; e.sampleOffset = static_cast<uint32_t>((t - t0) * 48000 + 1e-9);
                    e.data[0] = st; e.data[1] = static_cast<uint8_t>(n.note); e.data[2] = vel; ev.push_back(e); }
            };
            push(n.t, 0x90, static_cast<uint8_t>(n.vel)); push(n.t + n.len, 0x80, 0);
        }
        for (size_t i = 1; i < ev.size(); ++i) for (size_t j = i; j > 0 && ev[j - 1].sampleOffset > ev[j].sampleOffset; --j) std::swap(ev[j], ev[j - 1]);
        vk_kernel_render(k, ch, 2, 256, ev.data(), static_cast<uint32_t>(ev.size()));
        for (int i = 0; i < 256; ++i) { out.push_back(l[static_cast<size_t>(i)]); out.push_back(r[static_cast<size_t>(i)]); }
    }
    vk_kernel_destroy(k);
    return out;
}
