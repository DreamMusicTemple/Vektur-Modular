// EffectsInternal.hpp — factory entry points of each effect family and small
// shared helpers. Not part of the public interface.
#pragma once

#include "Effect.hpp"
#include "FxDSP.hpp"

namespace vektur::fx {

std::unique_ptr<Effect> makeDelayFamily(uint32_t type, float sr);
std::unique_ptr<Effect> makeReverbFamily(uint32_t type, float sr);
std::unique_ptr<Effect> makeConvolution(float sr, const ImpulseResponse& ir);
std::unique_ptr<Effect> makeEqFamily(uint32_t type, float sr);
std::unique_ptr<Effect> makeSpectral(float sr);
std::unique_ptr<Effect> makeGranular(float sr);
std::unique_ptr<Effect> makeCharacterFamily(uint32_t type, float sr);    // tape, lo-fi, amp, cabinet
std::unique_ptr<Effect> makeStutter(float sr);
std::unique_ptr<Effect> makeModulationFamily(uint32_t type, float sr);
std::unique_ptr<Effect> makeDynamicsFamily(uint32_t type, float sr);    // compressor, limiter, gate, width

// Mid/side width: 0 = mono, 1 = unchanged, 2 = double side.
inline void applyWidth(float& l, float& r, float width) noexcept {
    const float m = 0.5f * (l + r), s = 0.5f * (l - r) * width;
    l = m + s; r = m - s;
}

// Tail of a feedback loop: time for `fb` per pass to fall 60 dB.
inline float feedbackTail(float loopSeconds, float fb) noexcept {
    if (fb >= 0.999f) return -1.f;
    if (fb <= 1e-3f) return loopSeconds + 0.05f;
    return std::min(60.f, loopSeconds * (std::log(1e-3f) / std::log(fb)) + 0.05f);
}

} // namespace vektur::fx
