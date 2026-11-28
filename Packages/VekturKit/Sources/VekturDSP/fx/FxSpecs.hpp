// FxSpecs.hpp — the effect catalogue: for every effect type, the meaning,
// range, curve, default and display of its 12 generic slot parameters.
// Type order is PERMANENT and mirrors tools/params.py FX_TYPES.
#pragma once

#include <cstdint>

namespace vektur::fx {

enum class Unit : uint8_t { None, Percent, Ms, Hz, Db, Semitones, Ratio, Degrees, Bits, Seconds, PerSecond, Multiplier, Choice, Toggle };
enum class Curve : uint8_t { Linear, Log, Choice, Toggle };

struct ParamSpec {
    const char* name = nullptr;      // nullptr = unused slot parameter
    Unit unit = Unit::None;
    Curve curve = Curve::Linear;
    float min = 0.f, max = 1.f, def = 0.f;    // real units (choices: index)
    const char* const* choices = nullptr;
    uint8_t choiceCount = 0;
    bool structural = false;         // change rebuilds the effect (not automatable in effect)
};

enum FxType : uint8_t {
    FxEmpty = 0, FxStereoDelay, FxPingPong, FxTapeEcho, FxBBD,
    FxPlate, FxHall, FxRoom, FxSpring, FxConvolution,
    FxParamEQ, FxGraphicEQ, FxSpectral, FxGranular, FxTape, FxStutter, FxLoFi,
    FxAmp, FxCabinet, FxChorus, FxFlanger, FxPhaser, FxEnsemble, FxTremolo,
    FxCompressor, FxLimiter, FxGate, FxWidth,
    FxTypeCount
};

struct TypeSpec {
    const char* name;
    const char* family;
    float defaultMix;                // % applied when the type is chosen
    ParamSpec p[12];
};

const TypeSpec& typeSpec(uint32_t type) noexcept;
uint32_t paramCount(uint32_t type) noexcept;

// Normalised 0…1 ↔ real value.
float mapValue(const ParamSpec& s, float normalised) noexcept;
float unmapValue(const ParamSpec& s, float value) noexcept;
// Display string ("350 ms", "1/8 dotted", "+3.0 dB"). Returns length.
int formatValue(const ParamSpec& s, float value, char* out, uint32_t cap) noexcept;

// Built-in impulse responses of the convolution reverb (choice order).
inline constexpr uint32_t kBuiltInIRCount = 8;   // + "User" as the last choice

} // namespace vektur::fx
