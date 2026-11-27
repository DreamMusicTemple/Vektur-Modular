# DAW Test Matrix (Phase 7)

This matrix covers how Vektur Modular behaves as an AUv3 instrument in real hosts. Each row is run on hardware before a release. The automated half runs on every build:

- `DSPTests/test_host.cpp`: load/unload, formats, automation timing, meters and state at the C API.
- `DSPTests/test_midi.cpp`: the MIDI engine.
- `Packages/VekturKit/Tests/VekturAudioUnitTests`: the real `AUAudioUnit` in-process.
- `tools/auval.sh --stress`: Apple's validator, 3 strict passes plus 50 load/unload cycles (macOS).

Results: ✅ pass · ⚠️ pass with a known host quirk (noted) · ❌ fail · — not applicable. Cells marked "pending" have not yet been run on a device; nothing in this file is reported as passing until it has been.

## Hosts and versions

| Host | Platform | Version tested | MIDI protocol | MIDI out | Notes |
|---|---|---|---|---|---|
| Logic Pro | macOS | pending | MIDI 2.0 (UMP) | ✅ supported (MIDI FX / "MIDI-controlled effects" routing) | AUv3 loads out-of-process |
| GarageBand | iOS / macOS | pending | MIDI 1.0 | — not routable | |
| Ableton Live 12 | macOS | pending | MIDI 1.0 | — Live does not route AU MIDI out | Link test also runs here |
| Cubasis 3 | iPadOS | pending | MIDI 1.0 | ✅ (MIDI routing to another track) | |
| AUM | iOS | pending | MIDI 1.0 / 2.0 | ✅ (MIDI routing matrix) | Host-sync and Link master/slave |
| Standalone app | iOS / macOS | pending | MIDI 2.0 (CoreMIDI UMP) | ✅ virtual source "Vektur Modular" | |

## Procedure per host

Run every test at 44.1, 48 and 96 kHz (where the host allows), and at the smallest and largest buffer the host offers. Use a project with one instance unless the test says otherwise.

| # | Test | How | Pass criterion | Automated equivalent |
|---|---|---|---|---|
| 1 | Load / unload ×50 | Insert the instrument, play a chord, remove it, 50 times. Logic: also duplicate the track 8×. | No crash, no hang, no leak growth (Instruments → Allocations, the extension's process), no audio after removal | `test_host` lifecycle (50 cycles, random SR/buffers, 3 reallocations each); `auval -r 50` |
| 2 | Render before allocation and oversized buffers | Offline bounce right after insertion; change buffer size during playback | Silence, then sound; never garbage | `test_host`: render-before-allocate, frames > maxFrames → silence |
| 3 | Sample-rate change | Change the project / device rate while playing | Engine reallocates; pitch correct; no crash | `test_host` lifecycle; `AudioUnitTests.testAllocateRenderAndDeallocateRepeatedly` |
| 4 | Buffer-size change | 64 ↔ 2048 frames during playback | Arp/sequencer timing unchanged (bounce and diff the MIDI out, or compare audio onsets) | `test_host`: arp note times identical for buffers of 17…4096 frames |
| 5 | Parameter automation | Draw a step on Master Volume and on VCF B Cutoff; bounce; inspect | Step lands on the drawn sample (±1); ramps smooth | `test_host`: event takes effect at its sample; 400-frame ramp |
| 6 | Automation of hidden/structural parameters | Check the automation list | Effect slot types, sync source, MIDI channel settings are **not** offered (non-automatable) | ParameterRegistry `isAutomatable` |
| 7 | State | Change parameters, cables, mappings, effects; save; quit; reopen | Everything restored, including preset MIDI mappings and the effects IR references | `AudioUnitTests.testFullStateRoundTrip`, `testMIDILearnMapsAControlAndSyncsTheTree`; `test_host` state round trip |
| 8 | Presets | Save a user preset in the host's preset menu; recall it; send Program Change | Recalled; PC with bank MSB 0/1 selects factory presets; bank MSB 2 selects user presets in name order | `MIDIEvent.programChange` (manual in host) |
| 9 | Host tempo & transport | Tempo 87.5; start from bar 3; loop bars 5–8; tempo ramp | Arp, sequencer, generative, synced LFOs and synced delays follow, re-align on loop | `test_sequencing` host-sync suite; `test_midi` clock-out relocation |
| 10 | Time signature | 3/4, 6/8, 7/8 projects; a pick-up bar | Generative phrases are whole bars of the host's meter; stutter "1 bar" grid = one bar | `test_host` meter tests; `AudioUnitTests.testHostTimeSignatureReachesTheEngine` |
| 11 | MIDI 2.0 | Logic/AUM: a MIDI 2.0 controller (or the macOS MIDI 2.0 test source) | 16-bit velocity and 32-bit controller resolution reach the engine; per-note pitch bend affects only that note | `test_midi` UMP suite |
| 12 | MPE | Roli/Linnstrument/Osmose into the instrument, MPE enabled in the host | Per-note bend, pressure and timbre; MCM from the controller sets the zone | `test_midi` RPN/MCM; `test_modulation` MPE |
| 13 | MIDI out | Route Vektur's MIDI out to a second instrument; arp on | Notes arrive in time with the audio (latency-compensated by the host), on the chosen channel; clock out when enabled | `test_midi` MIDI out suite; `AudioUnitTests.testMIDIOutputReachesTheHostBlock` |
| 14 | MIDI Learn | Learn a hardware knob in "This preset" and in "All presets"; save; reload; open a second instance | Preset mapping travels with the project; global mapping applies to every instance and the standalone | `MIDIMappingTests`; `AudioUnitTests.testProfilesApplyAndGlobalMappingsMerge` |
| 15 | Profile import/export | Export a profile in the AUv3, import in the standalone (Files app) | Same mappings | `MIDIMappingTests.testProfileRoundTripAndValidation` |
| 16 | Latency & tail | Insert a limiter + convolution; bounce a short note | The host compensates the reported latency (bounce aligns with a reference); reverb tail is not cut off | `test_effects` routing; AU `latency` / `tailTime` |
| 17 | CPU / overload | 4 instances × 16 voices at 64 frames | No dropouts on the reference devices (iPad Pro M2, iPhone 13, Mac mini M1); host CPU meter recorded here | `render_demo` load figures |
| 18 | Background / interruption | iOS: phone call, Siri, app switch with AUM in the background | Recovers; no stuck notes | Standalone `handleInterruption` |
| 19 | Multiple instances | 8 instances with different presets | Independent state; global MIDI mappings shared; no cross-talk | — |
| 20 | MIDI flood | Send a dense CC/clock/note stream (MIDI Monitor "flood", or a sequencer at 1/128) | No dropouts, no stuck notes, monitor counts rise | `test_midi` flood + zero-allocation test |

## Ableton Link (standalone app)

| # | Test | Pass criterion |
|---|---|---|
| L1 | Enable Link with Live (Mac, same Wi-Fi) and AUM | Peers shown; tempo changes either side follow within one beat |
| L2 | Start/stop sync | Starting Live starts Vektur's arp on the next bar of the 4-beat phase |
| L3 | Phase | Vektur's arp notes line up with Live's metronome (record both, < 3 ms offset after output-latency compensation) |
| L4 | Network loss | Turn Wi-Fi off and on: keeps running at the last tempo, rejoins |

Link needs `LinkKit.xcframework`, which is licensed by Ableton and not vendored. It also needs the local-network permission (in `project.yml`) and, on iOS, the multicast entitlement granted to the team.

## Known host quirks (fill in while testing)

| Host | Quirk | Workaround / status |
|---|---|---|
| Ableton Live | Does not route AUv3 MIDI output | Documented; use the standalone app's virtual MIDI source |
| GarageBand | No MIDI out routing, no MIDI 2.0 | — |
| Logic Pro | Sends MIDI 2.0 only when the AU reports `audioUnitMIDIProtocol = ._2_0` (we do) | — |
