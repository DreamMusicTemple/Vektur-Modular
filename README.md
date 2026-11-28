# Vektur Modular

Vintage modular analogue synthesiser for iPadOS, iOS and macOS — standalone and AUv3.
DMT Cymatics — Dream Music Temple.

**Current state: 1.0.0 — Phases 9 and 10 complete (factory presets, recording, export, manual; HQ oscillators, settings, privacy and release prep).**
See `docs/PHASE10_REPORT.md`, `docs/PHASE9_REPORT.md` (and earlier reports) for exactly what is done, tested and pending; `docs/MIDI.md` for the MIDI implementation and `docs/DAW_TEST_MATRIX.md` for host testing.

## Build
Requires **Xcode 27+ (iOS 27 / macOS 27 SDK)** for TestFlight and App Store builds — Release builds fail on older SDKs (`tools/check_sdk.sh`); Swift 6 and [XcodeGen](https://github.com/yonaskolb/XcodeGen).

```bash
brew install xcodegen
# set DEVELOPMENT_TEAM in Config/Shared.xcconfig and register the App Group
# group.com.dreammusictemple.vekturmodular for the team (shared IR library, global MIDI mappings)
# optional: LinkKit.xcframework + multicast entitlement for Ableton Link (docs/MIDI.md)
xcodegen generate && open VekturModular.xcodeproj
```

Manual: `docs/manual/` (38 chapters, also in the app under Help). Release: `docs/RELEASE_CHECKLIST.md`.

Schemes: `VekturModular_iOS`, `VekturModular_macOS` (each embeds its AUv3 extension).

## Tests
```bash
cd Packages/VekturKit && swift test                       # Swift suites (macOS)
cd DSPTests && cmake -S . -B build && cmake --build build && ./build/test_kernel   # C++ kernel, any OS
./build/test_modules                                     # module measurements
./build/test_graph                                       # patching + Phase 2 regression (run from DSPTests/)
./build/test_modulation && ./build/test_sequencing       # modulation, arp / sequencer / generative / presets
./build/render_demo demo5.wav --phase5                   # sequencing & generative audition render
./build/test_effects && ./build/render_demo demo6.wav --phase6   # effects measurements + audition
./build/test_midi && ./build/test_host                    # MIDI engine; host simulation (load/unload, automation, meters)
./build/render_demo demo7.wav --phase7                   # MIDI audition (clock sync, learn, RPN, MIDI 2.0, 3/4)
tools/auval.sh --stress                                  # macOS: Apple validator + 50 load/unload cycles
tools/build_web.sh && node tools/test_web.mjs            # browser build (WebAssembly) + smoke test
python3 -m http.server -d web 8000                       # then open http://localhost:8000
./build/render_demo demo3.wav --phase3                   # patching audition render
./build/render_demo demo.wav                             # audition render
cmake -S . -B build-asan -DVK_SANITIZE=ON && cmake --build build-asan && ./build-asan/test_kernel
```

## Layout
| Path | What |
|---|---|
| `Packages/VekturKit/Sources/VekturDSP` | C++20 real-time kernel, plain-C API |
| `Packages/VekturKit/Sources/VekturCore` | Parameters, versioned state, migration |
| `Packages/VekturKit/Sources/VekturAudioUnit` | AUv3 `AUAudioUnit` subclass |
| `Packages/VekturKit/Sources/VekturEngine` | Standalone hosting, CoreMIDI in/out, Ableton Link |
| `Packages/VekturKit/Sources/VekturUI` | SwiftUI views |
| `App/`, `AUExtension/` | Thin app and extension shells |
| `web/` | Browser test build: kernel.wasm (generated), AudioWorklet host, panel UI |
| `docs/` | Architecture, phase checklist, phase reports |

## Rules that keep this project healthy
- Parameters are defined ONLY in `tools/params.py`; run `python3 tools/params.py` after editing.
- Panel layouts are defined ONLY in `tools/panels.py`; run `python3 tools/panels.py` after editing.
- Never renumber a parameter address or rename an identifier (`ParameterRegistryStabilityTests`).
- Never edit a shipped `StateMigrator` step; add the next one.
- Nothing on the render thread allocates, locks, logs or calls Swift/Obj-C.
- The reference hardware is named only in `ProductInfo.legalDisclaimer`.
