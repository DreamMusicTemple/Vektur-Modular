# 12. Envelopes EG 1 and EG 2

Both envelopes are ADSR envelopes with times from 0.5 ms to 20 s. There are four curve types; *Analogue* imitates an RC charge. Velocity scales the envelope's depth. **Retrigger** chooses whether a new note restarts the envelope from zero or from its current level. **Loop** cycles attack and decay while the key is held, which turns the envelope into an LFO with a shaped curve.

EG 1 is normalled to the filters and has a pitch amount for drum and sweep sounds. EG 2 is normalled to the VCA.

| Control | Range | Default | Identifier |
|---|---|---|---|
| EG 1 Attack | 0.5 ms … 20000 ms | 5 ms | `eg1.attack` |
| EG 1 Decay | 0.5 ms … 20000 ms | 400 ms | `eg1.decay` |
| EG 1 Sustain | 0 % … 100 % | 30 % | `eg1.sustain` |
| EG 1 Release | 0.5 ms … 20000 ms | 300 ms | `eg1.release` |
| EG 1 Curve | Linear / Exponential / Logarithmic / Analogue | Analogue | `eg1.curve` |
| EG 1 Velocity | 0 % … 100 % | 0 % | `eg1.velocity` |
| EG 1 Retrigger | From current / From zero | From current | `eg1.retrigger` |
| EG 1 Loop | 0 … 1 | 0 | `eg1.loop` |
| EG 1 → Pitch | -24 st … 24 st | 0 st | `eg1.pitchAmount` |
| EG 2 Attack | 0.5 ms … 20000 ms | 3 ms | `eg2.attack` |
| EG 2 Decay | 0.5 ms … 20000 ms | 300 ms | `eg2.decay` |
| EG 2 Sustain | 0 % … 100 % | 100 % | `eg2.sustain` |
| EG 2 Release | 0.5 ms … 20000 ms | 250 ms | `eg2.release` |
| EG 2 Curve | Linear / Exponential / Logarithmic / Analogue | Analogue | `eg2.curve` |
| EG 2 Velocity | 0 % … 100 % | 0 % | `eg2.velocity` |
| EG 2 Retrigger | From current / From zero | From current | `eg2.retrigger` |
| EG 2 Loop | 0 … 1 | 0 | `eg2.loop` |
