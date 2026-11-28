// CAPI.cpp — C entry points declared in VekturDSP.h.
#include "VekturDSP.h"
#include "Kernel.hpp"
#include "ChordAnalyzer.hpp"

#include <new>
#include <algorithm>
#include <cmath>
#include <vector>

using vektur::Kernel;

extern "C" {

VKKernel* vk_kernel_create(void) {
    auto* k = new (std::nothrow) Kernel();
    return reinterpret_cast<VKKernel*>(k);
}

void vk_kernel_destroy(VKKernel* k) { delete reinterpret_cast<Kernel*>(k); }

bool vk_kernel_register_param(VKKernel* k, uint64_t address, float minValue, float maxValue,
                              float defaultValue, float smoothingMs) {
    if (!k) return false;
    return reinterpret_cast<Kernel*>(k)->registerParam({address, minValue, maxValue, defaultValue, smoothingMs});
}

int vk_kernel_allocate(VKKernel* k, double sampleRate, uint32_t channelCount, uint32_t maxFrames) {
    if (!k) return -100;
    return reinterpret_cast<Kernel*>(k)->allocate(sampleRate, channelCount, maxFrames);
}

void vk_kernel_deallocate(VKKernel* k) { if (k) reinterpret_cast<Kernel*>(k)->deallocate(); }

bool vk_kernel_is_allocated(const VKKernel* k) {
    return k && reinterpret_cast<const Kernel*>(k)->isAllocated();
}

void vk_kernel_set_param(VKKernel* k, uint64_t address, float value) {
    if (k) reinterpret_cast<Kernel*>(k)->setParam(address, value);
}

float vk_kernel_get_param(const VKKernel* k, uint64_t address) {
    return k ? reinterpret_cast<const Kernel*>(k)->getParam(address) : 0.f;
}

void vk_kernel_render(VKKernel* k, float* const* channels, uint32_t channelCount, uint32_t frames,
                      const VKEvent* events, uint32_t eventCount) {
    if (k) reinterpret_cast<Kernel*>(k)->render(channels, channelCount, frames, events, eventCount);
}

bool vk_kernel_post_command(VKKernel* k, VKCommand cmd) {
    return k && reinterpret_cast<Kernel*>(k)->postCommand(cmd);
}

bool vk_kernel_pop_telemetry(VKKernel* k, VKTelemetry* out) {
    return k && out && reinterpret_cast<Kernel*>(k)->popTelemetry(*out);
}

} // extern "C"

// ---------------------------------------------------------------------------
// Rack introspection & patching
// ---------------------------------------------------------------------------
#include "graph/Rack.hpp"

extern "C" {

uint32_t vk_rack_module_count(void) { return vektur::rack().count; }

bool vk_rack_module_info(uint32_t module, VKModuleInfo* out) {
    const auto& rk = vektur::rack();
    if (!out || module >= rk.count) return false;
    const auto& d = rk.inst[module];
    out->key = d.key; out->name = d.name;
    out->paramModule = d.paramModule == vektur::kNoParam ? -1 : static_cast<int32_t>(d.paramModule);
    // Hidden routing nodes report no ports so UIs never offer them.
    out->inputCount = d.hidden ? 0 : d.nIn;
    out->outputCount = d.hidden ? 0 : d.nOut;
    return true;
}

bool vk_rack_port_info(uint32_t module, uint32_t port, bool input, VKPortInfo* out) {
    const auto& rk = vektur::rack();
    if (!out || module >= rk.count) return false;
    const auto& d = rk.inst[module];
    if (port >= (input ? d.nIn : d.nOut)) return false;
    const auto& p = input ? d.in[port] : d.out[port];
    out->key = p.key; out->name = p.name; out->kind = static_cast<uint8_t>(p.kind);
    out->attenuatorAddress = (input && d.atten[port].address != vektur::kNoAddress) ? static_cast<int64_t>(d.atten[port].address) : -1;
    return true;
}

uint32_t vk_rack_normal_count(void) { return vektur::rack().normalCount; }

bool vk_rack_normal_info(uint32_t index, VKNormalInfo* out) {
    const auto& rk = vektur::rack();
    if (!out || index >= rk.normalCount) return false;
    const auto& n = rk.normals[index];
    *out = {n.srcInst, n.srcPort, n.dstInst, n.dstPort};
    return true;
}

int32_t vk_kernel_set_patch(VKKernel* k, const VKCable* cables, uint32_t count, VKPatchResult* result) {
    VKPatchResult local;
    if (!k) return -1;
    return reinterpret_cast<Kernel*>(k)->setPatch(cables, count, result ? *result : local);
}

float vk_kernel_connection_level(const VKKernel* k, uint32_t slot) {
    return k ? reinterpret_cast<const Kernel*>(k)->connectionLevel(slot) : 0.f;
}

} // extern "C"

// Phase 4B
extern "C" void vk_kernel_set_transport(VKKernel* k, double tempoBPM, double beatPosition, bool playing) {
    if (k) reinterpret_cast<vektur::Kernel*>(k)->setTransport(tempoBPM, beatPosition, playing);
}
extern "C" float vk_kernel_mod_slot_value(const VKKernel* k, uint32_t slot) {
    return k ? reinterpret_cast<const vektur::Kernel*>(k)->slotMeter(slot) : 0.f;
}
extern "C" void vk_kernel_held_notes(const VKKernel* k, uint32_t out[4]) {
    if (!out) return;
    if (!k) { for (int i = 0; i < 4; ++i) out[i] = 0; return; }
    reinterpret_cast<const vektur::Kernel*>(k)->heldNotes(out);
}
extern "C" int32_t vk_chord_name(const uint8_t* notes, uint32_t count, char* out, uint32_t capacity) {
    return vektur::chordName(notes, count, out, capacity);
}

// Phase 7 — MIDI
extern "C" void vk_kernel_set_midi_mappings(VKKernel* k, const VKMidiMapping* m, uint32_t n) {
    if (k) reinterpret_cast<vektur::Kernel*>(k)->setMidiMappings(m, m ? n : 0);
}
extern "C" void vk_kernel_midi_learn(VKKernel* k, bool armed) { if (k) reinterpret_cast<vektur::Kernel*>(k)->midiLearn(armed); }
extern "C" bool vk_kernel_pop_midi_ui_event(VKKernel* k, VKMidiUIEvent* out) {
    VKMidiUIEvent discard{};
    return k && reinterpret_cast<vektur::Kernel*>(k)->popMidiUi(out ? *out : discard);
}
extern "C" uint32_t vk_kernel_take_midi_changes(VKKernel* k, uint64_t* a, uint32_t cap) { return k ? reinterpret_cast<vektur::Kernel*>(k)->takeMidiChanges(a, a ? cap : 0) : 0; }
extern "C" void vk_kernel_midi_status(const VKKernel* k, VKMidiStatus* out) {
    if (!out) return;
    *out = VKMidiStatus{};
    if (k) reinterpret_cast<const vektur::Kernel*>(k)->midiStatus(*out);
}
extern "C" uint32_t vk_kernel_midi_output(const VKKernel* k, const VKMidiOutEvent** ev) {
    if (!k) { if (ev) *ev = nullptr; return 0; }
    return reinterpret_cast<const vektur::Kernel*>(k)->midiOutput(ev);
}
extern "C" void vk_kernel_set_transport_ex(VKKernel* k, double tempo, double beat, bool playing, double barStart, double num, double den) {
    if (k) reinterpret_cast<vektur::Kernel*>(k)->setTransportEx(tempo, beat, playing, barStart, num, den);
}
#if !defined(__APPLE__)
// Non-Apple builds (host tests): there is no AU block to call.
extern "C" void vk_au_set_midi_output_block(VKKernel* k, const void* block) { if (k) reinterpret_cast<vektur::Kernel*>(k)->setMidiOutBlock(block); }
#endif

// Phase 10 — FFT self-test (control thread; allocates)
#include "fx/FFT.hpp"
extern "C" float vk_fft_selftest(uint32_t n) {
    if (n < 8 || (n & (n - 1))) return 1.f;
    vektur::fx::FFT f(n);
    std::vector<float> x(n), re(n / 2 + 1), im(n / 2 + 1), y(n);
    uint32_t s = 12345;
    for (auto& v : x) { s = s * 1664525u + 1013904223u; v = static_cast<float>(s >> 8) / 8388608.f - 1.f; }
    f.forward(x.data(), re.data(), im.data());
    double err = 0, peak = 1e-9;
    for (uint32_t k = 0; k <= n / 2; ++k) {
        double dr = 0, di = 0;
        for (uint32_t t = 0; t < n; ++t) { const double a = -2.0 * 3.141592653589793 * k * t / n; dr += x[t] * std::cos(a); di += x[t] * std::sin(a); }
        peak = std::max(peak, std::sqrt(dr * dr + di * di));
        err = std::max(err, std::sqrt((dr - re[k]) * (dr - re[k]) + (di - im[k]) * (di - im[k])));
    }
    err /= peak;
    f.inverse(re.data(), im.data(), y.data());
    for (uint32_t t = 0; t < n; ++t) err = std::max(err, static_cast<double>(std::fabs(y[t] - x[t])));
    return static_cast<float>(err);
}

// Phase 9 — recording
extern "C" bool vk_kernel_record_start(VKKernel* k) { return k && reinterpret_cast<vektur::Kernel*>(k)->recorder().start(); }
extern "C" void vk_kernel_record_stop(VKKernel* k) { if (k) reinterpret_cast<vektur::Kernel*>(k)->recorder().stop(); }
extern "C" bool vk_kernel_is_recording(const VKKernel* k) { return k && const_cast<vektur::Kernel*>(reinterpret_cast<const vektur::Kernel*>(k))->recorder().isRecording(); }
extern "C" uint32_t vk_kernel_record_read(VKKernel* k, float* out, uint32_t maxFrames) { return k && out ? reinterpret_cast<vektur::Kernel*>(k)->recorder().read(out, maxFrames) : 0; }
extern "C" uint64_t vk_kernel_record_dropped(const VKKernel* k) { return k ? const_cast<vektur::Kernel*>(reinterpret_cast<const vektur::Kernel*>(k))->recorder().dropped() : 0; }
