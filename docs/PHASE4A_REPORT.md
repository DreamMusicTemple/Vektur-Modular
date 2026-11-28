# Phase 4A Report — Modulation Engine & Voicing

Date: 2026-11-27 · Build 0.4.0 · State schema v1 (additive: new parameters only)

## Summary
Phase 4 is split in two, like Phase 3.
- **4A (this build):** the modulation engine and the voicing modes.
- **4B:** performance input (MPE, key zones, chord analyser, tempo-synced LFOs).

What 4A adds:
- A 16-slot, per-voice modulation matrix that can reach every continuous parameter, including the mixer levels and CV amounts.
- Three LFOs, which are also patchable rack modules.
- Four macros and the MIDI controller sources.
- Poly, mono, legato, unison and duo voice modes, with note priority and two glide behaviours.

## Files
**Created**
- `VekturDSP/modules/ModMatrix.hpp`: sources, the LFO, slot configuration and shaping
- `VekturCore/ModMatrix.swift`: the source and destination encodings, plus the slot field addresses
- `VekturUI/ModulationView.swift`: the modulation page, matrix rows, destination menu and the bend and mod wheels
- `Tests/VekturCoreTests/ModMatrixTests.swift`
- `DSPTests/test_modulation.cpp`: 70 checks

**Modified**
| File | Change |
|---|---|
| `tools/params.py` (+ 3 generated files) | 189 new parameters, 421 in total. Voicing goes in global indices 10–16, LFOs in 26–28, macros in 29 and the matrix in 60. A new `flags` field marks log taper and modulatable. Earlier addresses are unchanged. |
| `ParameterStore.hpp` | Parameters carry flags. New `modulated(address, offsets)`, which applies offsets in normalised taper units and clamps them. |
| `GraphModules.*` | `P()` now reads through the matrix. The keyboard applies the matrix pitch offset, unison detune, a per-note glide decision and constant-time or constant-rate glide. The output module ramps level and adds pan. New `LfoModule`. |
| `Rack.*` | Three LFO instances **appended**, so the rack now has 41 modules (39 patchable). |
| `VoiceGraph.*` | Per-voice modulation state: keyed LFOs, slew, per-note random and the offset table. Edge gains are now per voice when an attenuator is a matrix destination. |
| `Kernel.*` | `buildModFrame`, the MIDI controllers (CC 1/2/11, channel and poly pressure), a note stack, the mono/legato/unison/duo allocators, the bend-down range, and live activity for matrix-opened edges. |
| `DiagnosticsConsoleView.swift` | New Modulation page. |
| `AudioUnitTests.swift` | Rack count updated to 41/39. |
| `render_demo.cpp` | New `--phase4` mode. |
| `CMakeLists.txt` | New suite. Under g++ TSan builds only, a false `stringop-overflow` warning is suppressed. |
| Version | 0.4.0; AU component version 1024 |

## Functionality status
| Item | Status |
|---|---|
| Matrix: enable, source, scale-by, destination, amount, offset, curve (4), polarity (3), invert, slew | **Implemented, tested** |
| Per-voice, taper-aware destinations: every continuous module parameter, input attenuators (mixer levels, CV amounts), LFO rate and level | **Implemented, tested** |
| Virtual destinations: pitch, voice level, pan, all filter cutoffs | **Implemented, tested** |
| 21 sources: LFO 1–3, EG 1–2, velocity, key, mod wheel, aftertouch, poly aftertouch, bend, breath, expression, macros 1–4, per-note random, voice spread, constant | **Implemented, tested** |
| LFO × 3: 7 shapes, free or key, delay, phase, level; rack jacks `out`/`inv` | **Implemented, tested** |
| Macros × 4, automatable | **Implemented, tested** |
| Voice modes: poly, mono, legato, unison (2–8 voices, detune), duo; priority last/low/high | **Implemented, tested** |
| Glide always or legato-only; constant time or constant rate; separate bend-down range (default follows up) | **Implemented, tested** |
| Modulation page, matrix rows, destination menu, bend and mod wheels | **Partially implemented:** written, awaiting compile on a Mac |
| MPE, key zones, chord analyser, tempo sync | **Not started:** Phase 4B |

## Build & test results (Linux host, C++ core)
| Suite | clang -Werror | g++ ASan+UBSan | g++ TSan |
|---|---|---|---|
| test_kernel | 43/43 | 43/43 | 43/43 |
| test_modules | 74/74 | 74/74 | — |
| test_graph | 31/31 | 19/19 (golden skipped: toolchain) | 19/19 |
| test_modulation (new) | 70/70 | 70/70 | 70/70 |

- **Regression:** the default voice still matches the Phase 2 golden renders bit for bit (−139 dB residual), and the glide case is identical too. An empty matrix and the default poly mode therefore change nothing.
- **Real-time safety:** zero allocations during rendering with all 16 slots active, keyed LFOs, controller streams and voice-mode switching.
- **Swift:** still not compiled, because there's no Swift toolchain on the build host. Please build on a Mac with Xcode 16 or later.

## Performance
On the host, with 8 voices, the default patch and mode switching, one render core is:
- 0.9 % loaded with the matrix empty
- 1.1 % loaded with all 16 slots active, including two keyed LFOs and attenuator destinations

Matrix evaluation is O(slots) per voice per 32-frame block. Parameter reads add one branch when unmodulated, and one log/exp pair per 8 samples for a modulated log parameter.

## Known issues and limitations
- Modulation is control rate (32 frames). Fast audio-rate modulation belongs on patch cables, which are sample-accurate; the LFO jacks are provided for that.
- EG sources lag the audio by at most one block.
- Free-running LFOs ignore matrix rate offsets, because their phase is shared by all voices.
- Unison uses voices 1 to N regardless of the polyphony setting.
- Changing the voice mode releases all sounding notes, with a 2 ms fade.
- Discrete and global parameters are deliberately not destinations.
- The two-octave test keyboard sends no aftertouch; use a hardware controller or the host.

## Next
Phase 4B: MPE zones, key zones and layers, the chord analyser, tempo-synced LFOs, and per-slot live meters in the matrix UI.
