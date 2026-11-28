# Vektur Modular — Development Phase Checklist

Status legend: ✅ done & tested · 🟡 written, awaiting compile/test on Mac ·
🔵 awaiting device/DAW testing · ⬜ not started.
Every phase ends compilable, with a phase report in `docs/PHASEn_REPORT.md`.

## Phase 1 — Project architecture
- ✅ Technical audit, architecture, dependency map, signal flow, AUv3 state plan, risk register (`ARCHITECTURE.md`)
- ✅ C++ real-time kernel: param store + smoothing, sample-accurate events, SPSC command/telemetry queues, voice allocator, click-free stealing, safety stage
- ✅ Host-side DSP test suite (41 checks; clang -Werror, ASan/UBSan, TSan; zero-allocation render check)
- 🟡 Swift package `VekturKit` (Core, AudioUnit, Engine, UI) — Swift 6 strict concurrency
- 🟡 AUv3 instrument extension (iOS + macOS), parameter tree, fullState, user presets
- 🟡 Standalone app hosting the AU in-process; audio session, interruptions, route/config changes, background toggle, session persistence
- 🟡 Standalone CoreMIDI input (all sources, hot-plug)
- 🟡 Swift tests: state codec/migration, registry stability, C bridge, AU lifecycle/render/state
- 🔵 `auval -v aumu VkMd DMTc` passes (macOS)
- 🔵 Loads, plays, saves/restores in AUM, GarageBand, Logic Pro (iPad + Mac), Cubasis
- 🔵 Device check: iPhone + iPad, measured render load and I/O latency recorded

## Phase 2 — Core synthesis
- ✅ Parameter single source (`tools/params.py`) → C header, C++ table, Swift registry
- ✅ VCO (band-limited, drift, tolerance, HF droop, hard/soft sync, lin/exp FM, PWM, sub, LF mode, phase reset)
- ✅ Filter A (2-pole SVF multimode, ZDF), Filter B (4-pole ladder, nonlinear), 1/2/4× oversampling
- ✅ VCAs (lin/exp/saturating), EGs (4 curves, loop, retrigger), noise, ring mod, S&H, slew/glide
- ✅ Envelope follower, voltage processor, mixer modules (unit-tested; patchable in Phase 3)
- ✅ Authentic / Modern modes; instability amount; stereo spread
- ✅ DSP tests: 61 measured checks (pitch, aliasing, responses, self-osc, fuzz, timing, noise spectra, …)
- ✅ BringUpVoice retired; normalled voice behind the same allocator
- 🟡 Swift: grouped AU tree, log tapers, console UI (awaiting compile)
- 🔵 Device CPU measurement at 8/16/32 voices

## Phase 3A — Modular engine
- ✅ Fixed rack (36 instances), normalled connections reproducing the Phase 2 voice, input attenuators
- ✅ Graph compiler: validation, needed-module pruning, topological order, cycle breaking (delay + limiter)
- ✅ Lock-free triple-buffered graph publish; patch swap with fade; per-block gain ramps; inactive-module skipping
- ✅ Oscillator bank VCO 4–9; OTA multimode, comb, formant, VC band-pass, fixed filter bank
- ✅ Switch, multiples ×2, gate delay, 4-channel mixer, VCA 2; VP, slew, follower, S&H patchable
- ✅ Per-connection telemetry (levels for cable animation)
- ✅ Regression vs Phase 2 goldens (10 bit-close, 2 by design); 29 graph checks; RT/TSan patch fuzz
- 🟡 Swift: PatchState in InstrumentState, RackCatalog, applyPatch/currentPatch, patch bay list UI (awaiting compile)

## Phase 3B — Patch canvas
- ✅ Diode-ladder filter (coupled-stage implicit solve) and transistor-ladder filter (per-stage saturation), tested
- 🟡 Canvas: module panels, drag-to-connect, cable colour/opacity/thickness/lock, inspector (awaiting compile)
- 🟡 Telemetry-driven travelling glow on user and normalled cables (awaiting compile)
- 🟡 Pinch zoom to 500 % around the fingers, pan, fit/100 %, layout saved with the patch (awaiting compile)
- ⬜ Two-finger trackpad pan, module grouping/labelling → Phase 8 panel work
- ⬜ Per-voice swap fade → 1.1 (see PERFORMANCE.md)

## Phase 4A — Modulation engine & voicing
- ✅ 16-slot modulation matrix: enable, source, scale-by, destination, amount, offset, curve, polarity, invert, slew (tested)
- ✅ Per-voice, taper-aware offsets on any continuous parameter, including input attenuators; pitch / level / pan / all-cutoffs virtual destinations
- ✅ LFO × 3 (7 shapes, free or key retrigger, delay, phase, level), also patchable rack modules
- ✅ Macros × 4; mod wheel, channel and poly aftertouch, breath, expression and bend as sources
- ✅ Voice modes: poly / mono / legato / unison (2–8 voices, detune) / duo; note priority; glide always or legato-only; constant time or rate; separate bend-down range
- 🟡 Modulation page with performance wheels (SwiftUI, awaiting compile)

## Phase 4B — Performance input
- ✅ MPE lower/upper zone: per-note bend (range 0–96), pressure, timbre (CC 74); master channel stays zone-wide (tested)
- ✅ Key zones: split (split point, transpose per zone) and layer; Key Zone matrix source (tested)
- ✅ Chord analyser: 20 templates, inversions as slash chords, seventh chords without a fifth (tested)
- ✅ Tempo-synced LFOs: 15 divisions incl. dotted and triplet; host tempo and beat lock, internal tempo standalone (tested)
- ✅ Per-slot live meters and held-key telemetry (engine tested)
- 🟡 AU host-transport capture, meters, chord readout and zone/MPE controls in SwiftUI (awaiting compile)
- ⬜ MIDI 2.0 per-note controllers (UMP is currently converted to MIDI 1.0) → Phase 10 hardening

## Phase 5 — Sequencing & generative
- ✅ Arpeggiator: 14 patterns, 1–4 octaves, Euclidean rhythm, probability, ratchets, swing, gate, velocity modes, latch, pattern chaining, sequencer transpose; sample-accurate host sync (tested)
- ✅ Analogue-style step sequencer: 16 steps × rows A/B/C, Off/On/Tie/Skip, per-step probability and ratchet, 5 directions, scale quantiser, Run/Transport/Key; rows as matrix sources (tested)
- ✅ Generative composition engine: 5 styles, theory-aware, seeded, bar-locked, Evolve, follow keys; offline phrase = live playback; capture to MIDI (tested)
- ✅ Generative preset creator: 8 categories, constrained, auditioned, never silent or clipping, seeded (tested)
- ✅ Note capture of everything played → clips in saved state → Standard MIDI File (C++ tested)
- 🟡 Sequencing page (step slider bank, LEDs, capture/export, preset creator) and Swift bridge (awaiting compile)
- ✅ Generated notes as AU MIDI output (Phase 7)
- ⬜ Sequencer as a patchable rack module with CV/gate jacks → Phase 8 panels

## Phase 6 — Effects
- ✅ Effects framework: 8 slots on insert / send A / send B, series or parallel, pre/post-insert sends, wet/dry, output, click-free bypass and type swaps, full latency compensation and a constant reported latency (tested)
- ✅ Delays (stereo, ping-pong, tape echo, BBD), reverbs (plate, hall, room, spring, partitioned convolution with 8 built-in + user IRs), EQs (parametric, graphic), spectral, granular, tape, glitch (stutter, lo-fi), amp & cabinet, modulation (chorus, flanger, phaser, ensemble, tremolo/pan), dynamics (compressor, limiter, gate), stereo width — all measured (tested)
- ✅ Real-time safety: off-thread builds, lock-free adoption and retirement, zero allocations, NaN recovery, silence skipping (tested)
- 🟡 AU latency KVO, tail time, type-aware parameter display, IR library by content hash, Effects page (awaiting compile; register the App Group)
- ✅ vDSP FFT (Phase 10); ⬜ non-uniform convolution partitions → 1.1
- ⬜ True-peak limiting, external sidechain → later

## Phase 7 — MIDI & AUv3
- ✅ MIDI engine in the kernel: CC (7/14/32-bit), bank + PC, RPN 0/1/6 (bend range, fine tune, MPE Configuration), NRPN with increment/decrement, receive channel, velocity curves, MIDI clock in (tempo, start/stop/continue, SPP, sync source), clock out at exact offsets, note MIDI out, MIDI 2.0 UMP (16-bit velocity, 32-bit controllers, per-note bend / controllers / reset) (tested)
- ✅ MIDI Learn engine: ranges, invert, taper-aware, toggle, three relative encodings, pickup, note gates; lock-free mapping table; parameter-change reporting (tested)
- 🟡 MIDI Learn scopes (per-preset state section, global app-group store), hardware profiles (.vekturmidi) with a standard layout, program change → presets, MIDI page, AU MIDI output + MIDI 2.0 protocol (awaiting compile)
- ✅ Host time signature and bar position → generative phrases, stutter grids (tested in C++); 🟡 AU musical-context wiring (awaiting compile)
- 🟡 Standalone: MIDI 2.0 input, virtual MIDI output, Ableton Link host blocks (awaiting compile; LinkKit licence + multicast entitlement still needed)
- ✅ Automated host simulation: load/unload ×50 at random rates/buffers, automation timing, buffer-size invariance, state round trip (tested); `tools/auval.sh`
- 🔵 DAW test matrix on devices: Logic, GarageBand, Ableton Live (Mac), Cubasis, AUM — procedure in docs/DAW_TEST_MATRIX.md, not yet run

## Phase 8 — User interface
- ✅ Panel layouts as data: 33 module panels in 5 cabinets (`tools/panels.py` → `VekturCore/Resources/Panels.json`, shared with the web build), every control validated against the registry (tested)
- ✅ Seeded procedural wear (scratches, edge rub, smudges, rail dust, legend yellowing capped for AA contrast) — same generator in Swift and the web build (tested); `appearance` state section with rack and per-module Wear (tested)
- 🟡 SwiftUI hardware panel kit: painted panels, brushed rails with screws, tolex, slider banks, knobs, lever and rotary switches, jacks, LEDs, gate button, needle VU meters — VoiceOver adjustable, reset action, haptics (awaiting compile)
- 🟡 Rack screen with tap-to-patch cables (catenary, level glow); patch-canvas tiles restyled; older pages repainted (awaiting compile)
- 🟡 13 screens (rack, patch, perform, sequencer, arpeggiator, generative, effects, modulation, mixer, MIDI, presets, settings, diagnostics); sidebar in regular width, tab bar in compact, any screen in its own window (Stage Manager, external display, Mac) (awaiting compile)
- ✅ Web test build: the C++ kernel compiled to WebAssembly (matches native to −167 dB), AudioWorklet host, panel UI, patching, Web MIDI, MIDI Learn, presets (`tools/build_web.sh`, `tools/test_web.mjs`; checked in headless Chrome)
- 🔵 Side-by-side comparison with the reference photos; device passes for size classes, Stage Manager and external display
- ✅ Tilt-reactive metal highlights → done in Phase 10 (CoreMotion sheen)

## Phase 9 — Presets, documentation, export
- ✅ 32 + 32 factory presets (Classic; Modular & Generative): authored as diffs in `tools/factory_presets.py`, gated-RMS gain staging to −20 dBFS, peaks ≤ −1 dBFS, make-up limiter where needed, fingerprint distinctness, cables checked (`test_presets` 6/6). Shared by the app, the AU and the web build
- 🟡 AU `factoryPresets` (number = bank × 32 + index), loading keeps the player's setup; Program Change banks 0/1 factory and 2 user (app, AU and web) (awaiting compile)
- 🟡 Preset library: search, categories, favourites, tags and recents in SwiftData (`PresetMeta`; metadata only) (awaiting compile)
- 🟡 Recording: WAV/AIFF/CAF, 16/24/32f, session rate or 44.1–96 kHz via AVAudioConverter; wait-free kernel tap (✅ tested: bit-exact, zero-alloc, drop accounting, TSan) + utility-QoS writer (awaiting compile). Web: 16/24/32f WAV ✅
- ✅ Export / import: `.vekturpreset`, `.vekturproject`, `.vekturpattern` (versioned JSON, same format from the web build), `.mid` clips, `.vekturmidi` profiles; MIDI CC & automation reference (CSV / Markdown) — web checked in headless Chrome; Swift awaiting compile
- ✅ 38-chapter manual (`tools/manual.py`; parameter tables generated from the registry) bundled in the app and the web build, with contextual help (? / ⌘?) — web checked; Swift awaiting compile

## Phase 10 — Optimisation & release prep
- ✅ HQ oscillator option (2× via the half-band oversampler): −7 to −10 dB alias energy, level unchanged (tested)
- ✅ Oversampler evaluation-order bug fixed (affected HQ and 2×/4× filters under some compilers; found by the sanitizer builds)
- ✅ vDSP FFT on Apple (same interface; portable radix-2 elsewhere), self-test API checked here (portable) and by `Phase10DSPTests` on Apple
- ✅ Profiling method, targets and reference measurements (`docs/PERFORMANCE.md`); 🔵 device figures
- 🟡 Battery Saving Mode, Keep Screen Awake, Background Audio, oscillator/filter quality in Settings; tilt-reactive highlights (CoreMotion) (awaiting compile; web ✅)
- ✅ Accessibility review (`docs/ACCESSIBILITY.md`), privacy manifests for app and AUv3, App Store privacy answers (`docs/APP_STORE_PRIVACY.md`), release checklist (`docs/RELEASE_CHECKLIST.md`)
- ✅ TestFlight builds require the iOS 27 / macOS 27 SDK (Xcode 27+): enforced by a Release build phase
- 🟡 Native identity matched to the web build: aluminium top bar (logo, wordmark, play, rec, tempo, wear, load, MIDI, panic, keyboard, help), app icon and launch screen from the DMT Cymatics logo (awaiting compile)
- ⬜ SIMD across voices → 1.1 (decision and plan in PERFORMANCE.md)
- ⬜ Per-voice swap fade → 1.1 (whole-graph 1.3 ms crossfade is click-free today)
- ⬜ Choice of dial appearances as in Vektur Vortex (queued at the user's request; not started)
