# Phase 3A Report — Modular Engine

Date: 2026-11-27 · Build 0.3.0 · State schema v1 (additive: `patch` section)

Phase 3 is split in two, each leaving the project working:
- **3A (this report):** the modular engine. Patch graph, cable routing, new modules, patch state, per-cable signal levels, and a list-based patch bay in the console.
- **3B (next):** the patch canvas. Drag-to-connect animated cables, zoom to 500 %, module layout, plus the diode-ladder and transistor-ladder filter variants.

**Listen:** `VekturModular-Phase3A-demo.m4a` (28 s): a plucked comb tracking the keyboard, a talking formant filter, a supersaw from the oscillator bank, a feedback patch, and gate-delayed filter sweeps. Every sound is a patch rendered through the real engine.

## What changed architecturally
- **Fixed rack.** Like a hardware cabinet, there are 36 module instances (34 patchable, plus 2 hidden routing nodes for the filter-routing switch). Instances are persistent per voice. Recompiling a patch never resets oscillator phases or filter states, so module **parameter addresses stay fixed** for host automation.
- **Normalled connections.** 45 default edges reproduce the Phase 2 voice exactly. A cable into an input replaces that input's normalled edge; outputs can fan out.
- **Input attenuators.** The Phase 2 level and amount knobs are now the attenuators of the jacks they feed (e.g. "VCO 1 level" scales mixer input 1), so they act on whatever you patch there.
- **Compiler (UI thread).** Validates cables, merges normals, keeps only modules upstream of the output or of a user cable, orders them topologically, and breaks cycles with a one-block (≤ 0.67 ms) delay plus a soft limiter. It writes plain data into a lock-free triple buffer; no allocation and no locks reach the audio thread.
- **Runtime.** Per block, effective edge gains and module activity are computed once for all voices. Modules whose outputs reach nothing through a non-zero gain are skipped. Gains ramp across the block, so turning a level knob never clicks.
- **Patch swap.** About 1.3 ms fade-out, then adopt the new graph, then fade in. This is click-free (tested over 40 rapid swaps) and cheaper than running two graphs side by side.
- **Telemetry.** Peak level per connection, max across voices, about every 4 blocks, readable from any thread. This drives the patch bay meters now and the cable glow in 3B.

## Files
**Created:**
- `graph/Rack.hpp/.cpp`, `graph/Graph.hpp/.cpp`, `graph/GraphModules.hpp/.cpp`, `graph/VoiceGraph.hpp/.cpp`
- `VekturCore/PatchState.swift`
- `VekturAudioUnit/RackCatalog.swift`, `VekturAudioUnit/PatchApplyResult.swift`
- `VekturUI/PatchBayView.swift`
- `DSPTests/test_graph.cpp`, `DSPTests/golden_cases.hpp`, `DSPTests/make_golden.cpp`, `DSPTests/golden/` (12 Phase 2 reference renders and a README)

**Modified:**
| File | Change | Impact |
|---|---|---|
| `Kernel.hpp/.cpp` | Voices are now `VoiceGraph`; graph exchange, runtime, swap fade, telemetry; `setPatch` | Same events, allocator, safety and telemetry as before |
| `VekturDSP.h` | Rack/patch C API | Additive |
| `modules/Filters.hpp` | Halfband without modulo (mirrored history) | Bit-identical output, much faster |
| `modules/Utilities.hpp` | `SampleHold::wrapOffset` | Additive |
| `tools/params.py` (+ generated files) | 135 new parameters (222 total) | Phase 1/2 addresses unchanged (generator asserts it) |
| `InstrumentState.swift` | `patch` section | Additive. Older saves load with an empty patch; a malformed patch section never breaks the rest of the state |
| `VekturModularAudioUnit.swift` | `applyPatch`, `currentPatch`, cable levels; patch restored from `fullState` | Additive |
| `DiagnosticsConsoleView.swift` | Patch bay section, new module groups | Dev console only |

**Deleted:** `SynthVoice.hpp/.cpp`, the Phase 2 fixed voice. It is superseded by the graph, which reproduces it (see Regression below). The Phase 2 release zip remains the reference for re-rendering goldens.

## New modules
| Module | Status |
|---|---|
| VCO 4–9 (oscillator bank, same VCO as 1–3, with FM/sync on every VCO) | **Implemented, tested** |
| OTA multimode filter (LP/BP/HP, both integrators saturating) | **Implemented, tested** |
| Comb filter (±feedback, damping, 1 V/oct CV, saturating loop) | **Implemented, tested** |
| Formant filter (A-E-I-O-U morph, resonance, shift, CV) | **Implemented, tested** |
| VC band-pass filter (Q 0.5–40, two CV inputs) | **Implemented, tested** |
| Fixed filter bank (8 bands, 125 Hz–12 kHz) | **Implemented, tested** |
| Analogue switch (manual / CV select / sequential, 1 ms crossfade) | **Implemented, tested** |
| Multiples ×2 (4 outputs each) | **Implemented, tested** |
| Gate delay / pulse stretcher | **Implemented, tested** |
| 4-channel mixer (attenuverters, inverted out), VCA 2 | **Implemented, tested** |
| Voltage processor, slew limiter, envelope follower, S&H (now patchable) | **Implemented, tested** |
| Filter A individual LP/BP/HP/notch outputs (shared upsampler, 4 decimators) | **Implemented, tested** |
| Diode-ladder and transistor-ladder variants | **Phase 3B.** They need a proper coupled-stage ZDF solve; not rushed into this phase |
| Patch canvas, animated cables, zoom, layouts | **Phase 3B** |
| Swift patch layer and patch bay UI | **Written, awaiting compile** |

## Test results
| Suite | clang | g++ ASan + UBSan | g++ TSan |
|---|---|---|---|
| `test_kernel` (44) | ✅ | ✅ | ✅ |
| `test_modules` (61) | ✅ | ✅ | n/a |
| `test_graph` (29) | ✅ | ✅ (goldens SKIP: other toolchain) | ✅ (goldens SKIP) |

### Regression against Phase 2 (the key result)
The unpatched graph was compared with 12 golden renders from the Phase 2 engine:
- **10 cases** are bit-close at −100 to −300 dB, so they are effectively identical.
- **`a_to_b`** has a waveform residual of −22.6 dB but 1/3-octave spectra within 0.21 dB. This is by design: each filter now has its own oversampler, which adds latency and phase shift but doesn't change the sound.
- **`vco3_mod`** matches within 2.6 dB from the fundamental up, and is *cleaner* below it. Building the graph exposed a **Phase 2 bug**: VCO 3 → cutoff modulation dropped to zero at the start of every control block, creating low-frequency junk. It is fixed.
- **Toolchain note:** with a different compiler, floating-point rounding drifts oscillator phase, so the goldens are only bit-reproducible on the toolchain that rendered them. I verified that Phase 3 built with g++ against Phase 2 built with g++ is also bit-close in all 10 cases. On another toolchain the test prints SKIP instead of a misleading pass or fail; `golden/README.md` explains how to re-baseline in Xcode.

### Other measured checks
- **Patching:** VCO 4 replacing VCO 1 is pitched exactly (440.00 Hz). Validation rejects input-taken, duplicate id, wrong direction, bad gain and hidden-module cables.
- **Feedback:** a VCA → mixer → filter loop is broken with 1 delayed edge and stays finite for 5 s (peak 0.31).
- **Utility modules:** the sequential switch steps on each note. Gate delay is 101.5 ms for a 100 ms setting. Slew-limiter portamento works.
- **Filters:**
  - Comb peak within 0.1 % of 440 Hz.
  - Formant F1: 621 Hz for A (600 expected), 246 Hz for I (250 expected).
  - VC band-pass peak at 2010 Hz (2000 expected).
  - Filter bank peak at 967 Hz (1 kHz band).
  - OTA low-pass: 37.8 dB of separation.
  - Filter A's LP and HP outputs, patched separately, behave as LP and HP.
- **Telemetry:** the patched cable shows 0.98; a normalled edge whose level knob is at 0 shows exactly 0.
- **Real-time safety:** zero heap allocations in render across 300 random patch swaps, including feedback loops. Under TSan, 2000 patch changes from a UI thread while the audio thread renders produce no data races.

## Performance (host CPU, 32 voices sounding, share of buffer budget)
| | Phase 2 | Phase 3A |
|---|---|---|
| Default (Filter B, 2×) | 22 % | **21.6 %** |
| Filter A only, 2× | 15 % | 15.0 % |
| A→B or A+B, 2× | 21–23 % | 29.5 % |
| Worst single block, 60 s dense test | 72 % | 82 % (scheduling spikes on a shared VM) |

The default patch costs the same as Phase 2. A→B and A+B cost about 40 % more, because each filter now has its own oversampler (the price of making them independently patchable). An early profile measured 42 %; removing the modulo from the halfband filters fixed most of it. SIMD-across-voices remains the Phase 10 lever.

## Known issues
1. All Swift (Phases 1–3A) is still uncompiled. This remains the first thing to do on a Mac.
2. Feedback edges add one control block (≤ 32 samples) of delay. Very tight audio-rate feedback FM is therefore softer than on hardware.
3. The patch bay is a list. The visual canvas is 3B.
4. The patch-swap fade dips the whole output for about 2.7 ms. This is click-free, but a careful listener may notice it during sustained drones. Phase 3B can limit the fade to voices whose topology actually changed.
5. Cables into a VCO's `sync` input do nothing while that VCO's Sync parameter is Off. VCO 2's default is Off to preserve the Phase 2 sound; all other VCOs default to Hard.

## Next
**Phase 3B: patch canvas.** Module panels laid out on a zoomable workspace, drag-to-connect cables (colour, opacity, thickness, locking), cable glow driven by the telemetry above, pinch-zoom to 500 % around the touch point, fit and reset, saved layouts, and the diode-ladder and transistor-ladder filters.
