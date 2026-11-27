# 10. Filter B: the ladder, and filter routing

Filter B is a four-pole (24 dB/octave) transistor-ladder low-pass filter with saturation at its input. It self-oscillates from about 87 % resonance, in tune. As on the hardware it is modelled on, raising the resonance thins the bass. *Authentic* mode keeps most of that loss; *Modern* restores more of the low end.

**Filter Routing** chooses how the voice uses the two filters: A only, B only, A into B in series, or A and B in parallel. A high-pass A in series with B gives a resonant band-pass. Parallel routing gives two-peak, vocal-like sounds.

| Control | Range | Default | Identifier |
|---|---|---|---|
| Filter B Cutoff | 20 Hz … 20000 Hz | 1200 Hz | `vcfB.cutoff` |
| Filter B Resonance | 0 % … 100 % | 20 % | `vcfB.resonance` |
| Filter B Drive | 0 dB … 24 dB | 0 dB | `vcfB.drive` |
| Filter B EG1 Amount | -8 oct … 8 oct | 3 oct | `vcfB.envAmount` |
| Filter B Key Track | 0 % … 100 % | 50 % | `vcfB.keyTrack` |
| Filter B S&H Amount | -4 oct … 4 oct | 0 oct | `vcfB.shAmount` |
| Filter B VCO 3 Amount | -4 oct … 4 oct | 0 oct | `vcfB.vco3Amount` |
