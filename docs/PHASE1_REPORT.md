# Phase 1 Report — Project Architecture

Date: 2026-11-27 · Build 0.1.0 (1) · State schema v1

## Summary
**Renamed 2026-11-27:** working title "Vektur 700-M" → **Vektur Modular** (bundle IDs, targets, AU subtype `VkMd`, state key and file names all updated; nothing had shipped, so no migration is needed).

Phase 1 establishes the project structure, a real-time-safe audio kernel, the
AUv3 instrument extension, the standalone host, the parameter architecture and
versioned state management. A deliberately simple **bring-up voice** (band-
limited sawtooth + gain envelope) makes the whole path audible and testable;
it is not the synthesis engine and is replaced in Phase 2.

## Files created

```
project.yml                      XcodeGen spec (iOS + macOS app, iOS + macOS AUv3)
Config/Shared.xcconfig           Team ID + bundle prefix (edit before building)
App/VekturModularApp.swift          Standalone shell
AUExtension/AudioUnitViewController.swift   AUv3 factory + view
Packages/VekturKit/Package.swift
  Sources/VekturDSP/             C++20 kernel
    include/VekturDSP.h          plain-C API + stable parameter addresses
    Kernel.hpp/.cpp              render loop, events, voices, safety, telemetry
    ParameterStore.hpp           lock-free params + ramps
    SPSCQueue.hpp                wait-free queues
    BringUpVoice.hpp             Phase 1 test voice (temporary)
    CAPI.cpp                     C entry points
    AURenderAdapter.mm           AURenderEvent / UMP → kernel (Apple only)
  Sources/VekturCore/            ParameterSpec, ParameterRegistry, InstrumentState,
                                 StateMigrator, StateCodec, JSONValue, ProductInfo
  Sources/VekturAudioUnit/       VekturModularAudioUnit, KernelHandle,
                                 ParameterTreeBuilder, AudioComponentInfo
  Sources/VekturEngine/          StandaloneAudioEngine, MIDIInputService
  Sources/VekturUI/              DiagnosticsConsoleView, TestKeyboardView,
                                 TelemetryModel, ParameterModel, AboutView
  Tests/VekturCoreTests/         state codec, migration, registry stability
  Tests/VekturDSPTests/          Swift↔C bridge
  Tests/VekturAudioUnitTests/    AU lifecycle, render, fullState
DSPTests/                        CMake host test suite for the C++ kernel
docs/                            ARCHITECTURE, PHASES, this report
```

Files modified: none (greenfield).

## Functionality status

| Feature | Status |
|---|---|
| C++ kernel: sample-accurate MIDI & parameter events, ramps, smoothing | **Fully implemented, tested** |
| Voice allocation (1–32), same-key retrigger, quietest-release/oldest steal with 2 ms fade, polyphony reduction | **Fully implemented, tested** |
| Sustain pedal, pitch bend (range param), All Notes Off / All Sound Off, panic | **Fully implemented, tested** |
| Output safety (NaN/Inf recovery, ±2.0 ceiling), telemetry ring | **Fully implemented, tested** |
| Zero heap allocation in render | **Verified** by allocation-tracking test |
| AUv3 adapter (AURenderEvent, MIDI 1.0 bytes, UMP MIDI 1.0/2.0 note/CC/bend) | Written — **awaiting compile** on macOS |
| `VekturModularAudioUnit` (busses, tree, render block, fullState, user presets) | Written — **awaiting compile** |
| Versioned state, migration, sanitising, unknown-section preservation | Written — **awaiting compile**; tests written |
| Standalone engine (in-process AU, session, interruptions, route changes, media reset, background toggle, session file with corrupt-file quarantine) | Written — **awaiting compile & device test** |
| CoreMIDI input, hot-plug | Written — **awaiting hardware test** |
| Diagnostics console, test keyboard, About + disclaimer | Written — **awaiting compile** (prototype UI, replaced in Phase 8) |
| Factory presets | **Not in Phase 1** (Phase 9). `factoryPresets` returns an honest empty list. |
| DAW compatibility | **Awaiting DAW testing** |

## Build results
- **C++ kernel** — built clean with clang 18 (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`) and g++ 13.
- **Swift / Xcode targets — NOT BUILT.** The workspace used for this phase has
  no Swift toolchain or Xcode. The code targets Swift 6 strict concurrency and
  was reviewed by hand, but expect a small number of compiler fixes on first
  build. That is the first task on your Mac (below). I am not claiming it
  compiles until it has.

## Test results (C++ host suite, `DSPTests/`)
| Config | Result |
|---|---|
| clang RelWithDebInfo, -Werror | 41 checks, 0 failures |
| g++ Debug + AddressSanitizer + UBSan | 41 checks, 0 failures, no sanitizer reports |
| g++ + ThreadSanitizer | 41 checks, 0 failures, no races |

Covers: allocate validation; unallocated render silence; A4 = 440 Hz ± 1.5;
release to silence; sample-accurate note at offset 100; late events applied;
pitch bend ±12 and master tune ±100 ct accuracy; polyphony limit; steal adds no
discontinuity beyond the waveform's own; 40 notes on 32 voices; polyphony
reduction; sustain hold/release; clamping and NaN rejection; volume jump ramps;
host ramp events; random-event stress with **zero allocations**; telemetry
sanity; SPSC FIFO across threads (2 M items); 60 s dense 32-voice run.

Swift test suites (written, to run on Mac): 12 state/registry tests, 2 bridge
tests, 5 AU tests.

## Known issues / limits
1. Swift side uncompiled (see above).
2. Bring-up voice is a test tone, not the instrument.
3. `AURenderAdapter` converts UMP MIDI 2.0 to 7-bit MIDI 1.0 values; full
   32-bit/per-note handling arrives with MPE (Phase 4) and MIDI 2.0 (Phase 7).
4. MIDI from the on-screen keyboard is scheduled "immediate" (not sample-
   stamped) — fine for UI, the DAW path is sample-accurate.
5. The macOS AUv3 loads out-of-process; in-process loading on macOS requires
   moving the AU into a framework with `AudioComponentBundle` (Phase 7, only if
   profiling shows IPC overhead matters).
6. No app icon / asset catalog yet.

## Performance observations
Host CPU (cloud x86, not a device): worst 512-frame block at 32 voices ≈ 2.5 %
of the buffer budget in Release. This only proves the kernel overhead is small;
real budgets get measured on iPhone/iPad in Phase 1 device checks and Phase 10.

## What to do on your Mac
```bash
brew install xcodegen
cd VekturModular
# 1. put your Team ID in Config/Shared.xcconfig
xcodegen generate
open VekturModular.xcodeproj           # build VekturModular_iOS / VekturModular_macOS
cd Packages/VekturKit && swift test # Swift test suites (macOS)
cd ../../DSPTests && cmake -S . -B build && cmake --build build && ./build/test_kernel
# 2. after running the macOS app once (registers the extension):
auval -v aumu VkMd DMTc
```
Then load it in AUM / GarageBand on the iPad, play, save the project, reopen,
and confirm Master Volume and Polyphony restored. Send me any compiler errors
verbatim and I'll fix them before Phase 2 starts.

## Next phase
**Phase 2 — Core synthesis:** C++ module interface and per-voice graph runtime
(ARCHITECTURE §8), then VCO, Filter A (SVF), Filter B (ladder), VCAs, EGs,
noise, ring mod, S&H, voltage processor and mixers, each with DSP tests, and
the bring-up voice retired.
