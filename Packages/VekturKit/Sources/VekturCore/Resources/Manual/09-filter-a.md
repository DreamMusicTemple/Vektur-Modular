# 9. Filter A: the multimode filter

Filter A is a 12 dB/octave state-variable filter with low-pass, band-pass, high-pass and notch modes. It stays stable when VCO 3 sweeps its cutoff at audio rate. Resonance starts to self-oscillate at about 95 %. Drive adds input saturation with level make-up.

The EG 1, VCO 3 and S&H amounts are in octaves: +1 opens the filter one octave per unit of modulation. Key Track at 100 % makes the cutoff follow the keyboard exactly, so a self-oscillating filter can be played in tune.

**Filter Oversampling** (1×, 2× or 4×, in Settings) runs the filters at a higher internal rate. This reduces aliasing from drive and resonance.

| Control | Range | Default | Identifier |
|---|---|---|---|
| Filter A Mode | Low-pass / Band-pass / High-pass / Notch | Low-pass | `vcfA.mode` |
| Filter A Cutoff | 20 Hz … 20000 Hz | 2000 Hz | `vcfA.cutoff` |
| Filter A Resonance | 0 % … 100 % | 0 % | `vcfA.resonance` |
| Filter A Drive | 0 dB … 24 dB | 0 dB | `vcfA.drive` |
| Filter A EG1 Amount | -8 oct … 8 oct | 0 oct | `vcfA.envAmount` |
| Filter A Key Track | 0 % … 100 % | 50 % | `vcfA.keyTrack` |
| Filter A S&H Amount | -4 oct … 4 oct | 0 oct | `vcfA.shAmount` |
| Filter A VCO 3 Amount | -4 oct … 4 oct | 0 oct | `vcfA.vco3Amount` |
