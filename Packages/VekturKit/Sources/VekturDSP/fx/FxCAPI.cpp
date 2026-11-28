// FxCAPI.cpp — Phase 6 C entry points: effect catalogue and slot control.
#include "VekturDSP.h"
#include "../Kernel.hpp"
#include "FxSpecs.hpp"

using vektur::Kernel;
namespace fx = vektur::fx;

static_assert(fx::FxTypeCount == 28, "keep in step with tools/params.py FX_TYPES");

extern "C" {

uint32_t vk_fx_type_count(void) { return fx::FxTypeCount; }

bool vk_fx_type_info(uint32_t type, VKFxTypeInfo* out) {
    if (!out || type >= fx::FxTypeCount) return false;
    const fx::TypeSpec& t = fx::typeSpec(type);
    *out = {t.name, t.family, t.defaultMix, fx::paramCount(type)};
    return true;
}

bool vk_fx_param_info(uint32_t type, uint32_t index, VKFxParamInfo* out) {
    if (!out || type >= fx::FxTypeCount || index >= 12) return false;
    const fx::ParamSpec& p = fx::typeSpec(type).p[index];
    *out = VKFxParamInfo{};
    if (!p.name) return true;
    out->name = p.name; out->unit = static_cast<uint8_t>(p.unit);
    out->isLog = p.curve == fx::Curve::Log; out->isChoice = p.curve == fx::Curve::Choice; out->isToggle = p.curve == fx::Curve::Toggle;
    out->structural = p.structural;
    out->minValue = p.min; out->maxValue = p.max; out->defaultValue = p.def;
    out->defaultNormalised = fx::unmapValue(p, p.def);
    out->choiceCount = p.choiceCount;
    return true;
}

const char* vk_fx_choice_name(uint32_t type, uint32_t index, uint32_t choice) {
    if (type >= fx::FxTypeCount || index >= 12) return "";
    const fx::ParamSpec& p = fx::typeSpec(type).p[index];
    return p.choices && choice < p.choiceCount ? p.choices[choice] : "";
}

float vk_fx_map_value(uint32_t type, uint32_t index, float n) {
    if (type >= fx::FxTypeCount || index >= 12 || !fx::typeSpec(type).p[index].name) return n;
    return fx::mapValue(fx::typeSpec(type).p[index], n);
}

float vk_fx_unmap_value(uint32_t type, uint32_t index, float v) {
    if (type >= fx::FxTypeCount || index >= 12 || !fx::typeSpec(type).p[index].name) return v;
    return fx::unmapValue(fx::typeSpec(type).p[index], v);
}

int32_t vk_fx_format_value(uint32_t type, uint32_t index, float n, char* out, uint32_t capacity) {
    if (!out || capacity == 0) return 0;
    out[0] = 0;
    if (type >= fx::FxTypeCount || index >= 12 || !fx::typeSpec(type).p[index].name) return 0;
    const fx::ParamSpec& p = fx::typeSpec(type).p[index];
    return fx::formatValue(p, fx::mapValue(p, n), out, capacity);
}

uint32_t vk_kernel_update_effects(VKKernel* k) { return k ? reinterpret_cast<Kernel*>(k)->updateEffects() : 0; }

void vk_kernel_set_effect_type(VKKernel* kk, uint32_t slot, uint32_t type) {
    if (!kk || slot >= VK_FX_SLOTS || type >= fx::FxTypeCount) return;
    Kernel& k = *reinterpret_cast<Kernel*>(kk);
    const fx::TypeSpec& t = fx::typeSpec(type);
    k.setParam(fx::EffectsEngine::slotAddr(slot, 0), static_cast<float>(type));
    k.setParam(fx::EffectsEngine::slotAddr(slot, 2), t.defaultMix);
    for (uint32_t i = 0; i < 12; ++i)
        k.setParam(fx::EffectsEngine::slotAddr(slot, 5 + i), t.p[i].name ? fx::unmapValue(t.p[i], t.p[i].def) : 0.5f);
    k.updateEffects();
}

void vk_kernel_set_effect_ir(VKKernel* k, uint32_t slot, const float* left, const float* right, uint32_t frames, double sampleRate) {
    if (k) reinterpret_cast<Kernel*>(k)->effects().setUserIR(slot, left, right, frames, static_cast<float>(sampleRate));
}
void vk_kernel_clear_effect_ir(VKKernel* k, uint32_t slot) { if (k) reinterpret_cast<Kernel*>(k)->effects().clearUserIR(slot); }
uint32_t vk_kernel_effect_ir_frames(const VKKernel* k, uint32_t slot) { return k ? reinterpret_cast<const Kernel*>(k)->effects().userIRFrames(slot) : 0; }
uint32_t vk_kernel_effects_latency(const VKKernel* k) { return k ? reinterpret_cast<const Kernel*>(k)->effects().latency() : 0; }
uint32_t vk_kernel_effect_active_type(const VKKernel* k, uint32_t slot) { return k ? reinterpret_cast<const Kernel*>(k)->effects().activeType(slot) : 0; }
float vk_kernel_effect_meter(const VKKernel* k, uint32_t slot) { return k ? reinterpret_cast<const Kernel*>(k)->effects().meter(slot) : 0.f; }

} // extern "C"
