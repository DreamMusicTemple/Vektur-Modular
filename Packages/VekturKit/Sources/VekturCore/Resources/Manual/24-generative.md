# 24. The generative engine

The generator writes musical phrases as you listen, in five styles: melody, bassline, chords, ambient and broken chords. It works within any of 16 scales, a root, a range and a register.

- **Density** sets how many notes play.
- **Complexity** sets how adventurous the rhythm and the intervals are.
- **Evolve** lets the phrase change a little on each repeat.
- **Humanise** loosens the timing and velocity.

*Follow Keys* can transpose the phrase from the keyboard, or play it only while keys are held. The seed makes the output repeatable. Use *Capture* on the Sequencer screen to keep a phrase as a note clip (chapter 25).

| Control | Range | Default | Identifier |
|---|---|---|---|
| Generate | Off / On | Off | `gen.mode` |
| Generate Style | Melody / Bassline / Chords / Ambient / Broken Chords | Melody | `gen.style` |
| Generate Scale | Chromatic … Phrygian Dominant (16 choices) | Natural Minor | `gen.scale` |
| Generate Root | C … B (12 choices) | A | `gen.root` |
| Generate Density | 0 % … 100 % | 60 % | `gen.density` |
| Generate Complexity | 0 % … 100 % | 40 % | `gen.complexity` |
| Generate Range | 1 octave / 2 octaves / 3 octaves / 4 octaves | 2 octaves | `gen.range` |
| Generate Register | C-1 … G9 (128 choices) | C4 | `gen.register` |
| Generate Phrase Length | 1 bar / 2 bars / 4 bars / 8 bars | 4 bars | `gen.phraseBars` |
| Generate Grid | 1/1 … 1/32 triplet (15 choices) | 1/16 | `gen.rate` |
| Generate Seed | 0 … 65535 | 1 | `gen.seed` |
| Generate Evolve | 0 % … 100 % | 25 % | `gen.evolve` |
| Generate Humanise | 0 % … 100 % | 10 % | `gen.humanise` |
| Generate Follow Keys | Off / Transpose / Hold to Play | Off | `gen.followKeys` |
| Generate Swing | 50 % … 75 % | 50 % | `gen.swing` |
| Generate Gate | 5 % … 200 % | 85 % | `gen.gate` |
