# 23. The arpeggiator

The arpeggiator has 14 patterns, including converge, diverge, thumb, pinky, chord and a random walk. It spans 1–4 octaves in three octave directions, at straight, dotted or triplet rates from 1/1 to 1/32, with gate and swing. A Euclidean rhythm mode (steps, pulses, rotate) can replace the straight rhythm.

Probability and ratchets add variation, and the same seed always gives the same variation. Velocity can be as played, fixed or accented. **Pattern Chain** plays up to four patterns in turn. **Latch** keeps the arpeggio running after you lift your hands. Turn on *Transpose by Sequencer* to let the step sequencer's row A move the arpeggio.

| Control | Range | Default | Identifier |
|---|---|---|---|
| Arp Mode | Off / On / Latch | Off | `arp.mode` |
| Arp Pattern | Up … Chord (14 choices) | Up | `arp.pattern` |
| Arp Octaves | 1 / 2 / 3 / 4 | 1 | `arp.octaves` |
| Arp Octave Direction | Up / Down / Up-Down | Up | `arp.octaveMode` |
| Arp Rate | 1/1 … 1/32 triplet (15 choices) | 1/16 | `arp.rate` |
| Arp Gate | 5 % … 200 % | 50 % | `arp.gate` |
| Arp Swing | 50 % … 75 % | 50 % | `arp.swing` |
| Arp Rhythm | Straight / Euclidean | Straight | `arp.rhythm` |
| Arp Euclid Steps | 1 … 32 (32 choices) | 8 | `arp.euclidSteps` |
| Arp Euclid Pulses | 0 … 32 (33 choices) | 5 | `arp.euclidPulses` |
| Arp Euclid Rotate | 0 … 31 (32 choices) | 0 | `arp.euclidRotate` |
| Arp Probability | 0 % … 100 % | 100 % | `arp.probability` |
| Arp Ratchet | 1× / 2× / 3× / 4× / 5× / 6× / 7× / 8× | 1× | `arp.ratchet` |
| Arp Ratchet Chance | 0 % … 100 % | 100 % | `arp.ratchetChance` |
| Arp Velocity | As Played / Fixed / Accent | As Played | `arp.velocityMode` |
| Arp Fixed Velocity | 1 … 127 | 100 | `arp.fixedVelocity` |
| Arp Restart | Auto / Beat-locked / Key Restart | Auto | `arp.restart` |
| Arp Seed | 0 … 65535 | 1 | `arp.seed` |
| Arp Pattern Chain | Off / 2 patterns / 3 patterns / 4 patterns | Off | `arp.chain` |
| Arp Chain Pattern 2 | Up … Chord (14 choices) | Down | `arp.chainPattern2` |
| Arp Chain Pattern 3 | Up … Chord (14 choices) | Up-Down | `arp.chainPattern3` |
| Arp Chain Pattern 4 | Up … Chord (14 choices) | Random | `arp.chainPattern4` |
| Arp Chain Passes | 1 … 16 (16 choices) | 1 | `arp.chainPasses` |
| Arp Transpose by Sequencer | 0 … 1 | 0 | `arp.seqTranspose` |
