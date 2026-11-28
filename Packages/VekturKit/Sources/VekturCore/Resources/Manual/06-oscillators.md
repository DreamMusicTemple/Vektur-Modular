# 6. Oscillators VCO 1–3

VCO 1–3 are the main oscillators. Each has six waveforms, a ±3-octave switch, coarse and fine tuning, pulse width, a level on the mixer, linear or exponential FM, and hard or soft sync. VCO 1 also has a sub-oscillator one or two octaves down.

**Low Frequency** turns an oscillator into a slow modulation source (1/256 of its pitch). **Phase Reset** restarts the waveform at Start Phase on every note, for consistent attacks. Use VCO 3 in low-frequency mode as a third LFO: its output is normalled to the filter's VCO 3 amount.

**Oscillator Quality** (Settings → Performance) switches all oscillators to *HQ*. HQ runs them at twice the sample rate with a half-band decimator, which reduces aliasing on high notes by 7–10 dB. It costs roughly a quarter more engine CPU (measured: 12 % → 15.5 % of one core at 16 voices).

| Control | Range | Default | Identifier |
|---|---|---|---|
| VCO 1 Waveform | Sine / Triangle / Saw / Reverse Saw / Square / Pulse | Saw | `vco1.wave` |
| VCO 1 Octave | -3 / -2 / -1 / 0 / +1 / +2 / +3 | 0 | `vco1.octave` |
| VCO 1 Coarse | -12 st … 12 st | 0 st | `vco1.coarse` |
| VCO 1 Fine | -100 ct … 100 ct | 0 ct | `vco1.fine` |
| VCO 1 Pulse Width | 5 % … 95 % | 50 % | `vco1.pulseWidth` |
| VCO 1 Level | 0 % … 100 % | 100 % | `vco1.level` |
| VCO 1 Low Frequency | 0 … 1 | 0 | `vco1.lowFreqMode` |
| VCO 1 Phase Reset | 0 … 1 | 0 | `vco1.phaseReset` |
| VCO 1 Start Phase | 0 ° … 360 ° | 0 ° | `vco1.startPhase` |
| VCO 1 Sub Level | 0 % … 100 % | 0 % | `vco1.subLevel` |
| VCO 1 Sub Octave | -1 oct / -2 oct | -1 oct | `vco1.subOctave` |
| VCO 1 FM Amount | 0 % … 100 % | 50 % | `vco1.fmAmount` |
| VCO 1 FM Mode | Exponential / Linear | Exponential | `vco1.fmMode` |
| VCO 1 Sync | Off / Hard / Soft | Hard | `vco1.syncMode` |
| VCO 2 Waveform | Sine / Triangle / Saw / Reverse Saw / Square / Pulse | Saw | `vco2.wave` |
| VCO 2 Octave | -3 / -2 / -1 / 0 / +1 / +2 / +3 | 0 | `vco2.octave` |
| VCO 2 Coarse | -12 st … 12 st | 0 st | `vco2.coarse` |
| VCO 2 Fine | -100 ct … 100 ct | 0 ct | `vco2.fine` |
| VCO 2 Pulse Width | 5 % … 95 % | 50 % | `vco2.pulseWidth` |
| VCO 2 Level | 0 % … 100 % | 0 % | `vco2.level` |
| VCO 2 Low Frequency | 0 … 1 | 0 | `vco2.lowFreqMode` |
| VCO 2 Phase Reset | 0 … 1 | 0 | `vco2.phaseReset` |
| VCO 2 Start Phase | 0 ° … 360 ° | 0 ° | `vco2.startPhase` |
| VCO 2 Sync | Off / Hard / Soft | Off | `vco2.syncMode` |
| VCO 2 FM from VCO 1 | 0 % … 100 % | 0 % | `vco2.fmAmount` |
| VCO 2 FM Mode | Exponential / Linear | Exponential | `vco2.fmMode` |
| VCO 3 Waveform | Sine / Triangle / Saw / Reverse Saw / Square / Pulse | Saw | `vco3.wave` |
| VCO 3 Octave | -3 / -2 / -1 / 0 / +1 / +2 / +3 | 0 | `vco3.octave` |
| VCO 3 Coarse | -12 st … 12 st | 0 st | `vco3.coarse` |
| VCO 3 Fine | -100 ct … 100 ct | 0 ct | `vco3.fine` |
| VCO 3 Pulse Width | 5 % … 95 % | 50 % | `vco3.pulseWidth` |
| VCO 3 Level | 0 % … 100 % | 0 % | `vco3.level` |
| VCO 3 Low Frequency | 0 … 1 | 0 | `vco3.lowFreqMode` |
| VCO 3 Phase Reset | 0 … 1 | 0 | `vco3.phaseReset` |
| VCO 3 Start Phase | 0 ° … 360 ° | 0 ° | `vco3.startPhase` |
| VCO 3 → VCO 1+2 Pitch | -24 st … 24 st | 0 st | `vco3.pitchModAmount` |
| VCO 3 FM Amount | 0 % … 100 % | 50 % | `vco3.fmAmount` |
| VCO 3 FM Mode | Exponential / Linear | Exponential | `vco3.fmMode` |
| VCO 3 Sync | Off / Hard / Soft | Hard | `vco3.syncMode` |
