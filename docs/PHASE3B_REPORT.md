# Phase 3B Report — Patch Canvas and Ladder Variants

Date: 2026-11-27 · Build 0.3.1 · State schema v1 (additive: `patch.layout`)

## Summary
Phase 3 is now complete. 3B adds the visual modular workspace and the two
remaining filter models from the brief.

## Files
**Created:**
- `modules/LadderVariants.hpp`: the diode-ladder and transistor-ladder filters
- `VekturUI/PatchGeometry.swift`: panel, jack and cable geometry
- `VekturUI/PatchCanvasView.swift`: the canvas

**Modified:**
| File | Change | Impact |
|---|---|---|
| `tools/params.py` (+ generated) | 10 parameters (diode ladder at block 12, transistor ladder at block 59), 232 total | Earlier addresses unchanged |
| `graph/Rack.*`, `GraphModules.cpp`, `VoiceGraph.cpp` | Two instances **appended** (instance order permanent); `LadderVariantModule` | Existing patches unaffected |
| `PatchState.swift` | `PatchLayout`: positions, zoom, pan, cable opacity/thickness, show-defaults | Additive. Older saves get the default layout |
| `InstrumentState.swift` | Saves the patch when it has cables **or** a non-default layout | — |
| `VekturModularAudioUnit.swift` | `updateLayout` (visual only, never recompiles the graph) | — |
| `DiagnosticsConsoleView.swift`, app, AU view | Canvas/Console switch; only the console page scrolls, so canvas gestures don't fight the scroll view | — |
| Tests | Ladder tests, rack count 38/36, layout round-trip | — |

## Functionality status
| Item | Status |
|---|---|
| Diode-ladder filter: coupled-stage 4×4 implicit (ZDF) solve, resonance calibrated to cutoff, bounded self-oscillation, drive, 2 CV inputs, oversampled | **Implemented, tested** |
| Transistor-ladder filter: per-stage saturation, 2× internal, half-sample feedback compensation, self-oscillation | **Implemented, tested** |
| Canvas: module panels with input/output jacks, default grid layout, drag headers to move (10 pt snap) | **Written, awaiting compile** |
| Drag-to-connect (output → nearest input within 26 pt); re-patching an input replaces its cable unless locked | **Written, awaiting compile** |
| Cable colours by signal type (audio orange, CV blue, gate/trigger green) or a chosen colour; opacity 10–100 %; thickness 1–8 pt; gravity sag | **Written, awaiting compile** |
| Cable inspector: colour, gain (−2…+2), lock, remove | **Written, awaiting compile** |
| Travelling glow driven by real connection telemetry (speed and brightness follow the level; still when silent) on user and normalled cables | **Written, awaiting compile** |
| Normalled connections shown as faint dashed lines (toggle) | **Written, awaiting compile** |
| Pinch zoom up to 500 % around the fingers, one-finger pan on empty space, Fit and 100 % buttons, layout saved with the patch | **Written, awaiting compile** |
| Two-finger trackpad pan and module grouping/labelling | **Not yet.** One-finger pan covers touch; grouping and labels are added with the Phase 8 panel work |
| Per-voice swap fade | **Deferred to Phase 10** (optimisation); the global 1.3 ms fade is click-free |

## Test results
| Suite | clang | g++ ASan + UBSan | g++ TSan |
|---|---|---|---|
| `test_kernel` (44) | ✅ | ✅ | see packaging note |
| `test_modules` (74) | ✅ | ✅ | n/a |
| `test_graph` (31) | ✅ | ✅ (19; goldens SKIP: other toolchain) | see packaging note |

Ladder measurements:
- **Diode ladder:**
  - Slope 22.6–24.9 dB/oct well above the cutoff.
  - The soft knee is the diode character: −18.7 dB at fc and about 1 dB of passband droop.
  - Self-oscillation within 3.4 % of fc at 250 Hz, 1 kHz and 4 kHz.
- **Transistor ladder:**
  - −12.1 dB at fc (four poles at fc, matching Filter B's convention).
  - Slope 21–22 dB/oct at 2→4×fc.
  - Self-oscillation within 0.8–5.4 %.
- **Both:**
  - 10 s of random modulation fuzz stays finite, with peaks of 4.9 (diode) and 2.6 (transistor).
  - Patched through the rack with cutoff at 600 Hz, they reject 4–6 kHz by about 70 dB.

The canvas is SwiftUI and cannot be built or run here. Like all Swift so far, it awaits the first Mac build.

## Known issues
1. All Swift (Phases 1–3B) remains uncompiled.
2. Canvas styling is functional (dark panels). The hardware look is Phase 8.
3. Pan is one finger on empty space; a two-finger pan gesture needs UIKit gesture bridging (Phase 8).
4. VoiceOver can identify every jack, but patching by drag is not accessible. The list patch bay in the Console page stays as the accessible patching path.
5. The canvas redraws cables at 30 Hz. Battery Saving (Phase 10) will lower this; it never affects audio.

## Next
**Phase 4: modulation and performance.** The full-screen modulation matrix, three LFOs, four macros, assignable wheels, voice modes (mono, legato, duo, poly), MPE, key zones, and the chord analyser.
