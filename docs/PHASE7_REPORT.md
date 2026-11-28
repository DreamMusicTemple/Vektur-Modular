# Phase 7 Report — MIDI & AUv3

Date: 2026-11-27 · Build 0.7.0 · AU version 1792 · State schema v1 (additive: 7 new parameters, optional `midi` section)

## Summary
Phase 7 builds the MIDI engine into the real-time kernel, so the AUv3, the standalone app and the tests all share one sample-accurate implementation. It covers:
- **Input:** MIDI 1.0 and MIDI 2.0 (UMP): controllers at 7, 14 and 32 bits; bank and program change; RPN (bend range, fine tune, MPE configuration) and NRPN; receive channel and velocity curves.
- **Clock:** MIDI clock in with start, stop, continue and SPP, and a choice of sync source. MIDI clock out at exact sample offsets.
- **Output:** generated and played notes as AU MIDI output.
- **MIDI 2.0 per-note:** high-resolution velocity, per-note pitch bend, per-note controllers and per-note reset.

MIDI Learn has these features:
- range, inversion and taper-aware mapping;
- toggle, three relative encoder formats, pickup, and note gates;
- two scopes: per preset (saved in the state) and global (device-wide, shared by the app and the extension);
- portable JSON hardware profiles, plus a standard General MIDI layout.

The host's time signature now drives generative phrase length and the stutter bar grids. The standalone app gains MIDI 2.0 input, a virtual MIDI output and an Ableton Link transport. The Link code compiles in once LinkKit is added.

The DAW matrix is written as a procedure (`docs/DAW_TEST_MATRIX.md`). Its automated half runs in `test_host` and `tools/auval.sh`. **It has not been run on any device yet.**

## Design decisions
- **MIDI lives in the kernel, not in Swift.** Every message is handled at its sample offset on the render thread, with no allocation, no lock and bounded work. Swift only edits mappings and drains a UI queue. RPN, learnt controls and MCM change parameters inside the kernel; a per-address atomic dirty bitset reports them, and a 30 Hz utility-queue drain pushes them into the AU parameter tree so hosts and the UI follow.
- **MIDI 2.0 is passed whole.**
  - The adapter turns type-4 UMP into a `VK_EVENT_UMP` instead of down-converting, so 16-bit velocity and 32-bit controllers keep their resolution. A 25.000 % controller value arrives exactly; a 7-bit CC cannot express it.
  - The AU now reports `audioUnitMIDIProtocol = MIDI 2.0`. MIDI 1.0 hosts keep sending bytes.
  - In MPE mode, UMP is down-converted so zones keep MIDI 1.0 semantics, but high-resolution velocity survives.
- **Mappings are keyed by parameter identifier**, not address. They are published through a lock-free triple buffer of 256 entries. The kernel normalises along each parameter's taper, so a cutoff mapping sweeps in octaves.
  - Preset mappings override global mappings that use the same control or the same parameter.
  - Per-note expression on MPE member channels is never routed to mappings, so CC 74 timbre cannot move a global cutoff mapping.
  - Sustain is never captured by Learn.
- **Pickup** engages when the control comes within 2 % of the parameter's value, or crosses it. It re-arms when anything else (host, automation, UI) moves the parameter.
- **Clock in** smoothing converges fast and then settles: α = 0.5 for the first 24 ticks, then 0.08. ±40-sample jitter at 96 BPM reads 96.009 BPM.
  - Between ticks, the beat is interpolated but never runs past the next tick.
  - *Auto* sync takes the host transport first, then MIDI clock (a tick within 0.5 s), then the Tempo control.
- **Clock out** places ticks on the generators' own beat grid at the first sample at or after each tick, so it is locked to the host's beats when synced. It re-aligns after a host loop or locate.
- **Time signature.** `vk_kernel_set_transport_ex` passes beats per bar and the bar's downbeat.
  - Generative phrases are anchored to host bar lines, including pick-up bars, and last whole bars of the host's meter. The half-bar accent applies only to even meters.
  - Stutter's "1 bar" and "2 bars" grids follow the meter.
  - A host that gives no meter is treated as 4/4.
- **Program change** selects presets: bank MSB 1 is the user presets; bank 0 is the factory presets, or the user presets while the build ships none. Presets are in name order. A preset change is never applied from the render thread.
- **AU MIDI output.** The Objective-C++ adapter retains the host's `AUMIDIOutputEventBlock` (set at `allocateRenderResources`, cleared at deallocate). After each buffer it calls the block with timestamp plus offset. This is the one call out of the kernel on the render thread, as Apple specifies.
- **Standalone app as host.**
  - It installs the unit's musical-context and transport blocks: Link when enabled, otherwise "no host context", so the engine runs on its own tempo.
  - It forwards CoreMIDI event lists unchanged to `scheduleMIDIEventListBlock`, from a MIDI 2.0 input port.
  - It exposes the unit's MIDI out as a virtual source, "Vektur Modular", which is never looped back into the input.

## Files
**Created**
- `VekturDSP/KernelMidi.cpp`, `VekturDSP/midi/MidiLearn.hpp`
- `VekturCore/MIDIMappings.swift`
- `VekturAudioUnit/MIDI.swift`
- `VekturEngine/LinkTransport.swift`
- `VekturUI/MIDIView.swift`
- `DSPTests/test_midi.cpp` (103 checks), `DSPTests/test_host.cpp` (24 checks)
- `Tests/VekturCoreTests/MIDIMappingTests.swift`
- `docs/MIDI.md`, `docs/DAW_TEST_MATRIX.md`, `tools/auval.sh`

**Modified**
| File | Change |
|---|---|
| `tools/params.py` (+ generated) | 736 parameters (+7): `global.syncSource`, `receiveChannel`, `velocityCurve`, `programChange`, `midiOut`, `midiOutChannel`, `midiClockOut`, all non-automatable except Velocity Curve |
| `include/VekturDSP.h` | `VK_EVENT_UMP` + `ump[2]` in `VKEvent`; mapping, learn, UI-event, status, MIDI-out and `set_transport_ex` API |
| `Kernel.hpp/.cpp` | Sync-source selection, clock-driven transport, per-span MIDI out position, MIDI-changed reporting, hi-res velocity in `soundOn`, meter. MIDI handling moved to `KernelMidi.cpp`. |
| `ParameterStore.hpp` | Taper-aware `normalise` / `denormalise`, `isRegistered` |
| `seq/Generative.*`, `seq/SeqCAPI.cpp`, `fx/Effect.hpp`, `fx/Stutter.cpp` | Meter-aware phrases, accents, chord rhythm and bar grids |
| `graph/VoiceGraph.hpp` | `timbre()` accessor (tests) |
| `AURenderAdapter.mm` | Type-4 UMP passed whole; type-1 system UMP; MIDI output block |
| `CAPI.cpp` | Phase 7 C functions |
| `VekturModularAudioUnit.swift` | MIDI 2.0 protocol, `midiOutputNames`, output-block wiring, mapping publication on state load and allocation, MIDI drain. `HostTransport` moved to `MIDI.swift` and now passes the meter. |
| `InstrumentState.swift` | Optional `midi` section (malformed → none) |
| `MIDIInputService.swift` | MIDI 2.0 port, event-list forwarding (MIDI 1.0 fallback including system messages), virtual output source, render-safe output block |
| `StandaloneAudioEngine.swift`, `App/VekturModularApp.swift` | Link host blocks, MIDI out, MIDI 2.0 connection order |
| `DiagnosticsConsoleView.swift` | MIDI page |
| `AudioUnitTests.swift` | 6 MIDI tests |
| `project.yml` | 0.7.0 / AU 1792. Local-network usage string. `.vekturmidi` type declarations. macOS network entitlements. Multicast entitlement and LinkKit dependency as commented steps. The AU can write user-selected files (profile export). |
| `render_demo.cpp` | `--phase7` |

## Functionality status
| Item | Status |
|---|---|
| MIDI engine: CC 7/14/32-bit, bank + PC, RPN 0/1/6, NRPN ± inc/dec, receive channel, velocity curves | **Implemented, tested** |
| MIDI clock in (tempo, jitter, start/stop/continue, SPP, sync source, timeout) and out (24 ppqn, exact offsets, relocation) | **Implemented, tested** |
| MIDI out of generated and played notes, channel, balance, cap | **Implemented, tested** |
| MIDI 2.0: 16-bit velocity, 32-bit controllers, RPN/NRPN, per-note bend / controller 74 / reset, PC with bank, MPE down-conversion | **Implemented, tested** |
| MIDI Learn engine: all modes, pickup, taper, invert, channel filter, NRPN, learn reporting, MPE isolation | **Implemented, tested** |
| Host meter → generative phrases, accents, stutter grid; pick-up bars | **Implemented, tested** in C++ |
| Host simulation: load/unload ×50 at 7 rates and 9 buffer sizes with reallocation, render before allocation, oversize buffers, automation at its sample, ramps, buffer-size invariance, SR scaling, state round trip of all 736 parameters | **Implemented, tested** |
| Learn scopes, profiles, standard layout, program change → presets, tree sync, MIDI page, AU MIDI out, MIDI 2.0 protocol, meter from `musicalContextBlock` | **Partially implemented:** written, awaiting compile. Swift tests are written (`MIDIMappingTests`, 6 AU tests). |
| Standalone MIDI 2.0 input, virtual MIDI out | **Partially implemented:** written, awaiting compile |
| Ableton Link | **Partially implemented:** code behind `canImport(LinkKit)`. Blocked on the LinkKit licence and Apple's multicast entitlement. |
| DAW matrix on Logic, GarageBand, Live, Cubasis, AUM | **Not started on devices.** Procedure and pass criteria are written; `tools/auval.sh` is ready. |
| MIDI 2.0 output (UMP) and MIDI-CI / property exchange | **Not started.** Output is MIDI 1.0. |
| Per-note MIDI Learn sources, MIDI-learnable mod-matrix amounts | **Not started.** Macros cover the matrix. |

## Measured results (Linux host, 48 kHz, `test_midi` / `test_host`)
- **RPN:**
  - 12 st + 50 ct gives a bend range of 12.500.
  - Fine tune 96/0 gives +50.00 ct.
  - The null RPN ignores data entry.
  - MCM gives a lower zone with 7 members, or an upper zone with 4; zone-off is ignored on the other master.
  - RPN 0 on a member channel sets the per-note range.
- **NRPN 130:** data 64/0 maps to 50.003 %; the LSB refines it to 50.778 %. Two increments and one decrement give +1 step exactly.
- **Mappings:**
  - The 14-bit pair 100/64 is exact to 1e-3.
  - Inverted and 25–75 % ranges are exact.
  - A CC 64 into a log cutoff gives 20·1000^(64/127) Hz to 1e-4.
  - All three relative encodings step correctly, and are clamped to the range.
  - Toggle works on CC and on notes.
  - Pickup ignores the control below the value, picks up when it crosses, follows, re-arms after an external change, and picks up again.
  - Out-of-range addresses, unknown sources and NaN ranges are rejected.
- **Velocity:** with Linear, Soft, Hard and Fixed curves, a velocity of 64 becomes exactly the expected 7-bit-quantised voice velocity. A UMP velocity of 0x8000 arrives as 0.500008, unquantised.
- **Clock in:**
  - Ticks every 1250 samples measure 96.000 BPM; with ±40-sample jitter, 96.009 BPM.
  - The beat follows the ticks (1.7067 vs 1.7067).
  - SPP 16 followed by Continue resumes at beat 4.
  - Internal sync ignores the clock. Auto prefers the host, then returns to the clock when the host context goes away.
  - The clock is inactive 0.5 s after the last tick.
- **Clock out:**
  - At 120 BPM, ticks are exactly 1000 samples apart (49 in 1.0 s).
  - Following a host that starts at beat 0.1, the first tick is at sample 600 (beat 0.125), then every 1000 samples.
  - After a loop to beat 0, a tick lands on offset 0.
- **MIDI out:**
  - Arp notes are exactly 6000 samples apart on channel 4; every note-on has its note-off.
  - In "All Notes" mode, played keys are echoed at their own offsets (100 and 50).
  - The output is capped at 512 events per buffer under a 4000-event flood.
- **UMP:**
  - Per-note bend moves only its own note, by +24 st.
  - Per-note controller 74 sets 0.25 on its own note only; the reset restores it.
  - A 32-bit CC holds 25.000 %, and a 32-bit mod wheel reads 1.0.
  - The MIDI 2.0 RPN sets the bend range, and the MIDI 2.0 NRPN drives a mapping.
  - Program change carries its bank; type-2 note-ons play.
  - MPE through UMP keeps per-channel notes and their hi-res velocity. Channel bend measures 47.994 st.
- **Host simulation:**
  - 50 create, allocate, reallocate ×3, destroy cycles at 22.05–192 kHz and 16–4096 frames pass with **0 allocations** in any render call.
  - A master-volume event at sample 10 077 changes the output at exactly that sample; −6 → −40 dB measures −34.00 dB.
  - A 400-frame ramp is at 44.8 half-way and exactly 80.000 at its end.
  - Arp note times are identical for buffers of 17, 64, 256, 441, 1024 and 4096 frames.
  - The arp grid is 6000 samples at 48 kHz, on the 5512.5-sample grid (±1) at 44.1 kHz, and 12 000 at 96 kHz.
  - Level is independent of buffer size (0.000 dB).
  - Meters: 3/4 and 6/8 give 3 beats per bar, 7/8 gives 3.5, and phrases scale with them. A 3/4 pick-up bar puts the downbeat at beat 3.5.
- **Real-time safety:**
  - **0 allocations** over 300 buffers of 64-event floods that mix notes, CCs, clock, random UMP and bend, with 200 mappings of every mode, MIDI out and clock out on.
  - 3000 buffers with concurrent mapping publication, learn arming, UI drain and status reads are clean under TSan.

### Test suites
| Suite | clang -Werror | g++ ASan+UBSan | g++ TSan |
|---|---|---|---|
| test_kernel | 44/44 | 44/44 | 44/44 |
| test_modules | 74/74 | 74/74 | 74/74 |
| test_graph | 31/31 (goldens bit-close) | 19/19 | 19/19 |
| test_modulation | 97/97 | 97/97 | 97/97 |
| test_sequencing | 91/91 | 91/91 | 91/91 |
| test_effects | 74/74 | 74/74 | 74/74 |
| test_midi | 103/103 | 103/103 | 103/103 |
| test_host | 24/24 | 24/24 | 24/24 |

Swift (`swift test`, Xcode): not run here — no Swift toolchain on this host. The new Swift is reviewed by hand against the APIs, but it is unverified until it compiles.

## Demo (`render_demo --phase7`, 48.5 s)
1. **Arp slaved to MIDI clock.** The clock runs at 100 BPM, accelerates to 132 BPM, stops, sends a song position (bar 3) and continues.
2. **MIDI Learn.** CC 74 sweeps the cutoff along its taper. An endless encoder walks the resonance up and back down. A pad note toggles VCO 2 up a fifth and back.
3. **RPN bend range of 12 st**, then octave-wide bends.
4. **MIDI 2.0.**
   - A 16-step crescendo in 16-bit velocity.
   - A chord in which one note alone bends up a fourth (per-note pitch bend).
   - The same note's timbre opens (per-note controller 74 → matrix → cutoff).
5. **Host in 3/4 at 132 BPM:** a generative waltz with phrases on the host's bar lines.

## Known issues and limitations
- MIDI output is MIDI 1.0 only; there is no UMP output, and no MIDI-CI.
- Ableton Live and GarageBand do not route AUv3 MIDI output; use the standalone app's virtual source.
- The standalone's output block calls `MIDIReceivedEventList` on the render thread and retains a Swift object there. This is the same accepted exception as `HostTransport`, and is documented.
- `LinkTransport.peers` reports only connected or not connected (LinkKit's C API has no peer count).
- Program change cannot reach factory presets until Phase 9 ships them.
- Global mappings are re-read from the app group whenever an instance allocates. A change made in the app reaches an AUv3 instance that is already running only after the host reinitialises it.
- The MIDI page is functional styling only; the panel look arrives in Phase 8.

## Next
- **Before building:**
  - Register the App Group (from Phase 6).
  - Add `LinkKit.xcframework` and request the multicast entitlement, if Link is wanted in this release.
- Compile on Xcode 16, then fix and run the Swift suites. Run `tools/auval.sh --stress`. Walk through `docs/DAW_TEST_MATRIX.md` on Logic, GarageBand, Live, Cubasis, AUM and the standalone app, filling in the results and host quirks.
- **Phase 8:** the user interface. Hardware panels, all screens, and accessibility.
