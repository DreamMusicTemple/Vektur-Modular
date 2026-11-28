// vk_web.cpp — browser glue over the plain-C kernel API (VekturDSP.h).
// The kernel itself is unchanged: the web test build runs the same DSP,
// MIDI engine, sequencers and effects as the app and the AUv3.
//
// JS owns one kernel per AudioWorklet. It writes events into the shared
// event array, calls vkw_render, and reads the two output buffers.
#include "VekturDSP.h"
#include <new>
#include <cstddef>

#define EXPORT(name) __attribute__((export_name(#name)))

namespace {
constexpr unsigned kMaxFrames = 512, kMaxEvents = 1024;
struct Web {
    VKKernel* k = nullptr;
    float* buf[2] = {nullptr, nullptr};
    VKEvent events[kMaxEvents];
    VKMidiMapping maps[VK_MIDI_MAX_MAPPINGS];
    VKTelemetry tel;
    VKMidiStatus midi;
    VKSequencerStatus seq;
    VKMidiUIEvent ui;
    char text[64];
    VKCable cables[VK_MAX_CABLE_ID];
    VKPatchResult patch;
    VKModuleInfo module;
    VKPortInfo port;
};
Web gWeb;
float gL[kMaxFrames], gR[kMaxFrames];
}

extern "C" {
EXPORT(vkw_init) int vkw_init(double sampleRate) {
    if (!gWeb.k) gWeb.k = vk_kernel_create();
    gWeb.buf[0] = gL; gWeb.buf[1] = gR;
    return vk_kernel_allocate(gWeb.k, sampleRate, 2, kMaxFrames);
}
EXPORT(vkw_kernel) VKKernel* vkw_kernel() { return gWeb.k; }
EXPORT(vkw_left) float* vkw_left() { return gL; }
EXPORT(vkw_right) float* vkw_right() { return gR; }
EXPORT(vkw_events) VKEvent* vkw_events() { return gWeb.events; }
EXPORT(vkw_event_size) unsigned vkw_event_size() { return sizeof(VKEvent); }
EXPORT(vkw_max_events) unsigned vkw_max_events() { return kMaxEvents; }
EXPORT(vkw_render) void vkw_render(unsigned frames, unsigned eventCount) {
    if (frames > kMaxFrames) frames = kMaxFrames;
    if (eventCount > kMaxEvents) eventCount = kMaxEvents;
    vk_kernel_render(gWeb.k, gWeb.buf, 2, frames, eventCount ? gWeb.events : nullptr, eventCount);
}
EXPORT(vkw_set) void vkw_set(double address, float v) { vk_kernel_set_param(gWeb.k, static_cast<uint64_t>(address), v); }
EXPORT(vkw_get) float vkw_get(double address) { return vk_kernel_get_param(gWeb.k, static_cast<uint64_t>(address)); }
EXPORT(vkw_command) int vkw_command(unsigned type, unsigned arg, float value) { VKCommand c{type, arg, value}; return vk_kernel_post_command(gWeb.k, c); }
EXPORT(vkw_transport) void vkw_transport(double tempo, double beat, int playing, double barStart, double num, double den) {
    vk_kernel_set_transport_ex(gWeb.k, tempo, beat, playing != 0, barStart, num, den);
}
EXPORT(vkw_update_effects) unsigned vkw_update_effects() { return vk_kernel_update_effects(gWeb.k); }
EXPORT(vkw_effects_latency) unsigned vkw_effects_latency() { return vk_kernel_effects_latency(gWeb.k); }
EXPORT(vkw_set_effect_type) void vkw_set_effect_type(unsigned slot, unsigned type) { vk_kernel_set_effect_type(gWeb.k, slot, type); }
EXPORT(vkw_effect_meter) float vkw_effect_meter(unsigned slot) { return vk_kernel_effect_meter(gWeb.k, slot); }
// Telemetry: returns 1 and fills the struct when one was pending.
EXPORT(vkw_telemetry) VKTelemetry* vkw_telemetry() { return vk_kernel_pop_telemetry(gWeb.k, &gWeb.tel) ? &gWeb.tel : nullptr; }
EXPORT(vkw_midi_status) VKMidiStatus* vkw_midi_status() { vk_kernel_midi_status(gWeb.k, &gWeb.midi); return &gWeb.midi; }
EXPORT(vkw_seq_status) VKSequencerStatus* vkw_seq_status() { vk_kernel_sequencer_status(gWeb.k, &gWeb.seq); return &gWeb.seq; }
EXPORT(vkw_midi_ui) VKMidiUIEvent* vkw_midi_ui() { return vk_kernel_pop_midi_ui_event(gWeb.k, &gWeb.ui) ? &gWeb.ui : nullptr; }
EXPORT(vkw_midi_learn) void vkw_midi_learn(int armed) { vk_kernel_midi_learn(gWeb.k, armed != 0); }
EXPORT(vkw_mappings) VKMidiMapping* vkw_mappings() { return gWeb.maps; }
EXPORT(vkw_mapping_size) unsigned vkw_mapping_size() { return sizeof(VKMidiMapping); }
EXPORT(vkw_publish_mappings) void vkw_publish_mappings(unsigned n) { vk_kernel_set_midi_mappings(gWeb.k, gWeb.maps, n); }
EXPORT(vkw_take_changes) unsigned vkw_take_changes(uint64_t* out, unsigned cap) { return vk_kernel_take_midi_changes(gWeb.k, out, cap); }
EXPORT(vkw_midi_out) unsigned vkw_midi_out(const VKMidiOutEvent** out) { return vk_kernel_midi_output(gWeb.k, out); }
EXPORT(vkw_mod_slot) float vkw_mod_slot(unsigned s) { return vk_kernel_mod_slot_value(gWeb.k, s); }
EXPORT(vkw_held) void vkw_held(uint32_t* out4) { vk_kernel_held_notes(gWeb.k, out4); }
EXPORT(vkw_cable_level) float vkw_cable_level(unsigned slot) { return vk_kernel_connection_level(gWeb.k, slot); }
EXPORT(vkw_fx_format) const char* vkw_fx_format(unsigned type, unsigned index, float v) {
    vk_fx_format_value(type, index, v, gWeb.text, sizeof gWeb.text); return gWeb.text;
}
EXPORT(vkw_chord) const char* vkw_chord(const uint8_t* notes, unsigned n) { vk_chord_name(notes, n, gWeb.text, sizeof gWeb.text); return gWeb.text; }
// Preset creator: writes values into the kernel directly.
EXPORT(vkw_generate_preset) unsigned vkw_generate_preset(unsigned seed, unsigned category) {
    static VKParamValue v[1024];
    const unsigned n = vk_preset_generate(seed, category, v, 1024, nullptr);
    for (unsigned i = 0; i < n; ++i) vk_kernel_set_param(gWeb.k, v[i].address, v[i].value);
    return n;
}
// Patching (Phase 3 API). JS fills vkw_cables() and publishes.
EXPORT(vkw_cables) VKCable* vkw_cables() { return gWeb.cables; }
EXPORT(vkw_cable_size) unsigned vkw_cable_size() { return sizeof(VKCable); }
EXPORT(vkw_set_patch) unsigned vkw_set_patch(unsigned n) {
    if (n > VK_MAX_CABLE_ID) n = VK_MAX_CABLE_ID;
    vk_kernel_set_patch(gWeb.k, gWeb.cables, n, &gWeb.patch);
    return gWeb.patch.rejected;
}
EXPORT(vkw_module_info) VKModuleInfo* vkw_module_info(unsigned m) { return vk_rack_module_info(m, &gWeb.module) ? &gWeb.module : nullptr; }
EXPORT(vkw_port_info) VKPortInfo* vkw_port_info(unsigned m, unsigned p, int input) { return vk_rack_port_info(m, p, input != 0, &gWeb.port) ? &gWeb.port : nullptr; }
// Effect catalogue for the UI instance.
EXPORT(vkw_fx_type_name) const char* vkw_fx_type_name(unsigned t) { VKFxTypeInfo i{}; return vk_fx_type_info(t, &i) ? i.name : nullptr; }
EXPORT(vkw_fx_type_family) const char* vkw_fx_type_family(unsigned t) { VKFxTypeInfo i{}; return vk_fx_type_info(t, &i) ? i.family : nullptr; }
EXPORT(vkw_fx_param_name) const char* vkw_fx_param_name(unsigned t, unsigned k) { VKFxParamInfo i{}; return vk_fx_param_info(t, k, &i) ? i.name : nullptr; }
EXPORT(vkw_fx_param_default) float vkw_fx_param_default(unsigned t, unsigned k) { VKFxParamInfo i{}; return vk_fx_param_info(t, k, &i) ? i.defaultNormalised : 0.f; }
EXPORT(vkw_fx_default_mix) float vkw_fx_default_mix(unsigned t) { VKFxTypeInfo i{}; return vk_fx_type_info(t, &i) ? i.defaultMix : 100.f; }
// Struct layout for JavaScript (offsets in bytes), so the page never hard-codes them.
#define O(T, f) static_cast<int>(offsetof(T, f))
static const int kLayout[] = {
    sizeof(VKEvent), O(VKEvent, sampleOffset), O(VKEvent, type), O(VKEvent, length), O(VKEvent, data), O(VKEvent, address), O(VKEvent, value), O(VKEvent, rampFrames), O(VKEvent, ump),
    sizeof(VKCable), O(VKCable, id), O(VKCable, srcModule), O(VKCable, srcPort), O(VKCable, dstModule), O(VKCable, dstPort), O(VKCable, gain),
    sizeof(VKMidiMapping), O(VKMidiMapping, source), O(VKMidiMapping, channel), O(VKMidiMapping, number), O(VKMidiMapping, address), O(VKMidiMapping, minimum),
        O(VKMidiMapping, maximum), O(VKMidiMapping, mode), O(VKMidiMapping, relative), O(VKMidiMapping, pickup), O(VKMidiMapping, sensitivity),
    sizeof(VKMidiOutEvent), O(VKMidiOutEvent, sampleOffset), O(VKMidiOutEvent, length), O(VKMidiOutEvent, data),
    O(VKMidiUIEvent, kind), O(VKMidiUIEvent, source), O(VKMidiUIEvent, channel), O(VKMidiUIEvent, bankMSB), O(VKMidiUIEvent, bankLSB), O(VKMidiUIEvent, number),
    O(VKMidiStatus, clockTempo), O(VKMidiStatus, clockRunning), O(VKMidiStatus, clockActive), O(VKMidiStatus, syncSource), O(VKMidiStatus, messagesIn),
        O(VKMidiStatus, messagesOut), O(VKMidiStatus, lastStatus),
    O(VKSequencerStatus, seqStep), O(VKSequencerStatus, arpNote), O(VKSequencerStatus, genBar), O(VKSequencerStatus, genPhrase), O(VKSequencerStatus, flags),
        O(VKSequencerStatus, rows), O(VKSequencerStatus, beat),
    O(VKTelemetry, peak), O(VKTelemetry, rms), O(VKTelemetry, activeVoices), O(VKTelemetry, renderNanos), O(VKTelemetry, flags),
    O(VKModuleInfo, key), O(VKModuleInfo, name), O(VKModuleInfo, paramModule), O(VKModuleInfo, inputCount), O(VKModuleInfo, outputCount),
    O(VKPortInfo, key), O(VKPortInfo, name), O(VKPortInfo, kind), O(VKPortInfo, attenuatorAddress), VK_MAX_CHANNELS,
};
EXPORT(vkw_layout) const int* vkw_layout() { return kLayout; }
EXPORT(vkw_layout_count) int vkw_layout_count() { return static_cast<int>(sizeof kLayout / sizeof kLayout[0]); }
}
