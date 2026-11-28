# 15. Utilities

- **Sample & Hold** samples noise at its own rate, with optional slew. It is normalled to the filters' S&H amounts.
- **Slew** limits how fast a signal can rise or fall: portamento for CV, or smoothing for stepped modulation.
- **Switch** routes one of up to four inputs, chosen manually, by CV or by stepping through them on each gate.
- **Gate Delay** delays and re-times gates, for echoes and offset envelopes.
- **Envelope Follower** turns the level of any audio into CV.
- **Voltage Processor** mixes two inputs with gain, offset, inversion and limiting.
- **Macros 1–4** are performance knobs. You can patch them, map them to MIDI and automate them.

| Control | Range | Default | Identifier |
|---|---|---|---|
| Slew Rise | 0 ms … 10000 ms | 100 ms | `slew.rise` |
| Slew Fall | 0 ms … 10000 ms | 100 ms | `slew.fall` |
| Slew Mode | Exponential / Linear | Exponential | `slew.mode` |
| Follower Attack | 0.1 ms … 1000 ms | 5 ms | `follower.attack` |
| Follower Release | 1 ms … 5000 ms | 100 ms | `follower.release` |
| Follower Detector | Peak / RMS | Peak | `follower.detector` |
| Follower Threshold | 0 % … 100 % | 0 % | `follower.threshold` |
| Follower Gain | 0 % … 400 % | 100 % | `follower.gain` |
| VP Input 1 Gain | -200 % … 200 % | 100 % | `vproc.gain1` |
| VP Input 2 Gain | -200 % … 200 % | 100 % | `vproc.gain2` |
| VP Offset | -2 … 2 | 0 | `vproc.offset` |
| VP Invert | 0 … 1 | 0 | `vproc.invert` |
| VP Limit | 1 % … 200 % | 200 % | `vproc.limit` |
| Switch Mode | Manual / CV select / Sequential | Manual | `switch.mode` |
| Switch Position | 1 / 2 / 3 / 4 | 1 | `switch.position` |
| Switch Steps | 2 / 3 / 4 | 4 | `switch.steps` |
| Gate Delay Time | 0 ms … 2000 ms | 100 ms | `gateDelay.delay` |
| Gate Length | 0 ms … 2000 ms | 0 ms | `gateDelay.length` |
| S&H Rate | 0.05 Hz … 50 Hz | 4 Hz | `sampleHold.rate` |
| S&H Slew | 0 ms … 1000 ms | 0 ms | `sampleHold.slew` |
| S&H Source | White noise / Pink noise | White noise | `sampleHold.source` |
| Macro 1 | 0 % … 100 % | 0 % | `macro.macro1` |
| Macro 2 | 0 % … 100 % | 0 % | `macro.macro2` |
| Macro 3 | 0 % … 100 % | 0 % | `macro.macro3` |
| Macro 4 | 0 % … 100 % | 0 % | `macro.macro4` |
