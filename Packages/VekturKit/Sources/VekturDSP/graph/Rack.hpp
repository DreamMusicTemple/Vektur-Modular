// Rack.hpp — the fixed set of module instances, their ports, input
// attenuators and the normalled (default) connections.
//
// Instance order, instance keys and port keys are PERMANENT: saved patches
// refer to them by key, and parameter addresses are tied to instances.
// Append only.
#pragma once

#include "VekturDSP.h"
#include "../modules/DSPCommon.hpp"

#include <cstdint>

namespace vektur {

inline constexpr uint32_t kMaxPorts = 8;
inline constexpr uint32_t kMaxInstances = 48;
inline constexpr uint32_t kMaxNormals = 64;
inline constexpr uint32_t kNoParam = 0xFFFFFFFFu;
inline constexpr uint64_t kNoAddress = ~0ull;

enum class ModType : uint8_t {
    Keyboard, Vco, Noise, Ring, VoiceMixer, Sum, FilterSvf, FilterLadder, Vca, Envelope,
    Ota, Comb, Formant, Vcbpf, FilterBank, Switch, Mult, GateDelay, Mixer4, VoltageProc,
    Slew, Follower, SampleHold, Output, DiodeLadder, TransistorLadder, Lfo,
};

// Fixed instance ids (index into the rack).
enum Inst : uint8_t {
    I_KEYBOARD = 0, I_VCO1, I_VCO2, I_VCO3, I_VCO4, I_VCO5, I_VCO6, I_VCO7, I_VCO8, I_VCO9,
    I_NOISE, I_RING, I_MIXER, I_VCFA, I_VCFB, I_ROUTE_B, I_ROUTE_OUT, I_VCA1, I_VCA2, I_EG1, I_EG2,
    I_OTA, I_COMB, I_FORMANT, I_VCBPF, I_FBANK, I_SWITCH, I_MULT1, I_MULT2, I_GATE_DELAY, I_MIXER4,
    I_VPROC, I_SLEW, I_FOLLOWER, I_SH, I_OUTPUT,
    // Phase 3B — appended (instance order is permanent)
    I_DIODE, I_TLADDER,
    // Phase 4A — appended
    I_LFO1, I_LFO2, I_LFO3,
    I_COUNT
};
static_assert(I_COUNT <= kMaxInstances);

struct PortDesc { const char* key; const char* name; PortKind kind; };

// Input attenuator: effective input gain multiplier = param(address) * scale.
struct Atten { uint64_t address = kNoAddress; float scale = 1.f; };

struct InstanceDesc {
    const char* key = "";
    const char* name = "";
    ModType type = ModType::Sum;
    uint32_t paramModule = kNoParam;
    uint8_t nIn = 0, nOut = 0;
    PortDesc in[kMaxPorts]{};
    PortDesc out[kMaxPorts]{};
    Atten atten[kMaxPorts]{};
    bool hidden = false;   // internal routing node: not offered for patching
};

// Gain of a normalled connection, evaluated each control block.
struct GainSpec {
    enum class Kind : uint8_t { Const, Routing } kind = Kind::Const;
    float value = 1.f;
    float table[4] = {0.f, 0.f, 0.f, 0.f};   // Routing: gain for filter routing 0…3
};

struct NormalEdge { uint8_t srcInst, srcPort, dstInst, dstPort; GainSpec gain; };

struct Rack {
    InstanceDesc inst[kMaxInstances]{};
    uint32_t count = 0;
    NormalEdge normals[kMaxNormals]{};
    uint32_t normalCount = 0;

    int inPort(uint32_t i, const char* key) const noexcept;
    int outPort(uint32_t i, const char* key) const noexcept;
};

// Built once (first call — make it from a non-real-time thread).
const Rack& rack();

} // namespace vektur
