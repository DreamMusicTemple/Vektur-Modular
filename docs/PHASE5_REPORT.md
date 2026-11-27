# Phase 5 Report — Sequencing & Generative

Date: 2026-11-27 · Build 0.5.0 · AU version 1280 · State schema v1 (additive: 149 new parameters, optional `clips` section)

## Summary
Phase 5 adds four note and sound generators. All of them run inside the real-time kernel, on one shared musical clock:

- **Arpeggiator**
  - 14 patterns over 1–4 octaves
  - Straight or Euclidean rhythm
  - Probability, ratchets, swing, gate 5–200 %
  - Three velocity modes, latch and sustain-pedal hold
  - Pattern chaining (up to four patterns, N passes each)
  - Transposition by the step sequencer
- **Analogue-style step sequencer**
  - 16 steps and three rows (A/B/C)
  - Gate per step: Off, On, Tie or Skip
  - Probability and 1–4× ratchets per step
  - Five directions, scale quantiser, four run modes (including Key)
  - Rows A/B/C are also new matrix sources
- **Generative composition engine**
  - Five styles; theory-aware and seeded
  - Locked to the bar grid; Evolve mutates the phrase gradually
  - Follow keys / hold to play
  - Offline capture and MIDI export are identical to what plays
- **Generative preset creator**
  - Eight categories of constrained recipes
  - Each sound is auditioned offline and retried until it is audible and below clipping
  - Deterministic per seed

**Capture** records everything the instrument plays (keys and all three generators) on the beat grid. A capture becomes a clip that is kept with the project and exported as a Standard MIDI File.

## Design decisions
- **All settings are parameters** (modules 36–38, the blocks reserved in ARCHITECTURE §6). They save, restore, recall with presets and automate like every other control, with no new state plumbing. Seeds are not automatable.
- **Shared clock.** While the host transport moves, the clock is the host's beat position. A locate, a loop or a transport start re-aligns all generators (jumps over 0.01 beats). When the transport is stopped or absent, a free clock runs at the current tempo.
- **Sample-accurate.** The render loop splits spans at every generator event, as well as at MIDI events and control-block boundaries. The tests measure 0.000-sample error at a block size of 256.
- **Counter-based randomness.** Every random choice is hash(seed, tick). Patterns are therefore identical on replay, after a host relocation, and between live playback and offline capture.
- **Deviation from ARCHITECTURE §3.1.** The generative engine was planned as Swift at control rate. It is C++ instead (`VekturDSP/seq/Generative.*`), because that makes it testable here, keeps it allocation-free and makes it sample-accurate on the host grid. It regenerates at phrase boundaries on the render thread; generation is bounded.
- **Key routing.** When the arp is on, the sequencer is in Key mode, or the generative engine is in Hold to Play, physical keys go to the generators instead of the voices. When none of these is active, any key still held starts sounding normally.
- **Every note-on has exactly one note-off.** This holds for the voices and for capture. Overlapping same-pitch notes (gate over 100 %, ties) re-articulate in place, which is inaudible because the voice retriggers in place.

## Files
**Created**
| File | Purpose |
|---|---|
| `VekturDSP/seq/SeqCommon.hpp` | Divisions, 16 scales, quantiser, counter RNG, Euclid, swing, `NoteQueue` |
| `VekturDSP/seq/Arpeggiator.hpp` | Arpeggiator |
| `VekturDSP/seq/StepSequencer.hpp` | Step sequencer |
| `VekturDSP/seq/Generative.hpp/.cpp` | Phrase generator and player |
| `VekturDSP/seq/PresetGenerator.cpp` | `vk_preset_generate` |
| `VekturDSP/seq/SeqCAPI.cpp` | Capture, status, offline phrase and chords, SMF writer |
| `VekturCore/NoteClip.swift` | Clip model (pairing, sanitising) |
| `VekturAudioUnit/Sequencing.swift` | Status, capture, phrase/chords, clips, MIDI export, preset creator |
| `VekturUI/SequencerView.swift` | New "Sequencing" page |
| `DSPTests/test_sequencing.cpp` | 91 checks |
| `Tests/VekturCoreTests/NoteClipTests.swift` | Swift tests |

**Modified**
| File | Change |
|---|---|
| `tools/params.py` (+ generated) | 581 parameters (+149): arp 24, seq 13 + 16 × 6, gen 16. Sources 23–25 (Seq Row A/B/C) appended. Modules 36–38 are excluded from matrix destinations. |
| `Kernel.hpp/.cpp` | Generator clock, span splitting, physical-key layer (`noteOn`/`soundOn` split, sustain hold, direct-key tracking), capture ring, status atomics. Also a voice-mode fix (see Known issues). |
| `include/VekturDSP.h` | Phase 5 API, `VK_CMD_CAPTURE` |
| `modules/ModMatrix.hpp`, `VekturCore/ModMatrix.swift` | Three new sources |
| `InstrumentState.swift` | Optional `clips` section (additive; malformed → empty) |
| `VekturModularAudioUnit.swift` | State access helpers for the new extension |
| `DiagnosticsConsoleView.swift` | Sequencing page |
| `DSPTests/CMakeLists.txt`, `render_demo.cpp` (`--phase5`) | Build and audition |
| Version | 0.5.0; AU 1280 |

## Functionality status
| Item | Status |
|---|---|
| Arp: patterns, octaves and direction; Euclid with rotation; probability; ratchets; swing; gate; velocity modes; latch; pedal hold; chaining; sequencer transpose; Auto / beat-locked / key restart | **Implemented, tested** |
| Arp host sync: 1/8 grid at 128 BPM, 0.000-sample error; locate re-aligns; runs free when stopped | **Implemented, tested** |
| Sequencer: rows, range, quantiser, directions, skip (zero time), tie (look-ahead), rests, per-step probability and ratchet, mod-only, Run / Transport (song-locked) / Key | **Implemented, tested** |
| Generative: 5 styles; in scale; I … V loop with cadence; voice-led chords; bass approach notes; evolve; follow keys / hold; live = offline | **Implemented, tested** |
| Capture → clip → `.mid` (format 0, 480 PPQ, tempo meta; orphans and open notes handled); clips in saved state | **Implemented, tested** (C++); Swift awaiting compile |
| Preset creator: 8 categories × 12 seeds never silent on C2/C4/C6, never clipping, first candidate passed every time, deterministic | **Implemented, tested** |
| Sequencing page: LEDs, 16-step slider bank, gate/probability/ratchet per step, chord readout, capture/export, preset creator; VoiceOver labels and adjustable actions | **Partially implemented:** written, awaiting compile |
| Generated notes sent as AU MIDI output to the host | **Not started:** Phase 7 (MIDI engine) |
| Sequencer as a patchable rack module (CV/gate jacks); analogue-switch sequencing from the sequencer clock | **Not started:** Phase 8 panel work. The rows already reach every destination through the matrix. |

## Build & test results (Linux host)
| Suite | clang -Werror | g++ ASan+UBSan | g++ TSan |
|---|---|---|---|
| test_kernel | 44/44 | 44/44 | 44/44 |
| test_modules | 74/74 | 74/74 | — |
| test_graph | 31/31 (goldens bit-close) | 19/19 | 19/19 |
| test_modulation | 97/97 | 97/97 | 97/97 |
| test_sequencing | 91/91 | 91/91 | 91/91 |

- **Real-time safety:** zero allocations across 2000 blocks. This covers all three generators running together, the arp's Euclid rhythm and ratchets, generative phrase regeneration, host relocations and capture.
- **Thread safety:** under TSan, a UI thread drained capture and status concurrently with rendering, without errors.
- **Stuck-note fuzz:** 3000 blocks of random keys, modes, rates, directions, gates, voice modes, locates, transport toggles and pedal. No voice was left held, and every captured note-on was paired.
- **Load:** the arp, sequencer and generative engine together with up to 8 sounding voices measured 5.0 % of one core. The voices dominate this figure (Phase 4B measured 1.4 % for 8 voices).
- **Audition:** `render_demo vektur_phase5_demo.wav --phase5` renders 60 s in five sections. The two generative sections play on generated presets.

## Known issues and limitations
- **Fixed pre-existing bug:** a voice-mode change arriving in the same buffer as notes silenced those notes. The mode is now applied before the buffer's events, and a regression test covers it.
- Generative edits take effect at the next phrase boundary.
- A Tie step holds the previous note at its pitch. It does not glide to the tie step's Row A pitch.
- In the Random Walk direction, a note before a Tie is not lengthened, because ties look ahead and a random walk has no fixed next step.
- Arp patterns use up to 32 held keys. Each generator queue holds 256 pending events; on overflow the earliest event plays early rather than being dropped.
- Clips keep at most 16 per project and 8192 notes per clip.
- In the app, MIDI files are exported through the system file exporter. Whether that is available inside an AUv3 extension depends on the host; this is to be checked in the Phase 7 DAW matrix.

## Next
Phase 6, the effects framework and the effect families.
