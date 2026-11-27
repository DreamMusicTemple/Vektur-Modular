// GraphModules.hpp — every rack module as a block-processing graph node.
//
// Modules read already-attenuated input buffers (nullptr = unpatched) and
// write their output buffers. Blocks are ≤ VK_CONTROL_BLOCK (32) frames.
// Real-time safe: prepare() may allocate; nothing else does.
#pragma once

#include "Rack.hpp"
#include "../ParameterStore.hpp"
#include "../modules/DSPCommon.hpp"
#include "../modules/Envelope.hpp"
#include "../modules/Filters.hpp"
#include "../modules/Utilities.hpp"
#include "../modules/Vco.hpp"
#include "../modules/ModMatrix.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace vektur {

inline constexpr uint32_t kSubBlock = 8;   // pitch / coefficient update interval

// Shared by every voice for one control block.
struct GlobalContext {
    const ParameterStore* params = nullptr;
    double sampleRate = 48000.0;
    bool authentic = true;
    float instability = 0.f;      // 0 … 1
    float glideMs = 0.f;
    float pitchOffsetSemis = 0.f; // bend + master tune
    float stereoSpread = 0.f;
    int routing = 1;
    int oversampling = 2;
    float shGlobalValue = 0.f;    // global S&H output (shared noise source)
    int32_t shClockOffset = -1;   // frame within this block where the S&H clock fires, -1 = none
    float shNoise = 0.f;          // shared noise value sampled at that clock
    // Phase 4A
    const ModFrame* modf = nullptr;   // decoded matrix + global sources (kernel)
    const float* mod = nullptr;       // per-voice matrix offsets (set by VoiceGraph per voice)
    bool glideConstantTime = true;
    bool hqOscillators = false;   // Phase 10: VCOs run 2× oversampled
};

// Per-voice state visible to modules.
struct VoiceContext {
    uint32_t index = 0;
    int note = 60;
    float velocity = 0.f;
    bool gate = false;
    bool triggerPending = false;
    float glideFrom = 60.f;
    float thermalCents = 0.f;     // common-mode drift of this voice (unit std-dev)
    float tolCutoff = 0.f;        // per-voice cutoff tolerance (unit)
    float tolTime = 0.f;          // per-voice envelope time tolerance (unit)
    float* outL = nullptr;        // written by the Output module
    float* outR = nullptr;
    // Phase 4A
    float pitchModSemis = 0.f;    // matrix → pitch (control rate)
    float ampMod = 1.f;           // matrix → amplitude multiplier
    float panMod = 0.f;           // matrix → pan offset
    float detuneSemis = 0.f;      // unison detune of this voice
    float noteBendSemis = 0.f;    // MPE per-note pitch bend (Phase 4B)
    bool glide = true;            // this note may glide (glide mode)
    const float* lfo[3]{};        // per-sample LFO outputs for the current block
    uint32_t lfoOffset = 0;       // frame offset of the current sub-run within that block
};

struct Ports {
    const float* in[kMaxPorts]{};
    float* out[kMaxPorts]{};
    bool outUsed[kMaxPorts]{};
};

class Module {
public:
    virtual ~Module() = default;
    virtual void prepare(double sr, uint32_t voice, uint32_t paramModule) {
        sr_ = sr; voice_ = voice; base_ = paramModule == kNoParam ? 0 : static_cast<uint64_t>(paramModule) * VK_PARAMS_PER_MODULE;
    }
    virtual void reset() {}
    virtual void noteStart(const VoiceContext&, const GlobalContext&, bool /*fromIdle*/) {}
    virtual void process(const Ports& p, uint32_t n, VoiceContext& v, const GlobalContext& g) = 0;

protected:
    float P(const GlobalContext& g, uint32_t i) const noexcept { return g.params->modulated(base_ + i, g.mod); }
    int I(const GlobalContext& g, uint32_t i) const noexcept { return static_cast<int>(std::lround(g.params->get(base_ + i))); }
    bool B(const GlobalContext& g, uint32_t i) const noexcept { return g.params->get(base_ + i) >= 0.5f; }
    static float at(const float* b, uint32_t i, float fallback = 0.f) noexcept { return b ? b[i] : fallback; }

    double sr_ = 48000.0;
    uint32_t voice_ = 0;
    uint64_t base_ = 0;
};

// Rising-edge detector that also decodes sub-sample timing from VCO sync
// outputs (value 1 + fraction on the wrap sample).
struct TriggerIn {
    float prev = 0.f;
    bool step(float v, float& since) noexcept {
        const bool rise = prev < 0.5f && v >= 0.5f;
        prev = v;
        if (!rise) return false;
        since = (v >= 1.f && v < 2.f) ? clampf(v - 1.f, 0.f, 0.999f) : 0.f;
        return true;
    }
};

std::unique_ptr<Module> makeModule(ModType t, uint32_t instance);

} // namespace vektur
