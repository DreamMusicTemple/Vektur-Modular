# Phase 2 Report — Core Synthesis

Date: 2026-11-27 · Build 0.2.0 · State schema v1 (unchanged; additive only)

## Summary
The Phase 1 test voice is gone. Every note now plays through the analogue
engine, wired as a normalled semi-modular voice:

VCO 1–3 (sub, sync, FM) + noise + ring mod → mixer → Filter A (multimode SVF)
and/or Filter B (4-pole ladder), with 4 routings and 1/2/4× oversampling → VCA,
with EG 1 (filter, pitch) and EG 2 (amp). Global sample & hold drives the
filters. Authentic/Modern modes, an instability amount, glide and stereo voice
spread are included.

87 host-automatable parameters in 12 module groups. They are generated from a
single table (`tools/params.py`), so Swift and C++ cannot drift apart.

**Listen:** `VekturModular-Phase2-demo.m4a` is 38 s, 7 patches, rendered
through the same kernel the app and AUv3 run.

## Files created
```
tools/params.py                                   parameter single source → 3 generated files
Sources/VekturDSP/include/VekturParams.h          (generated) C address constants
Sources/VekturDSP/ParamTable.hpp                  (generated) C++ ranges/defaults/smoothing
Sources/VekturCore/ParameterRegistry+Generated.swift (generated)
Sources/VekturDSP/modules/DSPCommon.hpp           maths, RNG, port vocabulary
Sources/VekturDSP/modules/Vco.hpp                 band-limited VCO
Sources/VekturDSP/modules/Filters.hpp             SVF, ladder, halfband oversampler
Sources/VekturDSP/modules/Envelope.hpp            ADSR with vintage curves
Sources/VekturDSP/modules/Utilities.hpp           VCA, noise, ring mod, S&H, slew, follower, voltage processor, mixer
Sources/VekturDSP/SynthVoice.hpp/.cpp             normalled voice
DSPTests/test_modules.cpp                         61 measured checks
DSPTests/render_demo.cpp                          audition renderer
docs/dsp/MODULES.md                               module reference
```
## Files modified
| File | Change | Impact |
|---|---|---|
| `Kernel.hpp/.cpp` | `BringUpVoice` → `SynthVoice`; stereo bus; self-registers params; builds the per-block voice snapshot; global S&H; glide source | Same allocator, events, safety and telemetry. Mono hosts get L+R/2. |
| `VekturDSP.h` | Phase 1 address macros replaced by the generated `VekturParams.h` | **Phase 1 addresses 0–3 unchanged** (generator asserts it) |
| `ParameterSpec.swift` | + `taper`, `group`, units `octaves`, `degrees` | Additive |
| `ParameterRegistry.swift` | Now reads the generated list | Same API |
| `ParameterTreeBuilder.swift` | One AU group per module, log taper flag, non-automatable flag | Hosts see grouped parameters |
| `KernelHandle.swift` | No longer registers (the kernel does); debug check that defaults match | — |
| `VekturModularAudioUnit.swift` | Value strings for Hz, ms, %, oct, °, on/off | Display only |
| `DiagnosticsConsoleView.swift` | Collapsible module groups; pickers, toggles, log sliders | Dev console only |
| Tests | Kernel tests adapted to self-registering kernel; new registry and state tests | — |

**Deleted:** `BringUpVoice.hpp`, which was the temporary voice labelled as such
in Phase 1.

**State compatibility:** Phase 1 saves load, and every new parameter takes its
default (`testPhase1StateLoadsWithPhase2Defaults`). No schema bump was needed.

## Functionality status
| Item | Status |
|---|---|
| VCO: 6 waves, sub −1/−2 oct, hard + soft sync, exp/lin audio-rate FM, PWM, octave/coarse/fine, LF mode, phase reset/start phase, drift, tolerance, HF droop | **Implemented, tested** |
| Filter A SVF (LP/BP/HP/notch, resonance → self-osc, drive, audio-rate FM) | **Implemented, tested** |
| Filter B 4-pole ladder (24 dB/oct, self-osc, drive, bass loss, tolerance) | **Implemented, tested** |
| Oversampling 1/2/4× for the nonlinear filter section | **Implemented, tested** |
| VCA lin/exp/saturating, initial gain, velocity | **Implemented, tested** |
| EG ×2: ADSR, 4 curves, loop, retrigger mode, velocity; EG1 → pitch | **Implemented, tested** |
| Noise white/pink/colour, ring mod with carrier leak, S&H with slew | **Implemented, tested** |
| Authentic / Modern modes, instability amount, glide, stereo spread | **Implemented, tested** |
| Slew limiter, envelope follower, voltage processor, generic mixer modules | **Implemented, unit-tested — awaiting integration** (they become patchable in Phase 3) |
| Additional filter models (diode ladder, transistor ladder variant, OTA, comb, formant, fixed bank, VC band-pass) | **Not started — moved to Phase 3**. See below. |
| Expanded oscillator bank (6 extra VCOs) | **Moved to Phase 3**. Same `Vco` class; see below. |
| Swift / AUv3 / app changes | **Written, awaiting compile** (no Swift toolchain here) |

### Why two items moved to Phase 3 (Rule 12)
Exposing six more VCOs and seven more filter models as *normalled* controls now
would add roughly 120 host parameters whose meaning changes when patching
arrives. Their natural home is the patch graph: you place a comb filter or a
seventh VCO where you want it. The module classes they reuse (`Vco`, the ZDF
framework, the oversampler) are done. Phase 3 adds them as graph modules, with
their reserved address blocks (4–9, 12–17) already fixed in ARCHITECTURE §6.

## Build results
- C++: clean with clang 18 and g++ 13 at `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`.
- Swift: **not built**. As in Phase 1, the first compile happens on your Mac.

## Test results
| Suite | clang Release | g++ ASan + UBSan | g++ TSan |
|---|---|---|---|
| `test_kernel` (41) | ✅ 0 failures | ✅ | ✅ |
| `test_modules` (61) | ✅ 0 failures | ✅ | ✅ |

Selected measurements:
- **Pitch:** oscillators within 0.005 ct from 27.5 Hz to 4.2 kHz. Full-voice tracking C1–C7 within 0.001 ct (Modern, instability 0).
- **Aliasing below 15 kHz at 4.1 kHz:** saw −41.5 dB, square −41.3, pulse −41.9, triangle −60.3. That is 25–29 dB better than naive.
- **Hard sync:** off-harmonic energy −32 dB. **Soft sync near 2:1:** −54 dB (locks).
- **SVF:** LP −42.8 dB a decade above fc. Notch −60 dB at fc. HP DC residual 0.
- **Ladder:** slope 25.9 dB/oct. Self-oscillation pitch error ≤ 0.01 % at 220 Hz, 1 kHz and 4 kHz.
- **Stability fuzz:** 20 s of random cutoff, resonance and drive jumps every 32 samples with full-scale noise in. Always finite, peaks ≤ 3.2.
- **Envelopes:** all 4 curves hit attack, decay and release times to ±1 sample.
- **Voice character:** 8 voices on one note spread 8.5 ct (Authentic, 100 %) vs 1.2 ct (Modern), and the spread is deterministic per voice.
- **Real-time safety:** zero heap allocations in render under random-event stress (now covering the full voice).

## Performance observations (host CPU, not a device)
Steady state with 32 voices sounding, VCO 1 saw, as a share of the 512-frame budget:

| Oversampling | Filter A | Filter B | A→B | A+B |
|---|---|---|---|---|
| 1× | 7 % | 9–11 % | 9–10 % | 9–11 % |
| 2× (default) | 15 % | 22–27 % | 21 % | 20–23 % |

The worst single block in a 60 s dense run was 72 %, a scheduling spike on a
shared cloud VM. Unused oscillators, filters and noise are skipped per voice.
SIMD-across-voices (ARCHITECTURE §8) is still to do. Device numbers come in
the Phase 1/2 device check.

## Known issues
1. Swift side uncompiled (both phases). This is the top priority on your Mac.
2. Aliasing is good but not best-in-class. Phase 10 item: an "HQ oscillator" mode (table minBLEP or 2× oscillators) for bright high leads.
3. In the normalled voice, VCO 3 → cutoff is applied to both filters through their own amounts. Arbitrary modulation routing comes with the Phase 4 matrix.
4. S&H is a single global module, so all voices share one random stream (as on the hardware). Per-voice S&H comes with patching.
5. Velocity → filter is EG 1 velocity only. The full velocity routing comes with the matrix.

## Next phase
**Phase 3: modular environment.** The graph compiler with off-thread compile,
atomic swap and crossfade; the patch model; normalled connections expressed as
default cables over this voice; the oscillator bank and additional filter
models as graph modules; switching, multiples and gate delay; and the patch
canvas with signal-driven cable animation and 500 % zoom.
