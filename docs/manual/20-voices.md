# 20. Voices, polyphony and unison

**Polyphony** can be set from 1 to 32 voices. **Voice Mode** offers:

- **Poly.**
- **Mono** (retriggers).
- **Legato** (no retrigger while notes overlap).
- **Unison** (2 to 8 stacked voices with detune and stereo spread).
- **Duo** (two-note paraphony).

Note Priority applies to the mono modes. **Glide** (portamento) can be always on or only when playing legato, and can use a constant rate or a constant time.

On the rack, the **keyboard ctrl** panel shows only the Glide fader. Its **settings ▸** button opens a pop-up window with Voice Mode, Note Priority, Glide Mode, Unison Voices, Polyphony, pitch-bend ranges and Unison Detune. Tap *done*, or outside the window, to close it.

**Stereo Spread** pans voices across the stereo field. When more notes arrive than there are voices, the oldest released voice is stolen first, with a short fade so the change doesn't click.

| Control | Range | Default | Identifier |
|---|---|---|---|
| Master Volume | -60 dB … 6 dB | -6 dB | `global.masterVolume` |
| Master Tune | -100 ct … 100 ct | 0 ct | `global.masterTune` |
| Polyphony | 1 voice / 2 voices / 4 voices / 6 voices / 8 voices / 12 voices / 16 voices / 24 voices / 32 voices | 8 voices | `global.polyphony` |
| Pitch Bend Range | 0 st … 24 st | 2 st | `global.pitchBendRange` |
| Engine Mode | Authentic / Modern | Authentic | `global.engineMode` |
| Analogue Instability | 0 % … 100 % | 35 % | `global.instability` |
| Glide Time | 0 ms … 5000 ms | 0 ms | `global.glide` |
| Stereo Spread | 0 % … 100 % | 40 % | `global.stereoSpread` |
| Filter Routing | Filter A / Filter B / A → B / A + B | Filter B | `global.filterRouting` |
| Filter Oversampling | 1× / 2× / 4× | 2× | `global.oversampling` |
| Voice Mode | Poly / Mono / Legato / Unison / Duo | Poly | `global.voiceMode` |
| Note Priority | Last / Low / High | Last | `global.notePriority` |
| Glide Mode | Always / Legato Only | Always | `global.glideMode` |
| Unison Voices | 2 voices / 3 voices / 4 voices / 5 voices / 6 voices / 7 voices / 8 voices | 4 voices | `global.unisonVoices` |
| Unison Detune | 0 ct … 50 ct | 12 ct | `global.unisonDetune` |
| Pitch Bend Down Range | -1 st … 24 st | -1 st | `global.bendDown` |
| Glide Constant Time | 0 … 1 | 1 | `global.glideConstantTime` |
| Internal Tempo | 20 … 300 | 120 | `global.tempo` |
| MPE | Off / Lower Zone / Upper Zone | Off | `global.mpeMode` |
| MPE Member Channels | 1 … 15 (15 choices) | 15 | `global.mpeMemberChannels` |
| MPE Per-Note Bend Range | 0 st … 96 st | 48 st | `global.mpePitchRange` |
| Key Zones | Off / Split / Layer | Off | `global.zoneMode` |
| Split Point | C-1 … G9 (128 choices) | C4 | `global.splitPoint` |
| Zone A Transpose | -24 st … 24 st | 0 st | `global.zoneATranspose` |
| Zone B Transpose | -24 st … 24 st | 0 st | `global.zoneBTranspose` |
| Sync Source | Auto / Internal / MIDI Clock | Auto | `global.syncSource` |
| MIDI Receive Channel | Omni … 16 (17 choices) | Omni | `global.receiveChannel` |
| Velocity Curve | Linear / Soft / Hard / Fixed | Linear | `global.velocityCurve` |
| Respond to Program Change | 0 … 1 | 1 | `global.programChange` |
| MIDI Out | Off / Generated Notes / All Notes | Off | `global.midiOut` |
| MIDI Out Channel | 1 … 16 (16 choices) | 1 | `global.midiOutChannel` |
| Send MIDI Clock | 0 … 1 | 0 | `global.midiClockOut` |
| Oscillator Quality | Standard / HQ (2× oversampled) | Standard | `global.oscillatorQuality` |
