# 7. Extra oscillators VCO 4–9

VCO 4–9 are six more oscillators with the same controls as VCO 1–3, except for the sub-oscillator. They are not connected by default. Patch their outputs into the mixer's inputs, an FM input, a filter's audio input or a VCA, or use them as modulation sources.

They track the keyboard through the voice's pitch, plus their own octave and tuning settings.

| Control | Range | Default | Identifier |
|---|---|---|---|
| VCO 4 Waveform | Sine / Triangle / Saw / Reverse Saw / Square / Pulse | Saw | `vco4.wave` |
| VCO 4 Octave | -3 / -2 / -1 / 0 / +1 / +2 / +3 | 0 | `vco4.octave` |
| VCO 4 Coarse | -12 st … 12 st | 0 st | `vco4.coarse` |
| VCO 4 Fine | -100 ct … 100 ct | 0 ct | `vco4.fine` |
| VCO 4 Pulse Width | 5 % … 95 % | 50 % | `vco4.pulseWidth` |
| VCO 4 Level | 0 % … 100 % | 100 % | `vco4.level` |
| VCO 4 Low Frequency | 0 … 1 | 0 | `vco4.lowFreqMode` |
| VCO 4 Phase Reset | 0 … 1 | 0 | `vco4.phaseReset` |
| VCO 4 Start Phase | 0 ° … 360 ° | 0 ° | `vco4.startPhase` |
| VCO 4 FM Amount | 0 % … 100 % | 50 % | `vco4.fmAmount` |
| VCO 4 FM Mode | Exponential / Linear | Exponential | `vco4.fmMode` |
| VCO 4 Sync | Off / Hard / Soft | Hard | `vco4.syncMode` |
