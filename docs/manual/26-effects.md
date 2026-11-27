# 26. Effects slots

There are eight effect slots. Each can hold any of 27 effect types:

- **Delays:** stereo, ping-pong, tape echo and BBD.
- **Reverbs:** plate, hall, room, spring and convolution.
- **Equalisers:** parametric and graphic.
- **Spectral and granular** processors.
- **Saturation and character:** tape, stutter and lo-fi.
- **Amp and cabinet.**
- **Modulation:** chorus, flanger, phaser, ensemble and tremolo/pan.
- **Dynamics:** compressor, limiter and gate.
- **Stereo width.**

Every slot has an on/off switch, mix, output level and up to 12 settings, all automatable.

Changing a slot's type crossfades to the new effect with no click. Latency from look-ahead effects is reported to the host and compensated across every path.

| Control | Range | Default | Identifier |
|---|---|---|---|
| FX 1 Type | Empty … Stereo Width (28 choices) | Empty | `fx1.type` |
| FX 1 On | 0 … 1 | 1 | `fx1.enabled` |
| FX 1 Mix | 0 % … 100 % | 100 % | `fx1.mix` |
| FX 1 Output | -24 dB … 12 dB | 0 dB | `fx1.output` |
| FX 1 Bus | Insert / Send A / Send B | Insert | `fx1.bus` |
| FX 1 Param 1 | 0 … 1 | 0.5 | `fx1.p1` |
| FX 1 Param 2 | 0 … 1 | 0.5 | `fx1.p2` |
| FX 1 Param 3 | 0 … 1 | 0.5 | `fx1.p3` |
| FX 1 Param 4 | 0 … 1 | 0.5 | `fx1.p4` |
| FX 1 Param 5 | 0 … 1 | 0.5 | `fx1.p5` |
| FX 1 Param 6 | 0 … 1 | 0.5 | `fx1.p6` |
| FX 1 Param 7 | 0 … 1 | 0.5 | `fx1.p7` |
| FX 1 Param 8 | 0 … 1 | 0.5 | `fx1.p8` |
| FX 1 Param 9 | 0 … 1 | 0.5 | `fx1.p9` |
| FX 1 Param 10 | 0 … 1 | 0.5 | `fx1.p10` |
| FX 1 Param 11 | 0 … 1 | 0.5 | `fx1.p11` |
| FX 1 Param 12 | 0 … 1 | 0.5 | `fx1.p12` |
