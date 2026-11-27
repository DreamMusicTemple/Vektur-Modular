# 8. Noise, ring modulator and mixer

**Noise** produces white or pink noise. Colour tilts the spectrum darker or brighter. **Ring Mod** multiplies VCO 1 by VCO 2 by default. Carrier Leak lets some of the dry signal through, as worn ring modulators do.

The voice mixer sets the level of each source going into the filters. Overdriving it saturates gently, so it is safe to push for a fatter sound. **Mixer 4** is a separate four-channel utility mixer with polarity (−100…100 %) per channel. Use it for CV as well as audio.

| Control | Range | Default | Identifier |
|---|---|---|---|
| Noise Type | White / Pink | White | `noise.type` |
| Noise Colour | -100 % … 100 % | 0 % | `noise.colour` |
| Noise Level | 0 % … 100 % | 0 % | `noise.level` |
| Ring Mod Level | 0 % … 100 % | 0 % | `ringMod.level` |
| Ring Mod Carrier Leak | 0 % … 20 % | 0 % | `ringMod.carrierLeak` |
| Mixer Ch 1 | -100 % … 100 % | 100 % | `mixer4.gain1` |
| Mixer Ch 2 | -100 % … 100 % | 100 % | `mixer4.gain2` |
| Mixer Ch 3 | -100 % … 100 % | 100 % | `mixer4.gain3` |
| Mixer Ch 4 | -100 % … 100 % | 100 % | `mixer4.gain4` |
| Mixer Level | 0 % … 200 % | 100 % | `mixer4.level` |
