# Phase 8 Report — User Interface (and a web test build)

Date: 2026-11-27 · Build 0.8.0 · AU version 2048 · State schema v1 (additive: optional `appearance` section)

## Summary
Phase 8 gives Vektur Modular the hardware panel look from `docs/design/HARDWARE_AESTHETIC.md`:
- black-painted panels with satin sheen and orange peel;
- white silkscreen legends and printed block diagrams;
- slider banks with white caps, silver knobs, lever and rotary switches;
- jacks with silver rings, red and green LEDs, red gate buttons, cream needle VU meters;
- brushed-aluminium rails with screws, all in a tolex cabinet.

Wear is procedural and seeded per module. It ranges from 0 (factory new) to 1 (well-used studio unit), set for the whole rack or per module, and is saved with the project.

The app has 13 screens. In regular width (iPad, Mac) they sit in a sidebar; in compact width (iPhone, narrow Split View) in a tab bar. On iPadOS (Stage Manager, external display) and on the Mac, any screen can also open in its own window.

The user also asked for a **web test version**, which is published as an artifact. It runs the **real C++ kernel compiled to WebAssembly** in an AudioWorklet, behind a panel UI in the same design language. It is the first way to hear and play Vektur Modular without Xcode.

## Design decisions
- **Panels are data, not code.** `tools/panels.py` is the single description of all 33 module panels in 5 cabinets: sources; filters, amplifiers and envelopes; modulation and control; expanders; output.
  - Each panel lists its controls by parameter identifier and names a rack module for its jacks.
  - The SwiftUI rack and the web build read the same JSON.
  - The script, a Swift test and an AU test check that every identifier and rack key exists.
- **The control style comes from the parameter:** booleans and lists of up to 4 become lever switches, longer lists become rotary selectors, continuous values become knobs. A panel's `sliders` form its main fader bank. So a new parameter can never get the wrong kind of control.
- **Wear is generated, never photographed.**
  - The seed is FNV-1a of the module key, fed through an LCG. Swift and JavaScript use the identical algorithm, so a module looks the same in the app and in the browser (pinned in `PanelLayoutTests`).
  - Scratches cluster in the lower half, near jacks and slots. Rub-throughs sit on the edges. Dust collects in the rails, and fingerprints on fader caps.
  - Legend yellowing is capped: at full Wear the contrast is still about 15:1 (WCAG AA needs 4.5:1).
- **Accessibility.**
  - Every control is one VoiceOver element. It carries the parameter's full name and its formatted value, with increment and decrement actions (VoiceOver swipes, Switch Control, Full Keyboard Access) and a "Reset to default" action.
  - Lever and rotary switches give selection haptics.
  - Jacks are buttons with a patched / selected / empty value.
  - Cable glow follows Reduce Motion.
- **Patching on the rack:**
  - Tap an output, then an input, to connect.
  - Tap a patched input with nothing selected to remove its cable.
  - The rack edits the same `PatchState` as the patch canvas, which keeps drag-to-wire, zoom and the cable inspector.
  - Cables droop as cubic curves and glow with signal level.
- **Web build without Emscripten.**
  - The kernel is compiled with clang's `wasm32` target and linked with `wasm-ld`, using the host's header-only libstdc++.
  - A 300-line runtime (`web/runtime/wasm_rt.cpp`) supplies: the heap, memory and string functions, a small printf, complex helpers, libstdc++'s hash-table policy, and float maths in-module. Double-precision maths and the clock come from JavaScript.
  - Struct layouts are exported by the module (`vkw_layout`), so the page never hard-codes offsets.
  - **The kernel source is unchanged.**

## Files
**Created**
- `tools/panels.py`, `VekturCore/Resources/Panels.json` (generated), `VekturCore/PanelLayout.swift`: layout model, control styles, legends, `WearPattern`, `AppearanceState`.
- `VekturUI/Panel/PanelKit.swift`: theme, paint, rails, tolex, screws, taper helpers.
- `VekturUI/Panel/PanelControls.swift`: fader, knob, lever, rotary, LED, jack, VU meter, gate button.
- `VekturUI/Panel/ModulePanelView.swift`: block diagrams, panel view, jack anchors.
- `VekturUI/Panel/RackView.swift`: cabinets and the cable layer.
- `VekturUI/MainView.swift`: the 13 screens and the navigation.
- `VekturUI/Screens.swift`: the Perform, Arpeggiator, Generative, Mixer & Output, Presets and Settings screens.
- `VekturAudioUnit/Appearance.swift`.
- `Tests/VekturCoreTests/PanelLayoutTests.swift`.
- **Web:**
  - `web/index.html`, `web/app.js`, `web/engine.js`, `web/worklet.js`;
  - `web/runtime/wasm_rt.cpp`, `web/runtime/vk_web.cpp`;
  - `tools/build_web.sh`, `tools/test_web.mjs`;
  - `web/params.json` and `web/panels.json` (generated).

**Modified**
| File | Change |
|---|---|
| `tools/params.py` | Also writes `web/params.json` |
| `Package.swift` | VekturCore bundles `Panels.json` |
| `InstrumentState.swift` | Optional `appearance` section |
| `VekturModularAudioUnit.swift` | Posts an appearance change when a state is applied |
| `PatchCanvasView.swift` | Tolex workspace. Module tiles have aluminium headers, painted bodies and silver jacks; the signal-kind ring is kept. |
| `EffectsView`, `MIDIView`, `ModulationView`, `SequencerView` | Panel paint instead of grey cards |
| `TestKeyboardView.swift` | Explicit initialiser (range, octaves) |
| `App/VekturModularApp.swift` | `MainView`, plus a `screen` WindowGroup for extra windows |
| `AUExtension/AudioUnitViewController.swift` | `MainView`, 1024 × 640 preferred size |
| `AudioUnitTests.swift` | Appearance persistence; every panel's rack module exists |
| `project.yml`, `AudioComponentInfo.swift` | 0.8.0 / AU 2048 |

## Functionality status
| Item | Status |
|---|---|
| Panel layouts (33 panels, 5 cabinets), validation, control-style rules | **Implemented, tested** (generator, `PanelLayoutTests`) |
| Seeded wear (Swift ⇄ web parity), appearance state | **Implemented, tested** |
| SwiftUI panel kit, rack with patching, 13 screens, multi-window, restyled canvas and pages | **Partially implemented:** written, awaiting compile. No Swift toolchain here. |
| Web test build: kernel → wasm, AudioWorklet (with ScriptProcessor fallback), rack with patching, sequencer, arp/gen with preset creator, effects (type-named parameters), matrix, MIDI (Web MIDI in/out, Learn, profiles), presets, keyboard / wheels / VU, wear | **Implemented, tested** in Node and headless Chrome (desktop and 390 pt phone widths) |
| Tilt-reactive metal highlights | **Not started.** The setting is stored; rendering needs Metal layer effects (Phase 10). |
| Comparison with the reference photos; device passes (size classes, Stage Manager, external display, VoiceOver) | **Not started** (needs devices) |

## Measured results
- **Web kernel vs native.** A 2 s chord render differs from the native build by −167 dB relative (max 2.2e-8). With the plate reverb the difference is −95 dB (max 1.3e-6), which comes from the in-module float `exp`, `sin` and `tanh`.
  - Speed: 19× real time on this host (Node, V8) for a 3-note chord, and 16× with the plate.
  - Size: `kernel.wasm` is 460 KB.
- **Headless Chrome** (`--autoplay-policy=no-user-gesture-required`):
  - The worklet started and played a computer-keyboard chord; the readout showed 3 voices and the chord "C".
  - The patch was accepted and the cable drawn.
  - A fader drag reached 20 kHz.
  - Plate reverb's 10 parameters were labelled from the catalogue.
  - The preset creator, arpeggiator and Play all ran without page errors.
  - Desktop and phone layouts were checked from screenshots.
- `node tools/test_web.mjs`: 6/6.

### Test suites
| Suite | Result |
|---|---|
| C++ (clang -Werror): kernel 44, modules 74, graph 31, modulation 97, sequencing 91, effects 74, midi 103, host 24 | All pass. No C++ changes this phase, so the Phase 7 ASan and TSan results stand. |
| Web smoke test (Node) | 6/6 |
| Swift (`PanelLayoutTests` 6, AU 2 new) | Written; not run (no Swift toolchain here) |

## Web test build: what differs from the app
- No AUv3 host and no Ableton Link.
- No file access: no user impulse responses, no MIDI-file export.
- Presets and MIDI mappings live in the browser's storage. "Copy state" gives parameters by identifier in the app's state format.
- Program change selects browser presets by name order.
- Web MIDI depends on the browser and on permission inside the artifact frame. The on-screen and computer keyboards always work.
- In the AudioWorklet, the load readout shows voices only, because the worklet has no high-resolution clock.

## Known issues and limitations
- The SwiftUI code has not been compiled; expect a first round of compile fixes in Xcode.
- The panel's orange peel and wear are redrawn by `Canvas` when the panel changes. On large pages this may need `drawingGroup()` or caching after profiling on device (Phase 10).
- Rack patching removes a cable by tapping its input, so the rack has no per-cable inspector; use the patch canvas for gain, colour and lock.
- The legend text heuristic (`PanelLegend`) drops the module's name from parameter names. A few legends read awkwardly ("generate style"); `tools/panels.py` can override them in a later pass.

## Next
- Compile in Xcode 16 and fix any errors, then run `swift test`.
- On devices, check: iPhone (compact), iPad Split View, Stage Manager with an external display, the Mac, and VoiceOver.
- Compare the rack against the reference photos and tune paint, wear density and legend sizes.
- Phase 9: presets (2 × 32 factory), documentation and export.
