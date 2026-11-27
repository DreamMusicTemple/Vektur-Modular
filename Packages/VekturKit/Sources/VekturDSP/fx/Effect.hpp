// Effect.hpp — interface of one effect instance. Instances are built on the
// control thread (makeEffect: allocates everything the type needs at the
// current sample rate) and handed to the audio thread through the effects
// engine's lock-free slot exchange. process() is real-time safe.
#pragma once

#include "FxSpecs.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace vektur::fx {

struct FxContext {
    float sampleRate = 48000.f;
    double tempo = 120.0;
    double beat = 0.0;        // generators' clock at the start of this span
    bool playing = false;
    double beatsPerBar = 4.0; // time signature (Phase 7)
};

// Mapped parameter values (real units; choices as indices).
struct FxParams { float v[12]{}; };

class Effect {
public:
    virtual ~Effect() = default;
    uint32_t fxType = 0;   // set by makeEffect
    // Processes a stereo span in place, 100 % wet (the engine mixes dry/wet).
    virtual void process(float* L, float* R, uint32_t n, const FxParams& p, const FxContext& c) noexcept = 0;
    virtual void reset() noexcept = 0;
    // Fixed processing latency in samples (constant for the instance's life).
    virtual uint32_t latency() const noexcept { return 0; }
    // How long the output can continue after the input falls silent (s); < 0 = indefinitely.
    virtual float tailSeconds(const FxParams&) const noexcept { return 0.05f; }
    // Metering for the UI (dynamics: gain reduction in dB, ≤ 0). [any] relaxed read.
    virtual float meter() const noexcept { return 0.f; }
};

// A stereo impulse response at the engine sample rate.
struct ImpulseResponse {
    std::vector<float> left, right;
    float sampleRate = 48000.f;
    uint32_t generation = 0;   // changes when the user IR of a slot changes
};

// Built-in IR `index` (FxSpecs kIRs) rendered for `sampleRate`. Deterministic.
ImpulseResponse builtInImpulse(uint32_t index, float sampleRate);

// Builds an effect of `type` (nullptr for Empty/unknown). `structural` is the
// value of the type's structural parameter (convolution: IR choice); `userIR`
// is used when that choice is "User IR". [control]
std::unique_ptr<Effect> makeEffect(uint32_t type, float sampleRate, uint32_t maxFrames, int structural,
                                   const ImpulseResponse* userIR);

} // namespace vektur::fx
