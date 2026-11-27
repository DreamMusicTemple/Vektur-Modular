// EffectFactory.cpp — makeEffect: one entry point for every effect type.
#include "EffectsInternal.hpp"

namespace vektur::fx {

namespace {
std::unique_ptr<Effect> create(uint32_t type, float sr, int structural, const ImpulseResponse* userIR) {
    switch (type) {
        case FxStereoDelay: case FxPingPong: case FxTapeEcho: case FxBBD: return makeDelayFamily(type, sr);
        case FxPlate: case FxHall: case FxRoom: case FxSpring: return makeReverbFamily(type, sr);
        case FxConvolution: {
            const auto choice = static_cast<uint32_t>(std::max(structural, 0));
            if (choice >= kBuiltInIRCount && userIR && !userIR->left.empty()) return makeConvolution(sr, *userIR);
            return makeConvolution(sr, builtInImpulse(std::min(choice, kBuiltInIRCount - 1), sr));
        }
        case FxParamEQ: case FxGraphicEQ: return makeEqFamily(type, sr);
        case FxSpectral: return makeSpectral(sr);
        case FxGranular: return makeGranular(sr);
        case FxTape: case FxLoFi: case FxAmp: case FxCabinet: return makeCharacterFamily(type, sr);
        case FxStutter: return makeStutter(sr);
        case FxChorus: case FxFlanger: case FxPhaser: case FxEnsemble: case FxTremolo: return makeModulationFamily(type, sr);
        case FxCompressor: case FxLimiter: case FxGate: case FxWidth: return makeDynamicsFamily(type, sr);
        default: return nullptr;
    }
}
} // namespace

std::unique_ptr<Effect> makeEffect(uint32_t type, float sr, uint32_t, int structural, const ImpulseResponse* userIR) {
    auto e = create(type, sr, structural, userIR);
    if (e) e->fxType = type;
    return e;
}

} // namespace vektur::fx
