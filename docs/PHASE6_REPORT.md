# Phase 6 Report — Effects

Date: 2026-11-27 · Build 0.6.0 · AU version 1536 · State schema v1 (additive: 148 new parameters, optional `effects` section)

## Summary
Phase 6 adds the effects engine and 27 effects in ten families. There are eight slots, which sit on three buses: the insert chain, send A and send B. Each bus runs its slots in series or in parallel. Each send taps the signal before or after the inserts.

Every slot has:
- wet/dry mix
- output gain
- click-free bypass
- a 3 ms crossfade when its effect changes

Every signal path is delay-compensated. The engine reports one constant latency to the host through the AU `latency` property, with KVO.

| Family | Effects |
|---|---|
| Delay | Stereo Delay, Ping-Pong, Tape Echo (3 heads, wow/flutter, saturation, tape age), BBD (clock-dependent bandwidth, companded grit) |
| Reverb | Plate (Dattorro tank), Hall and Room (8-line FDN, three-band decay, early reflections), Spring (dispersive all-pass chirp), **Convolution** (uniformly partitioned FFT, 8 built-in IRs plus user IRs) |
| EQ | Parametric (HPF, low shelf, two peaks, high shelf, LPF), 10-band Graphic (constant or proportional Q) |
| Spectral | STFT phase vocoder: freeze, blur, pitch shift, smear, tilt, spectral gate, phase scatter |
| Granular | Up to 64 grains: size, density, pitch with spread and quantise, position, spray, reverse, freeze, feedback |
| Tape | Saturation with bias, head bump, HF loss per tape speed, wow and flutter, hiss, auto gain |
| Glitch | Stutter (tempo-synced beat repeat: reverse, pitch drop, decay, tape stop; seeded per grid line), Lo-Fi (bits, sample rate, jitter) |
| Amps & cabs | Amp (4 channels, 1–3 anti-aliased gain stages, tone stack, sag), Cabinet (4 boxes × 3 microphones, position, distance) |
| Modulation | Chorus, Flanger, Phaser (2–12 stages), Ensemble, Tremolo / Auto-Pan / Harmonic tremolo; rates can sync to the song position |
| Dynamics & utility | Compressor (soft knee, peak or RMS, sidechain HPF, link, auto makeup), Limiter (3 ms look-ahead brick wall), Gate/Expander, Stereo Width (M/S, bass mono, Haas) |

## Design decisions
- **Generic slot parameters.** Each slot has type, on, mix, output, bus and p1…p12, stored as 0…1. The effect catalogue (`fx/FxSpecs.cpp`) gives each type's parameters their name, unit, curve, range, default and display. The AU's string callback shows "375 ms", "1/8 dotted" or "+3.0 dB" for the slot's current type, and hosts can automate every p.
  - The alternative was a fixed parameter set per effect type. That would mean thousands of parameters, against ARCHITECTURE R-7.
  - The total is now 729. The earlier ≤ 600 target assumed no effects, so ARCHITECTURE §6 is revised to ≤ 800 at v1.0.
- **Structural changes are built off the audio thread.** These are a slot's type, the convolution IR and user IRs.
  - The build happens on a UI or host thread, through `vk_kernel_update_effects`. The AU calls it automatically when a structural parameter changes.
  - Each built effect goes to its slot through an atomic pointer. The audio thread fades the slot out, swaps, and fades in. The retired instance goes to a lock-free graveyard, which the UI thread frees.
  - Memory is allocated only for the effect actually inserted (ARCHITECTURE R-5). Slot types are therefore not automatable.
- **Latency.**
  - Each effect has a fixed latency: Convolution 256, Spectral 2048, Limiter 3 ms, Tape 5 ms (centre of the wow).
  - Each slot's dry path is delayed by its effect's latency. Parallel branches are aligned to the slowest branch. The main path and both returns are aligned to the slowest path. That total is the reported latency.
  - Bypassing a slot does not change the latency. Changing a type or the routing can, and is followed by a short fade of the whole effects output.
- **Exactness.** With no effects inserted, the engine passes audio through bit-exactly, so the Phase 2–5 golden renders are unchanged. 100 % mix gives the effect output exactly. A bypassed slot gives the dry signal, aligned to the latency.
- **Robustness.**
  - A slot whose input has been silent for longer than its tail skips processing.
  - A slot that produces NaN or Inf resets itself and raises the telemetry flag once.
  - Effects run before the master volume, so tails follow the volume.
- **IRs are files referenced by content hash** (ARCHITECTURE §7). User IRs are copied into a shared library, `group.com.dmtcymatics.vekturmodular`, so the app and the extension see the same files. The state stores only the references: slot, SHA-256, name, frames, rate and channels. A project moved to a device without the file falls back to the built-in IR and shows a warning.

## Files
**Created:** in `VekturDSP/fx/`:
- `FxDSP.hpp`, `FFT.hpp`
- `FxSpecs.hpp/.cpp`, `Effect.hpp`, `EffectsInternal.hpp`, `EffectFactory.cpp`
- `EffectsEngine.hpp/.cpp`, `FxCAPI.cpp`
- `Delays.cpp`, `Reverbs.cpp`, `Convolution.cpp`, `EQ.cpp`, `Spectral.cpp`, `Granular.cpp`, `Character.cpp`, `Stutter.cpp`, `Modulation.cpp`, `Dynamics.cpp`

Elsewhere:
- `VekturCore/EffectsState.swift`
- `VekturAudioUnit/Effects.swift`
- `VekturUI/EffectsView.swift`
- `DSPTests/test_effects.cpp` (74 checks)

**Modified**
| File | Change |
|---|---|
| `tools/params.py` (+ generated) | 729 parameters (+148): `fx1…fx8` (17 each) and `fxBus` (12). `FX_TYPES` is permanent. Effect parameters are excluded from matrix destinations. |
| `include/VekturDSP.h` | Effects catalogue, slot control, IR and latency API |
| `Kernel.hpp/.cpp` | Effects after the voice sum and before the master gain. Effects are rebuilt at allocate. A non-finite effect output is reported without resetting the voices. |
| `VekturModularAudioUnit.swift` | Type-aware parameter display, automatic rebuilds, `latency` override with KVO, `tailTime`, IR restore on state load |
| `InstrumentState.swift` | Optional `effects` section (malformed → none) |
| `DiagnosticsConsoleView.swift` | Effects page |
| `project.yml` | App-group entitlement for the IR library; read access for IR files; version 0.6.0 / AU 1536 |
| `render_demo.cpp` | `--phase6` |

## Functionality status
| Item | Status |
|---|---|
| Framework: 8 slots, insert / send A / send B, series and parallel, pre/post sends, mix, output, bypass, swaps, latency compensation and reporting | **Implemented, tested** |
| All 27 effects | **Implemented, tested** (measurements below) |
| Built-in IRs (8, synthesised at the session rate) and user IRs (resampled to the session rate, ≤ 3 s, mono or stereo) | **Implemented, tested** in C++ |
| IR library, state references, host latency KVO, effects page | **Partially implemented:** written, awaiting compile. The app group must also be registered for the team (see Next). |
| vDSP FFT and non-uniform partitions for long IRs | **Not started:** Phase 10 optimisation |
| True-peak (inter-sample) limiting; sidechain input from another track | **Not started:** limiter is sample-peak; no sidechain bus yet |

## Measured results (Linux host, 48 kHz)
- **Delays:** 250 / 125 ms echoes land on exactly sample 12000 / 6000. 1/8 dotted at 120 BPM lands on sample 18000. 50 % feedback gives a second echo at 0.51. Ping-pong alternates L then R. Tape wow and flutter spread the pitch by 9.5 Hz, against 0.7 Hz off. The BBD gets darker as the delay lengthens.
- **Reverbs:**
  - Hall RT60 measured 0.95 s for 1.0 s set and 2.31 s for 2.5 s set. Room measured 0.56 s for 0.6 s set; its L/R correlation is −0.03.
  - All four algorithmic reverbs are stable at maximum settings over 12 s.
  - The spring's highs arrive before its lows, as a spring's dispersion chirp should.
- **Convolution:** with a 5000-tap user IR, the output equals direct convolution to 2.5e-6, delayed by exactly the reported 256 samples.
- **EQs:**
  - +12 dB peak: measured 12.00 dB.
  - Low cut: −3.01 dB at the corner frequency.
  - High shelf −9 dB: measured −8.99 dB.
  - Graphic band +6 dB: measured 6.00 dB.
- **Spectral:** the neutral setting is transparent to 1.2e-7. A +12 st shift turns 440 Hz into 880.0 Hz. Freeze sustains 2 s after the input stops.
- **Granular:** +12 st turns 300 Hz into grains at 600 Hz ± 2 %. Freeze keeps playing.
- **Tape:** THD rises from 0.038 to 0.288 with 18 dB of drive.
- **Lo-fi:** 3 bits gives 9 levels. A 4 kHz rate produces 317 holds in 3800 samples, as expected.
- **Amp:** gain 90 % gives a THD of 0.13, against 0.03 at 5 %.
- **Cabinet:** −32 dB at 10 kHz.
- **Stutter:** repeats are sample-exact to 3e-8, locked to the grid.
- **Modulation:** the phaser and flanger produce 20 / 22 dB notch sweeps. The chorus detunes by ±2.6 Hz. Ensemble L/R correlation is −0.42. Tremolo reaches full depth, auto-pan is in opposition, and the 1/16 sync is locked to the song position.
- **Dynamics:**
  - Compressor at −24 dB, 4:1: −6 dBFS in gives −19.05 dBFS out, against −19.5 in theory.
  - Limiter driven +18 dB never exceeds its ceiling, and is unity and latency-exact below it.
  - Gate closes a −60 dBFS tone and passes −10 dBFS at unity.
  - Width 0 % is mono; bass mono removes the side signal below its cutoff.
- **Routing:** two +6 dB EQs give 12.00 dB in series and 6.00 dB in parallel. An insert limiter plus a send convolution gives a total of 400 samples, and the dry and return signals arrive on the same sample. Re-tapping the send pre-insert realigns to 256.
- **Bypass and swaps:** the largest sample step while toggling bypass and swapping delay for chorus is 0.017. The sine alone steps 0.0105, so there is no click.
- **Real-time safety:** zero allocations with eight heavy slots across all three buses, with parameter movement.
- **Fuzz:** 27 types × 6 random settings stayed finite, with a peak of 7.1. NaN input is recovered from. All types build and run at 44.1 and 96 kHz, and latencies scale with the rate.
- **Kernel:** a note through hall and limiter allocates nothing, reports 144 samples of latency, and the tail rings on after release. Re-allocating at 96 kHz rebuilds the slots, and the latency becomes 288.

### Load (host build, scalar FFT, % of one core at 48 kHz)
| Effect | Load |
|---|---|
| Convolution (3 s IR) | 5.9 |
| Amp | 1.7 |
| Room | 1.3 |
| Hall | 1.3 |
| Spectral | 1.3 |
| Spring | 1.2 |
| Tape | 1.0 |
| Everything else | ≤ 1.0 |

Eight heavy slots together measured 11.2 %.

### Test suites
| Suite | clang -Werror | g++ ASan+UBSan | g++ TSan |
|---|---|---|---|
| test_kernel | 44/44 | 44/44 | 44/44 |
| test_modules | 74/74 | 74/74 | — |
| test_graph | 31/31 (goldens bit-close) | 19/19 | 19/19 |
| test_modulation | 97/97 | 97/97 | 97/97 |
| test_sequencing | 91/91 | 91/91 | 91/91 |
| test_effects | 74/74 | 74/74 | 74/74 (UI-thread rebuilds during render) |

## Known issues and limitations
- Slot types and the convolution IR choice are not automatable. Changing them rebuilds the slot, with a 3 ms crossfade.
- Convolution IRs are truncated to 3 s. The uniform 256-sample partitions cost about 6 % of a core for a 3 s IR on this host; Phase 10 adds vDSP and non-uniform partitions.
- Parallel chains average their branches, so equal branches keep unity gain.
- Effects edits (type, routing) briefly fade the whole effects output, about 5 ms, when they change the total latency.
- The limiter is sample-peak, not true-peak.
- In the AUv3 extension, the file importer used for "Load IR…" depends on the host. This goes into the Phase 7 DAW matrix.

## Next
- **Before building:** register the App Group `group.com.dmtcymatics.vekturmodular` for the team in the developer portal. Both targets now request it.
- Phase 7: MIDI and AUv3. That covers the MIDI engine, MIDI Learn, host transport and Ableton Link, and the DAW test matrix, including effects latency and tail behaviour across hosts.
