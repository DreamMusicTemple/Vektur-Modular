# 22. The step sequencer

The sequencer has 16 steps and three rows (A, B and C). Each step also has a gate, a probability and a ratchet setting (1–8 repeats).

- **Row A** plays notes, quantised to a scale and root.
- **Row B** can control velocity.
- **Row C** is a free modulation row.

You can set the direction, rate, swing and gate length. A seed makes the random directions repeatable.

The sequencer runs from the internal tempo, the host or MIDI clock. When *Plays Notes* is off, the rows act only as modulation sources for the matrix and the arpeggiator.

| Control | Range | Default | Identifier |
|---|---|---|---|
| Seq Run | Off / Run / Transport / Key | Off | `seq.run` |
| Seq Length | 1 … 16 (16 choices) | 16 | `seq.length` |
| Seq Direction | Forward / Reverse / Ping-Pong / Random / Random Walk | Forward | `seq.direction` |
| Seq Rate | 1/1 … 1/32 triplet (15 choices) | 1/16 | `seq.rate` |
| Seq Swing | 50 % … 75 % | 50 % | `seq.swing` |
| Seq Gate | 5 % … 200 % | 50 % | `seq.gate` |
| Seq Row A Range | 1 octave / 2 octaves / 3 octaves / 5 octaves | 2 octaves | `seq.pitchRange` |
| Seq Quantise Scale | Chromatic … Phrygian Dominant (16 choices) | Chromatic | `seq.scale` |
| Seq Root | C … B (12 choices) | C | `seq.root` |
| Seq Transpose | -24 st … 24 st | 0 st | `seq.transpose` |
| Seq Plays Notes | 0 … 1 | 1 | `seq.playNotes` |
| Seq Row B → Velocity | 0 … 1 | 0 | `seq.rowBVelocity` |
| Seq Seed | 0 … 65535 | 1 | `seq.seed` |
| Seq Step 1 Row A | 0 % … 100 % | 0 % | `seq.step1A` |
| Seq Step 1 Row B | 0 % … 100 % | 50 % | `seq.step1B` |
| Seq Step 1 Row C | 0 % … 100 % | 50 % | `seq.step1C` |
| Seq Step 1 Gate | Off / On / Tie / Skip | On | `seq.step1Gate` |
| Seq Step 1 Probability | 0 % … 100 % | 100 % | `seq.step1Probability` |
| Seq Step 1 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step1Ratchet` |
| Seq Step 2 Row A | 0 % … 100 % | 0 % | `seq.step2A` |
| Seq Step 2 Row B | 0 % … 100 % | 50 % | `seq.step2B` |
| Seq Step 2 Row C | 0 % … 100 % | 50 % | `seq.step2C` |
| Seq Step 2 Gate | Off / On / Tie / Skip | On | `seq.step2Gate` |
| Seq Step 2 Probability | 0 % … 100 % | 100 % | `seq.step2Probability` |
| Seq Step 2 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step2Ratchet` |
| Seq Step 3 Row A | 0 % … 100 % | 0 % | `seq.step3A` |
| Seq Step 3 Row B | 0 % … 100 % | 50 % | `seq.step3B` |
| Seq Step 3 Row C | 0 % … 100 % | 50 % | `seq.step3C` |
| Seq Step 3 Gate | Off / On / Tie / Skip | On | `seq.step3Gate` |
| Seq Step 3 Probability | 0 % … 100 % | 100 % | `seq.step3Probability` |
| Seq Step 3 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step3Ratchet` |
| Seq Step 4 Row A | 0 % … 100 % | 0 % | `seq.step4A` |
| Seq Step 4 Row B | 0 % … 100 % | 50 % | `seq.step4B` |
| Seq Step 4 Row C | 0 % … 100 % | 50 % | `seq.step4C` |
| Seq Step 4 Gate | Off / On / Tie / Skip | On | `seq.step4Gate` |
| Seq Step 4 Probability | 0 % … 100 % | 100 % | `seq.step4Probability` |
| Seq Step 4 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step4Ratchet` |
| Seq Step 5 Row A | 0 % … 100 % | 0 % | `seq.step5A` |
| Seq Step 5 Row B | 0 % … 100 % | 50 % | `seq.step5B` |
| Seq Step 5 Row C | 0 % … 100 % | 50 % | `seq.step5C` |
| Seq Step 5 Gate | Off / On / Tie / Skip | On | `seq.step5Gate` |
| Seq Step 5 Probability | 0 % … 100 % | 100 % | `seq.step5Probability` |
| Seq Step 5 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step5Ratchet` |
| Seq Step 6 Row A | 0 % … 100 % | 0 % | `seq.step6A` |
| Seq Step 6 Row B | 0 % … 100 % | 50 % | `seq.step6B` |
| Seq Step 6 Row C | 0 % … 100 % | 50 % | `seq.step6C` |
| Seq Step 6 Gate | Off / On / Tie / Skip | On | `seq.step6Gate` |
| Seq Step 6 Probability | 0 % … 100 % | 100 % | `seq.step6Probability` |
| Seq Step 6 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step6Ratchet` |
| Seq Step 7 Row A | 0 % … 100 % | 0 % | `seq.step7A` |
| Seq Step 7 Row B | 0 % … 100 % | 50 % | `seq.step7B` |
| Seq Step 7 Row C | 0 % … 100 % | 50 % | `seq.step7C` |
| Seq Step 7 Gate | Off / On / Tie / Skip | On | `seq.step7Gate` |
| Seq Step 7 Probability | 0 % … 100 % | 100 % | `seq.step7Probability` |
| Seq Step 7 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step7Ratchet` |
| Seq Step 8 Row A | 0 % … 100 % | 0 % | `seq.step8A` |
| Seq Step 8 Row B | 0 % … 100 % | 50 % | `seq.step8B` |
| Seq Step 8 Row C | 0 % … 100 % | 50 % | `seq.step8C` |
| Seq Step 8 Gate | Off / On / Tie / Skip | On | `seq.step8Gate` |
| Seq Step 8 Probability | 0 % … 100 % | 100 % | `seq.step8Probability` |
| Seq Step 8 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step8Ratchet` |
| Seq Step 9 Row A | 0 % … 100 % | 0 % | `seq.step9A` |
| Seq Step 9 Row B | 0 % … 100 % | 50 % | `seq.step9B` |
| Seq Step 9 Row C | 0 % … 100 % | 50 % | `seq.step9C` |
| Seq Step 9 Gate | Off / On / Tie / Skip | On | `seq.step9Gate` |
| Seq Step 9 Probability | 0 % … 100 % | 100 % | `seq.step9Probability` |
| Seq Step 9 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step9Ratchet` |
| Seq Step 10 Row A | 0 % … 100 % | 0 % | `seq.step10A` |
| Seq Step 10 Row B | 0 % … 100 % | 50 % | `seq.step10B` |
| Seq Step 10 Row C | 0 % … 100 % | 50 % | `seq.step10C` |
| Seq Step 10 Gate | Off / On / Tie / Skip | On | `seq.step10Gate` |
| Seq Step 10 Probability | 0 % … 100 % | 100 % | `seq.step10Probability` |
| Seq Step 10 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step10Ratchet` |
| Seq Step 11 Row A | 0 % … 100 % | 0 % | `seq.step11A` |
| Seq Step 11 Row B | 0 % … 100 % | 50 % | `seq.step11B` |
| Seq Step 11 Row C | 0 % … 100 % | 50 % | `seq.step11C` |
| Seq Step 11 Gate | Off / On / Tie / Skip | On | `seq.step11Gate` |
| Seq Step 11 Probability | 0 % … 100 % | 100 % | `seq.step11Probability` |
| Seq Step 11 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step11Ratchet` |
| Seq Step 12 Row A | 0 % … 100 % | 0 % | `seq.step12A` |
| Seq Step 12 Row B | 0 % … 100 % | 50 % | `seq.step12B` |
| Seq Step 12 Row C | 0 % … 100 % | 50 % | `seq.step12C` |
| Seq Step 12 Gate | Off / On / Tie / Skip | On | `seq.step12Gate` |
| Seq Step 12 Probability | 0 % … 100 % | 100 % | `seq.step12Probability` |
| Seq Step 12 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step12Ratchet` |
| Seq Step 13 Row A | 0 % … 100 % | 0 % | `seq.step13A` |
| Seq Step 13 Row B | 0 % … 100 % | 50 % | `seq.step13B` |
| Seq Step 13 Row C | 0 % … 100 % | 50 % | `seq.step13C` |
| Seq Step 13 Gate | Off / On / Tie / Skip | On | `seq.step13Gate` |
| Seq Step 13 Probability | 0 % … 100 % | 100 % | `seq.step13Probability` |
| Seq Step 13 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step13Ratchet` |
| Seq Step 14 Row A | 0 % … 100 % | 0 % | `seq.step14A` |
| Seq Step 14 Row B | 0 % … 100 % | 50 % | `seq.step14B` |
| Seq Step 14 Row C | 0 % … 100 % | 50 % | `seq.step14C` |
| Seq Step 14 Gate | Off / On / Tie / Skip | On | `seq.step14Gate` |
| Seq Step 14 Probability | 0 % … 100 % | 100 % | `seq.step14Probability` |
| Seq Step 14 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step14Ratchet` |
| Seq Step 15 Row A | 0 % … 100 % | 0 % | `seq.step15A` |
| Seq Step 15 Row B | 0 % … 100 % | 50 % | `seq.step15B` |
| Seq Step 15 Row C | 0 % … 100 % | 50 % | `seq.step15C` |
| Seq Step 15 Gate | Off / On / Tie / Skip | On | `seq.step15Gate` |
| Seq Step 15 Probability | 0 % … 100 % | 100 % | `seq.step15Probability` |
| Seq Step 15 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step15Ratchet` |
| Seq Step 16 Row A | 0 % … 100 % | 0 % | `seq.step16A` |
| Seq Step 16 Row B | 0 % … 100 % | 50 % | `seq.step16B` |
| Seq Step 16 Row C | 0 % … 100 % | 50 % | `seq.step16C` |
| Seq Step 16 Gate | Off / On / Tie / Skip | On | `seq.step16Gate` |
| Seq Step 16 Probability | 0 % … 100 % | 100 % | `seq.step16Probability` |
| Seq Step 16 Ratchet | 1× / 2× / 3× / 4× | 1× | `seq.step16Ratchet` |
