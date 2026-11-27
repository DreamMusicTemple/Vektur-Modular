# 32. Tempo and sync

**Sync Source**:

- **Auto** follows the host when running as a plug-in, and incoming MIDI clock when it is present.
- **Internal** uses the instrument's own tempo, from 20 to 300 BPM.
- **MIDI Clock** follows an external clock, with start, stop and song position.

The standalone app can join an **Ableton Link** session, once the Link library is included in the build. The transport panel shows the tempo, the play state and the time signature. The sequencer, arpeggiator, generator, tempo-synced LFOs and tempo-synced delays all follow the same clock.
