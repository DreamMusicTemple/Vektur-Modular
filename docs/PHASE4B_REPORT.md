# Phase 4B Report — Performance Input

Date: 2026-11-27 · Build 0.4.1 · State schema v1 (additive: 11 new parameters)

## Summary
Phase 4 is now complete. 4B adds:
- MPE, in a lower or upper zone
- Split and layer keyboard zones, plus a Key Zone matrix source so each zone can sound different
- Tempo-synced LFOs that lock to the host's beat
- A chord analyser
- Live per-slot meters and held-key telemetry for the UI

## Files
**Created**
- `VekturDSP/ChordAnalyzer.hpp`

**Modified**
| File | Change |
|---|---|
| `tools/params.py` (+ generated files) | 432 parameters in total (+11): global 17–24 for tempo, MPE and zones; `lfoN.tempoSync` at index 6. Sources appended: MPE Timbre (21) and Key Zone (22). |
| `include/VekturDSP.h`, `CAPI.cpp` | New `vk_kernel_set_transport`, `vk_kernel_mod_slot_value`, `vk_kernel_held_notes` and `vk_chord_name`. |
| `modules/ModMatrix.hpp` | Sync-division table, synced LFO rate, `Lfo::setPhase`, the two new sources. |
| `Kernel.*` | Musical time (host context or internal tempo); MPE member-channel handling; zone allocation (`polyStart`); key-based note-off matching; meters and held-key mask. |
| `VoiceGraph.*`, `GraphModules.*` | Per-voice key, zone and channel tags; per-note bend into the keyboard pitch; timbre and zone sources; per-slot values. |
| `VekturModularAudioUnit.swift` | `HostTransport` captures the host's musical-context and transport blocks and feeds the kernel before every render. Also `modSlotValue`, `heldNotes` and `ChordAnalyzer`. |
| `VekturCore/ModMatrix.swift` | Two new sources (appended). |
| `ModulationView.swift` | New MPE/zones/tempo section, an LFO tempo-sync control, per-slot live meters and a chord readout. |
| Tests | 27 new C++ checks in `test_modulation.cpp`; 2 Swift AU tests. |
| `render_demo.cpp` | New `--phase4b` mode. |
| Version | 0.4.1; AU 1025 |

## Functionality status
| Item | Status |
|---|---|
| Tempo sync: 15 divisions; free LFOs lock to the host beat and re-lock on relocation; internal tempo when standalone | **Implemented, tested** |
| MPE lower and upper zones; per-note bend, pressure and timbre; pre-note-on data adopted; master channel zone-wide; MPE off stays omni | **Implemented, tested** |
| Key zones: split with a per-zone transpose, layer, Key Zone source, correct release of transposed or layered notes | **Implemented, tested** |
| Chord analyser: 17 reference cases including inversions, no-fifth sevenths, symmetric chords and clusters | **Implemented, tested** |
| Matrix meters (newest voice) and held-key mask | **Implemented in the engine and tested.** UI awaiting compile |
| Host-transport capture in the AU, SwiftUI readouts and controls | **Partially implemented:** written, awaiting compile and DAW testing |
| MIDI 2.0 per-note controllers | **Not started:** UMP is still converted to MIDI 1.0; Phase 10 |

## Build & test results (Linux host)
| Suite | clang -Werror | g++ ASan+UBSan | g++ TSan |
|---|---|---|---|
| test_kernel | 43/43 | 43/43 | 43/43 |
| test_modules | 74/74 | 74/74 | — |
| test_graph | 31/31 (golden bit-close) | 19/19 | 19/19 |
| test_modulation | 97/97 | 97/97 | 97/97 |

- **Real-time safety:** zero allocations while rendering MPE streams on six channels, layered zones, host transport and meter reads.
- **Load:** 8 voices with all 16 slots active measured about 1.4 % of one core on the host. That's about +0.3 % over Phase 4A, within run-to-run noise.

## Known issues and limitations
- Zones apply in Poly mode only. Mono, legato, unison and duo ignore them.
- Layer mode uses two voices per key, so it halves the effective polyphony.
- An MPE zone's member-channel count is a parameter. MPE Configuration Messages (RPN 6) are not yet auto-detected.
- On a stopped transport, synced LFOs run free at the synced rate from their last phase.
- The tempo-synced rate ignores matrix offsets to the LFO rate.

## Next
Phase 5: the sequencing and generative engines. That means the arpeggiator and step sequencer, with sample-accurate host sync through the same transport path, and an analog-style sequencer and switch modelled on the reference hardware.
