# Phase 9 Report — Presets, documentation, export

Date: 2026-11-27 · Build 0.9.0 (released together with Phase 10 as 1.0.0) · State schema v1 (unchanged; new files are versioned on their own)

## Summary
- **64 factory presets** in two banks of 32, *Classic* and *Modular & Generative*. They are real patch data and use every part of the instrument: cables, the matrix, the sequencer, the arpeggiator, the generator and the effects. All 64 are gain-staged and checked automatically to sound distinct. The app, the AUv3 and the web build load identical sounds from one generated file.
- **Preset library.** Search, categories, favourites, tags and recents (SwiftData metadata), the preset creator, and Program Change banks 0/1 (factory) and 2 (user).
- **Recording.** WAV, AIFF or CAF at 16-bit, 24-bit or 32-bit float, at the session rate or converted to 44.1–96 kHz. The audio thread writes into a wait-free tap with no allocation, and the tap is tested to be bit-exact.
- **Export and import.** Preset, project and pattern files in one versioned format, written identically by the app and the web build. MIDI clips export as `.mid` files, and MIDI profiles as `.vekturmidi`.
- **MIDI CC and automation reference** screen with CSV and Markdown export.
- **A 38-chapter manual** (about 9,500 words of prose, plus parameter tables generated from the registry) bundled in the app and the web build, with **contextual help** on every screen (? button, ⌘?).
- Also completed, from the mid-phase request:
  - the DMT Cymatics logo in About and at the start of the top row;
  - a hide/show keyboard button;
  - drag-to-patch in either direction, with animated cables (spring sag, sway, signal-flow dashes);
  - long-press or right-click jack menus.

  These are in the app, the AUv3 and the web build.

## Factory presets
- Presets are authored as diffs from the defaults in `tools/factory_presets.py`. Effect settings are written in real units and normalised through the engine's own catalogue (`DSPTests/fxmap`). Each effect slot carries all 12 of its type's defaults, so a slot sounds the same wherever it is loaded.
- **Gain staging.** The gated RMS (the windows within 20 dB of the loudest) of a per-category audition targets −20 dBFS, with peaks ≤ −1 dBFS. Twenty-one quiet presets get an automatic make-up Limiter in slot 8. Measured range: −23.2 to −19.9 dBFS.
- **Distinctness.** A fingerprint of 10 octave-band energies, the spectral centroid and an 8-point envelope. The closest pair (Rubber Sub / Sub Pressure) is 0.39 apart, above the 0.25 threshold.
- **`test_presets`** (6/6) checks finite output, the loudness window, peaks, distinctness, that every cable is accepted by the engine, and that every identifier exists.
- **AU.** `factoryPresets` returns 64 presets (number = bank × 32 + index). Loading one keeps the player's setup (MIDI channel, zones, curves, sync, tempo), clips, appearance and the canvas layout.

## Recording
- **Kernel.** `Recorder.hpp` is a single-producer/single-consumer ring of 2^19 stereo frames, allocated when recording first starts (UI thread). `Kernel::render` copies the final output into it after the safety limiter. When the ring is full, whole frames are dropped and counted; the audio thread never waits.
- **App and AU.** `RecordingService` drains the ring every 40 ms on a utility-QoS queue into `AVAudioFile`, using `AVAudioConverter` (Mastering quality) when the rate differs. Files go to Documents/Recordings, which is visible in the Files app. The UI shows elapsed time, dropped frames (if any) and a share button.
- **Web.** The worklet streams blocks to the page, which writes a 16-bit, 24-bit or 32-bit float WAV.
- **Tests** (`test_phase10`):
  - the recording is bit-identical to the output (102,400 frames);
  - the render allocates nothing with the tap armed;
  - nothing is dropped when the ring is drained;
  - overflow is counted exactly (10,240 frames);
  - with a concurrent reader under TSan, every frame is either read or counted.

## Exchange formats
`ExchangeFile { format: "vektur.preset|project|pattern", schemaVersion, writtenBy, name, state }`:
- **A preset** holds the sound without the setup parameters, clips or appearance.
- **A project** holds everything.
- **A pattern** holds only `seq.*` and `arp.*`, and importing one changes nothing else.

A file from a newer schema is refused with a clear message; a file of the wrong kind is named as such. The web build writes the same structure: cables become `{from:{module,port}, to:{module,port}, gain}` with rack keys. A project export → reset → import round trip restores the exact state (headless Chrome).

## Manual and help
`tools/manual.py` writes `docs/manual/01…38-*.md` plus a README, the bundled `VekturCore/Resources/Manual` (with `index.json`) and `web/manual.json`. Each chapter names the screens it documents, and the ? button opens the right one. The native reader renders headings, lists and tables, and has search and previous/next. The web reader has a table of contents and search.

## Test results
| Suite | Result |
|---|---|
| C++ (10 suites: kernel, modules, graph, modulation, sequencing, effects, midi, host, presets, phase10) | all pass; ASan + UBSan and TSan clean |
| `tools/test_web.mjs` (Node, wasm) | 6/6 |
| Headless Chrome (Phase 9/10 web features) | 12/12: contextual help, 38 chapters, tables, search, MIDI reference, CSV export, project file format, exact import round trip, Battery Saving, HQ restore, 16-bit WAV, no page errors |
| Swift (`Phase9Tests`, `Phase10DSPTests`, existing suites) | written; **not compiled here** (no Swift toolchain in this environment) |

## Not done / pending
- Swift compile and device run (as in every phase).
- MIDI-clip export from the web build.
