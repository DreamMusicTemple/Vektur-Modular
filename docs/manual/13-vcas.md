# 13. VCAs

The main VCA shapes each voice's level, with a linear or exponential response. Saturation adds gentle warmth when the VCA is driven hard. Initial Gain lets sound through without an envelope, which is useful for drones and for external processing.

**VCA 2** is a free utility VCA. Use it to make a modulation depth itself modulated, or to gate a second sound source.

| Control | Range | Default | Identifier |
|---|---|---|---|
| VCA Response | Linear / Exponential | Exponential | `vca.response` |
| VCA Saturation | 0 % … 100 % | 0 % | `vca.saturation` |
| VCA Velocity | 0 % … 100 % | 50 % | `vca.velocity` |
| VCA Initial Gain | 0 % … 100 % | 0 % | `vca.initialGain` |
| VCA 2 Response | Linear / Exponential | Linear | `vca2.response` |
| VCA 2 Saturation | 0 % … 100 % | 0 % | `vca2.saturation` |
| VCA 2 Initial Gain | 0 % … 100 % | 0 % | `vca2.initialGain` |
| VCA 2 CV Amount | 0 % … 100 % | 100 % | `vca2.cvAmount` |
