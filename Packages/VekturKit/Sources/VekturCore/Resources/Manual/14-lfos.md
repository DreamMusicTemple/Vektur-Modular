# 14. LFOs

There are three LFOs. Each has seven shapes, a rate from 0.01 Hz to 50 Hz or a tempo-synced division, key or free retrigger, a fade-in delay, a start phase and a level. Tempo-synced LFOs follow the internal tempo, the host or MIDI clock (chapter 32).

LFO 1 is normalled to vibrato and the filter. LFO 2 and LFO 3 are free sources for the modulation matrix and for cables.

| Control | Range | Default | Identifier |
|---|---|---|---|
| LFO 1 Shape | Sine / Triangle / Saw Up / Saw Down / Square / Random Step / Random Smooth | Triangle | `lfo1.shape` |
| LFO 1 Rate | 0.01 Hz … 50 Hz | 5 Hz | `lfo1.rate` |
| LFO 1 Retrigger | Free / Key | Free | `lfo1.sync` |
| LFO 1 Delay | 0 ms … 5000 ms | 0 ms | `lfo1.delay` |
| LFO 1 Phase | 0 ° … 360 ° | 0 ° | `lfo1.phase` |
| LFO 1 Level | 0 % … 100 % | 100 % | `lfo1.level` |
| LFO 1 Tempo Sync | Off … 1/16 triplet (16 choices) | Off | `lfo1.tempoSync` |
| LFO 2 Shape | Sine / Triangle / Saw Up / Saw Down / Square / Random Step / Random Smooth | Sine | `lfo2.shape` |
| LFO 2 Rate | 0.01 Hz … 50 Hz | 0.5 Hz | `lfo2.rate` |
| LFO 2 Retrigger | Free / Key | Free | `lfo2.sync` |
| LFO 2 Delay | 0 ms … 5000 ms | 0 ms | `lfo2.delay` |
| LFO 2 Phase | 0 ° … 360 ° | 0 ° | `lfo2.phase` |
| LFO 2 Level | 0 % … 100 % | 100 % | `lfo2.level` |
| LFO 2 Tempo Sync | Off … 1/16 triplet (16 choices) | Off | `lfo2.tempoSync` |
| LFO 3 Shape | Sine / Triangle / Saw Up / Saw Down / Square / Random Step / Random Smooth | Sine | `lfo3.shape` |
| LFO 3 Rate | 0.01 Hz … 50 Hz | 0.1 Hz | `lfo3.rate` |
| LFO 3 Retrigger | Free / Key | Free | `lfo3.sync` |
| LFO 3 Delay | 0 ms … 5000 ms | 0 ms | `lfo3.delay` |
| LFO 3 Phase | 0 ° … 360 ° | 0 ° | `lfo3.phase` |
| LFO 3 Level | 0 % … 100 % | 100 % | `lfo3.level` |
| LFO 3 Tempo Sync | Off … 1/16 triplet (16 choices) | Off | `lfo3.tempoSync` |
