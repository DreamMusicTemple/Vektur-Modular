# DSP Module Reference (Phase 2)

Every module lives in `Packages/VekturKit/Sources/VekturDSP/modules/`, is
header-only C++20, allocation-free, and declares its ports in `kPorts` for the
Phase 3 graph compiler. CV convention: 1.0 = 5 V; pitch 0.2/octave (1 V/oct);
gates 0/1. Tests: `DSPTests/test_modules.cpp` (measured values in
`docs/PHASE2_REPORT.md`).

The models are **circuit-informed**, not component-level recreations. Each
section says what behaviour is modelled and what is not.

---

## VCO (`Vco.hpp`)
**Waveforms:** sine, triangle, sawtooth, reverse sawtooth, square, variable pulse (2–98 %).
**Sub-oscillator:** square, −1 or −2 octaves (flip-flop divider, VCO 1).
**Sync:** hard (reset on master wrap); soft = *window sync*, which snaps only
when the slave is within 25 % of its own cycle boundary. Near-harmonic ratios
lock and other ratios pass through, the same musical effect as the
reversing-core soft sync on hardware.
**FM:** exponential (±4 oct at 100 %) or linear (through-zero excluded),
audio-rate, VCO 1 → VCO 2.
**Anti-aliasing:** every step and corner, including sync resets and the sub
edges, is placed at its exact sub-sample time and corrected with 2-point
polyBLEP/polyBLAMP. Cost: 1 sample of latency.
Measured alias below 15 kHz at 4.1 kHz fundamental: saw −41.5 dB, square
−41.3 dB, pulse −41.9 dB, triangle −60.3 dB. That is 25–29 dB better than a
naive oscillator, but not as clean as table-based minBLEP; see the Phase 10
"HQ oscillator" item.
**Analogue behaviour** (applied by the voice):
- Ornstein–Uhlenbeck drift per oscillator (τ 2 s) plus a common thermal drift (τ 20 s).
- Fixed per-voice tuning tolerance (deterministic per voice index).
- Exponential-converter high-frequency droop (Authentic only, up to ≈ 12 ct at 10 kHz).

**Low-frequency mode:** ÷256, so VCO 3 can act as a keyboard-tracking LFO.
**Phase:** free-running by default; optional reset to a start phase on note-on.

## Filter A — multimode SVF (`Filters.hpp: SvfFilter`)
Trapezoidal (ZDF) state-variable filter. LP/BP/HP/notch outputs are all
available simultaneously; the voice uses the selected one. 12 dB/oct.
- Resonance 0–100 %. Self-oscillation starts at ≈ 95 % and is bounded by a
  soft clipper on the band-pass integrator.
- Drive 0–24 dB of tanh input saturation, with level make-up.
- Stable under audio-rate cutoff modulation; coefficients update per sample
  when VCO 3 modulates the cutoff at audio rate.

## Filter B — 4-pole ladder (`Filters.hpp: LadderFilter`)
Four TPT one-pole stages with implicitly solved (ZDF) global feedback, and tanh
saturation at the ladder input.
- 24 dB/oct; measured slope 25.9 dB/oct between 4× and 8× fc.
- Self-oscillation at resonance ≈ 87 %+; bounded, pitch-accurate to < 0.02 %.
- Resonance causes passband (bass) loss, as on transistor ladders. Authentic
  restores 25 % of it, Modern 60 %.

## Oversampler (`Filters.hpp: Oversampler`)
1×/2×/4×, polyphase 31-tap halfband (Blackman-windowed sinc, exact halfband
normalisation). Passband within ±0.08 dB to 10 kHz. It wraps the nonlinear
filter section and, with **Oscillator Quality = HQ** (Phase 10), every VCO:
each VCO then takes two half-rate steps per sample, with a sync event placed
in the half-sample it falls in and wraps reported at the base rate. HQ cuts
alias energy by 7–10 dB on high notes at an unchanged level.
Phase 10 fix: the callback is stateful, so its sub-sample calls are now
sequenced explicitly. Before, they ran inside a function's argument list,
whose evaluation order is unspecified.

## Recording tap (`Recorder.hpp`, Phase 9)
Wait-free SPSC ring of 2^19 interleaved stereo frames, allocated when
recording first starts. It is written at the end of `Kernel::render` (after
the safety stage). When full it drops whole frames and counts them; it never
blocks the audio thread.

## FFT (`fx/FFT.hpp`)
Real FFT for the convolution and spectral effects. Phase 10: vDSP
(`vDSP_fft_zrip`) on Apple, radix-2 elsewhere, with the same scaling.
`vk_fft_selftest()` checks either path against a direct DFT.

## Envelope (`Envelope.hpp`)
ADSR, 0.5 ms–20 s per stage; sustain 0–100 %. Curves: linear, exponential,
logarithmic, and **Analogue**. Analogue charges toward a 1.3× overshoot target
(arriving at 1.0 with energy) and discharges exponentially. Every stage starts
from the current level, so retriggering and early release are click-free.
Options: loop (A→D cycling while the gate is held), retrigger from zero,
velocity amount, ±5 % per-voice time tolerance (Authentic).
Timing accuracy: ±1 sample.

## VCA (`Utilities.hpp: Vca`)
Linear or exponential law (exponential spans 60 dB, normalised 0→0, 1→1).
Optional tanh saturation. Authentic adds −80 dB control bleed while a voice is
active. CV = EG2 × velocity scaling + initial gain.

## Noise (`Utilities.hpp: Noise`)
Deterministic white noise (xorshift), pink noise (Kellet filter), and a colour
tilt from −100 % (dark) to +100 % (bright). Measured octave-band energy at
2–4 kHz vs 250–500 Hz: white +9.0 dB, pink 0.0 dB.

## Ring modulator (`Utilities.hpp: RingMod`)
Four-quadrant multiplier. Carrier leak 0–20 % models imperfect balance.
Balanced suppression measures −199 dB (numerically exact).

## Sample & hold (`Utilities.hpp: SampleHold`)
Internal clock 0.05–50 Hz and/or an external trigger, with output slew
0–1000 ms. In the normalled voice it is global: one S&H samples white or pink
noise and feeds the cutoff of both filters. In Phase 3 it becomes patchable
from any source.

## Slew limiter, envelope follower, voltage processor, mixer (`Utilities.hpp`)
- **Slew limiter:** rise and fall times, exponential or linear. Used for glide.
- **Envelope follower:** peak or RMS detection, with attack, release, threshold and gain.
- **Voltage processor:** two summed inputs with attenuverters, offset, invert and ±limit.
- **Mixer:** N channels with gain, phase invert and equal-power pan to stereo. DC-coupled.

These four are implemented and unit-tested. Only the slew limiter (as glide)
is in the normalled voice; the others reach the user when patching arrives in
Phase 3.

---

# Phase 3A modules (graph-only; reach the sound through patch cables)

Every module is in `graph/GraphModules.cpp`. Ports and normals are listed by
`vk_rack_*` / `RackCatalog`. Attenuated inputs show their knob in the port info.

- **VCO 4–9 (bank):** the same oscillator as VCO 1–3. Each has FM amount/mode and sync mode; pitch is normalled to the keyboard. Output level is a per-VCO parameter.
- **OTA multimode:** an SVF topology with both integrators tanh-saturating. It gives softer, compressing resonance than Filter A. LP/BP/HP outputs plus a mode-selected out; two cutoff CV inputs in octaves per unit CV.
- **Comb filter:** fractional delay (20 Hz–5 kHz), feedback ±99 % (negative gives odd harmonics), one-pole damping, wet/dry mix, 1 V/oct CV. The feedback loop saturates softly above normal levels. Measured resonance within 0.1 % of the set frequency.
- **Formant filter:** three normalised band-passes morphing through A-E-I-O-U (bass-voice formant table). Resonance scales the bandwidths, shift transposes the formants ±12 st, and CV moves the vowel.
- **VC band-pass:** unity-peak SVF band-pass, Q 0.5–40, two CV inputs.
- **Fixed filter bank:** eight unity-peak bands at 125, 250, 500 Hz and 1, 2, 4, 8, 12 kHz, each with a level, plus an overall level.
- **Analogue switch:** four inputs. Modes: manual, CV select (0–1 across 2–4 steps), and sequential (advances on each clock edge). 1 ms crossfade between positions.
- **Multiples ×2:** one input to four outputs.
- **Gate delay:** delays gates by 0–2 s. Length 0 keeps the input gate's length; otherwise the output is a pulse of the set length (pulse stretcher).
- **4-channel mixer:** attenuverters ±100 %, overall level, normal and inverted outputs.
- **VCA 2:** linear/exponential law, saturation, initial gain, CV amount.
- **Sample & hold (patchable):** with nothing patched it follows the global shared random stream. With a signal patched, each voice samples its own signal on the global clock or on its trigger input.
- **Voice output:** equal-power pan from the stereo-spread pattern plus a pan CV, and the +6 dB that restores the mixer's headroom.

## LFO (`ModMatrix.hpp: Lfo`, rack modules `lfo1…lfo3`)
Phase accumulator, 0.01–50 Hz (log taper). Shapes: sine, triangle (both start at 0
rising), saw up, saw down, square, random step (new value per cycle) and random smooth
(cosine interpolation between per-cycle random values). Naive shapes: at these rates
aliasing is inaudible; patching an LFO into an audio input at 50 Hz is possible.
- **Free:** one oscillator in the kernel with a shared phase for every voice (classic
  single-LFO behaviour). Rate modulation from the matrix does not apply in free mode.
- **Key:** one oscillator per voice, restarted at the phase setting on every note; rate
  and level accept per-voice matrix offsets.
- **Delay:** a linear fade-in from note start (both modes).
- **Level:** output gain 0–100 %, modulatable.
Outputs `out` and `inv` on the rack module carry the same signal the matrix sees.

## Modulation matrix (`ModMatrix.hpp`, `VoiceGraph::runModulation`)
Per slot, at control rate (32 frames):
`y = offset + amount × invert(curve(polarity(source))) × scaleBy`, then a one-pole slew
(time constant = slew ms). Polarity converts unipolar ↔ bipolar (0…1 ↔ −1…1); curves are
linear, exponential (x·|x|), logarithmic (sign·√|x|) and S-curve (smoothstep). Invert is
1 − x for unipolar results and −x for bipolar. Offsets from several slots on one
destination add up.
Destinations are in normalised range units: ±100 % sweeps the whole range, so on a
log-tapered cutoff 10 % ≈ 1 octave. Virtual destinations: pitch (100 % = ±1 octave),
voice level (multiplier 1 + y, clamped 0…2), pan, and all filter cutoffs (Filter A, B, OTA,
diode, transistor, VCBPF centre, comb frequency).
Limitations (documented, by design): EG sources use the end of the previous block (≤
0.67 ms lag); discrete parameters (waveforms, modes, switches) and global parameters
are not destinations.

## Performance input (Phase 4B, `Kernel.cpp`)
- **Tempo sync:** an LFO with a sync division runs at tempo ÷ 60 ÷ beats-per-cycle.
  Free-running synced LFOs are phase-locked to the host beat position every block
  while the transport moves, and follow relocations and loops. Key-retriggered synced
  LFOs restart on each note. Synced rate ignores matrix rate offsets. Without a host
  musical context the Internal Tempo parameter is used.
- **MPE:** lower zone (master ch 1, members 2…) or upper zone (master ch 16, members …15).
  Member channels give each note its own bend (± range), pressure (feeds the Poly
  Aftertouch source) and CC 74 timbre (MPE Timbre source, rests at 0.5). Bend and
  pressure sent before the note-on are adopted. The master channel stays zone-wide.
  With MPE off, input is omni and channel-blind as before.
- **Key zones (Poly mode):** split sends keys below the split point to zone A and the
  rest to zone B; layer plays both. Each zone has a transpose, and the Key Zone source
  (0 = A, 1 = B) lets the matrix voice each zone differently within one patch. Note-offs
  match the played key, so transposed and layered notes release correctly.
- **Chord analyser (`ChordAnalyzer.hpp`):** pitch-class templates (triads, sus, 6ths,
  7ths, 9ths, power chord). It prefers an exact match on the bass note, then an inversion
  written as a slash chord, then a seventh chord with the fifth omitted. Clusters return no name.

# Phase 5 — note generators (`seq/`)

## Shared clock and timing (`Kernel::runGenerators`, `seq/SeqCommon.hpp`)
- Clock: the host's beat while its transport moves; otherwise a free clock at the current tempo. A jump of more than 0.01 beats (locate, loop) or a transport start re-aligns every generator, flushing its sounding notes first.
- Sample accuracy: the render loop splits spans at the next generator event (as it does for MIDI events), so notes start on their exact sample. Measured error: 0.000 samples at a block size of 256.
- Swing: odd grid ticks move late by (swing − 50 %) × 2 of a step (50 % straight, 66.7 % triplet feel, 75 % dotted).
- Randomness: counter-based hash(seed, tick). It is reproducible and survives relocation.
- `NoteQueue` (256 events per generator): every note-on has exactly one note-off. A note-off only closes the pitch's current note, so an overlapping gate never cuts a newer note short; ratchets still re-articulate.

## Arpeggiator (`Arpeggiator.hpp`, module 36)
Patterns:
- Up, Down
- Up-Down and Down-Up, each with or without repeated ends
- As Played, Random, Random Walk
- Converge, Diverge
- Thumb, Pinky
- Chord

Octaves 1–4, ascending, descending or up-down. Other controls: Euclidean rhythm E(k, n) with rotation, probability, ratchets 1–8× (with chance), gate 5–200 % and three velocity modes (as played, fixed, accent on the beat). Latch replaces the chord on a fresh hand, and the sustain pedal holds keys. Chaining plays up to four patterns, N passes each. The arp can also be transposed by the sequencer. Restart is Auto (bar grid while the host plays, first key otherwise), Beat-locked or Key restart.

## Step sequencer (`StepSequencer.hpp`, module 37)
- 16 steps × rows A/B/C (0–100 %). Row A covers 1, 2, 3 or 5 octaves above C3 + root and is quantised to one of 16 scales (ties round down).
- Gate per step:
  - Off: rest.
  - On.
  - Tie: holds the previous note. Ties look ahead, so the previous note's gate already reaches the tie.
  - Skip: the step is removed from the cycle and takes no time.
- Per step: probability and 1–4× ratchets.
- Directions: forward, reverse, ping-pong (2n − 2), random, random walk.
- Run modes:
  - Run: starts from step 1 at the next grid line.
  - Transport: song-locked, plays only while the host plays.
  - Key: runs while a key is held and transposes from C4.
- Rows A/B/C are global matrix sources 23–25. With "Plays Notes" off, the sequencer drives the matrix only.

## Generative engine (`Generative.hpp/.cpp`, module 38)
- `generatePhrase(settings, seed, repeat)` is pure and bounded (512 notes, 8 bars × up to 48 ticks).
- Harmony: bar 1 is the tonic; middle bars follow functional-harmony Markov weights (colour chords ii/iii/vii° scale with complexity); the last bar is the dominant, or the subdominant with probability complexity × 0.3. Sevenths appear with probability complexity × 0.7. Chromatic harmonises as major.
- Rhythm: onset probability is density × metric strength (downbeat 1.0, half 0.85, beat 0.7, 8th 0.45, 16th 0.3). The phrase always starts on the one.
- Styles:
  - Melody: chord tones on strong beats; stepwise scale motion with leaps (complexity × 0.35) on weak beats; stays inside the register ± range; ends on a chord tone.
  - Bassline: root on every downbeat; fifth, octave, third or neighbour notes elsewhere; approach notes into the next bar's root.
  - Chords: voice-led close voicing with an optional root below.
  - Ambient: sparse, long, overlapping chord tones with add9.
  - Broken chords: up, up-down or seeded order across two octaves.
- Evolve: on each repeat, each bar re-rolls with probability Evolve and the progression at Evolve / 3. This is computed functionally (look back up to 64 repeats), so the result can be relocated.
- Player: phrase n occupies beats [n·L, (n + 1)·L) of the clock, so it is bar-locked; Hold to Play restarts at the key. Settings are read at each phrase start. Follow Keys transposes to the lowest held key's pitch class (−6 … +5 semitones).

## Preset creator (`PresetGenerator.cpp`)
- Eight recipes (bass, lead, pad, pluck, keys, brass, FX, drone) over the normalled voice: VCOs, sub, noise, ring mod, filters and routing, EGs, VCA, LFOs, voicing, glide and up to four matrix slots.
- Each candidate is auditioned in a private kernel (held C3 + C4, 0.9 s). It must be finite, its loudest 50 ms must be above −40 dBFS, and it is trimmed to a peak of −3 dBFS or less.
- A failed candidate is retried with a derived seed, up to 8 times. After that a safe voice is used, so the creator never returns silence.

# Phase 6 — effects (`fx/`)

## Engine (`EffectsEngine.*`)
- **Slots:** eight slots; each takes a type, on, mix, output, bus (Insert / Send A / Send B) and p1…p12. The p values are stored 0…1 and mapped through the type's spec: linear, log, choice or toggle. Choices and toggles read the parameter target; continuous parameters are smoothed (20 ms).
- **Signal flow:**
  - Insert chain: in → slots in order (series), or side by side, aligned and averaged (parallel).
  - Sends: tapped pre- or post-inserts, each through its own chain.
  - Output: insert-chain level × main + return A + return B, then × output.
- **Slot mixing:** `out = (wet·mix + dry_d·(1 − mix))·gain`, where `dry_d` is the dry signal delayed by the slot's latency. Bypass crossfades to `dry_d` over 10 ms.
- **Latency:**
  - Insert path: series sums the slot latencies; parallel takes the maximum.
  - Send path: its own chain, plus the insert latency when tapped post-insert.
  - Total: the maximum over all paths. Every path is delayed up to the total (at most 16384 samples).
  - Constant until types or routing change, which triggers a 5 ms output fade.
- **Structural changes:** type, convolution IR choice and user IR are built by `update()` on the [ui] thread. Each goes to its slot through an atomic pointer; the slot fades 3 ms, swaps and fades in. Replaced instances are freed through a lock-free graveyard on the next `update()`.
- **Silence:** a slot skips processing once its input has been silent for longer than its tail plus latency. Freeze modes report an infinite tail and never skip.
- **NaN/Inf:** the slot resets itself and the telemetry flag is raised; the voices are not reset.
- **Empty engine:** bit-exact pass-through with zero latency.

## Effects
| Effect | Model | Latency |
|---|---|---|
| Stereo / Ping-Pong Delay | Cubic-interpolated lines (2.6 s). Time glides with a 60 ms smoothing, tape-like. Loop has HP/LP and soft-clip drive; output has ducking (5/250 ms follower); optional modulation | 0 |
| Tape Echo | One loop with heads at 1×/2×/3× the repeat time (7 combinations). Wow (slow random + 0.6 Hz) and flutter (11 Hz) on speed. Record saturation; tone loses HF with age; hiss | 0 |
| BBD | 4096-stage clock model: bandwidth = 0.3 · 4096 / (2·delay) × tone. Anti-alias/reconstruction low-passes; signal-following (companded) noise; saturation | 0 |
| Plate | Dattorro figure-eight tank, scaled from 29.761 kHz; size scales the tank; modulated input all-passes | 0 |
| Hall / Room | 8-line FDN with a Householder matrix. Each line has three bands (< 250 Hz, mid, > 3.5 kHz / 2.5 kHz) with gains 10^(−3·len/RT) for low, mid and high RT. Four input diffusers; 12 early-reflection taps; modulated lines | 0 |
| Spring | 1–3 springs, each a 33–41 ms transit loop through 40 first-order all-passes (a = −0.55 … −0.75 with tension) → dispersion chirp; "drip" transient emphasis | 0 |
| Convolution | Uniformly partitioned overlap-save (B = 256, FFT 512, frequency-domain delay line), stereo IR ≤ 3 s. Built-in IRs: band-split exponentially decaying noise with early reflections, or a filtered-impulse cabinet | 256 |
| Parametric / Graphic EQ | RBJ biquads (TDF-II); bands at exactly 0 dB are skipped | 0 |
| Spectral | STFT 2048 / hop 512 with √Hann windows; phase vocoder (true-frequency estimate, phase propagation) for shift and freeze; neutral = identity | 2048 |
| Granular | 2.5 s capture; ≤ 64 grains with Hann, triangle or trapezoid envelopes; overlap-normalised; freeze stops the write head | 0 |
| Tape | ADAA tanh with bias, head bump (55/85/130 Hz), two-pole HF loss per speed, wow and flutter on a 5 ms centred delay | 5 ms |
| Stutter | Beat repeat on the clock grid, seeded per grid index; 2 ms slice edges; tape stop | 0 |
| Lo-Fi | Phase-accumulator sample-and-hold with jitter, fractional bit depth, optional 4-pole anti-alias | 0 |
| Amp | 1–3 ADAA tanh stages with asymmetry and DC blocking, inter-stage HPF, sag; tone stack: bass 120 Hz, mid 700 Hz, treble 3 kHz, presence 5 kHz, plus 7.5 kHz LP | 0 |
| Cabinet | Box resonance, mid scoop, presence, double LP (mic and position dependent), distance comb | 0 |
| Chorus / Flanger / Ensemble | Modulated cubic delays; ensemble uses 3 taps at 120° with slow + fast LFOs | 0 |
| Phaser | 2–12 first-order all-passes, centre ±2 octaves, feedback | 0 |
| Tremolo / Pan | Sine, triangle, square (smoothed) or saw; auto-pan in opposition; harmonic mode splits at 800 Hz | 0 |
| Compressor | Soft-knee static curve; peak or RMS detector; attack/release in the dB domain; link; auto makeup = −½·T·(1 − 1/R) | 0 |
| Limiter | Sliding-minimum gain over 3 ms, box-smoothed so the ramp completes as the peak leaves the delay; release one-pole; hard ceiling guard | 3 ms |
| Gate | Peak detector with hysteresis, hold, range and an expander ratio below threshold | 0 |
| Stereo Width | M/S width and gains; side high-passed (4th-order Butterworth) for bass mono; balance pan; Haas | 0 |

Built-in IRs (`builtInImpulse`): small room, studio, chamber, concert hall, cathedral, plate, spring tank and 4×12 cabinet. Each is energy-normalised to 0.5 per channel and deterministic per sample rate.

# Phase 7 — MIDI engine (`KernelMidi.cpp`, `midi/MidiLearn.hpp`)

Not a rack module; part of the kernel's event handling. See `docs/MIDI.md` for the implementation chart.

- **Ordering per message:** system (clock) → receive-channel filter → MIDI Learn (report, then apply mappings) → MPE member-channel handling → channel-wide handling.
- **Controllers:**
  - **Per-channel state:** bank MSB/LSB, the RPN/NRPN number, data MSB/LSB and the 32 controller MSBs, kept for 14-bit pairs.
  - **RPN:** 0 sets bend range (semitones + cents). On an MPE member channel it sets the per-note range instead. 1 sets fine tune (8192 = 0, ±100 ct). 6 is the MCM. 127/127 is null.
  - **NRPN:** goes to MIDI Learn only, with 14-bit data; increment and decrement step relative mappings.
- **MIDI 2.0 (UMP type 4):**
  - 16-bit velocity is carried to the voice unquantised: a side value (`hiResVel_`) set for the one note being handled, so `soundOn` uses it for that key only.
  - 32-bit CC, pressure and bend.
  - Registered and assignable controllers map to RPN and NRPN.
  - Per-note pitch bend × MPE per-note range; per-note controller 74 → timbre; per-note management reset.
  - Program change with the bank-valid flag.
  - In MPE mode, messages are down-converted so zones keep their MIDI 1.0 semantics; hi-res velocity is kept.
  - Types 1 and 2 (system, MIDI 1.0 in UMP) are converted to bytes.
- **Clock in:**
  - Interval smoothing: α = 0.5 for the first 24 ticks, then 0.08.
  - Tempo = 60·SR / (24·interval), clamped to 20–400 BPM.
  - Start sets the next tick to beat 0; Continue resumes from SPP; Stop halts the transport.
  - The beat is interpolated between ticks, clamped so it never runs past the next tick.
  - Clock is "active" if a tick arrived within 0.5 s.
  - Sync tolerance for re-locating the generators is 0.1 beat for clock, against 0.01 for the host.
- **Clock out:** ticks are placed at `ceil(beat·24)` on the generators' clock, at the first sample at or after each tick time. After a relocation they re-align. FA and FC are sent when the clock is switched on and off.
- **MIDI Learn (`midi::apply`):**
  - Values are normalised along the parameter's taper (`ParameterStore::normalise/denormalise`).
  - **Absolute:** min + (max − min)·x. Pickup engages within 0.02, or when the control crosses the parameter's value.
  - **Toggle:** flips on each rising edge to the range end farther from the current value.
  - **Relative:** step × sensitivity, with the sign taken from the range direction, clamped to the range.
  - Pickup re-arms when the parameter moves by more than 1e-4 for any other reason.
- **Time signature:** `vk_kernel_set_transport_ex` gives beats per bar (num·4/den, 0.5–32) and the bar's downbeat.
  - The generative engine anchors phrases to the host's bar lines and sizes them in host bars. Its metric accents use the half bar only in even meters.
  - Stutter's bar grids follow the meter.
  - A plain `set_transport` or no host resets to 4/4.

