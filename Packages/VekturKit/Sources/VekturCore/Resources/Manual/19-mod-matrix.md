# 19. The modulation matrix

The matrix has 16 slots. Each slot connects a **source** (an envelope, LFO, velocity, key, aftertouch, mod wheel, MPE expression, macro, sequencer row and more) to a **destination** (most continuous parameters). A second source can scale the slot's depth. For example, the mod wheel can scale the depth of LFO vibrato.

Each slot also has an amount, an offset, a curve, a polarity setting (unipolar or bipolar), invert and slew. A slot's live value is shown while you play. Destinations are modulated per voice, so every note can move differently.

| Control | Range | Default | Identifier |
|---|---|---|---|
| Mod 1 Enable | 0 … 1 | 1 | `matrix.slot1Enable` |
| Mod 1 Source | None … Seq Row C (26 choices) | None | `matrix.slot1Source` |
| Mod 1 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot1Via` |
| Mod 1 Destination | 0 … 16447 | 0 | `matrix.slot1Dest` |
| Mod 1 Amount | -100 % … 100 % | 0 % | `matrix.slot1Amount` |
| Mod 1 Offset | -100 % … 100 % | 0 % | `matrix.slot1Offset` |
| Mod 1 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot1Curve` |
| Mod 1 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot1Polarity` |
| Mod 1 Invert | 0 … 1 | 0 | `matrix.slot1Invert` |
| Mod 1 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot1Slew` |
| Mod 2 Enable | 0 … 1 | 1 | `matrix.slot2Enable` |
| Mod 2 Source | None … Seq Row C (26 choices) | None | `matrix.slot2Source` |
| Mod 2 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot2Via` |
| Mod 2 Destination | 0 … 16447 | 0 | `matrix.slot2Dest` |
| Mod 2 Amount | -100 % … 100 % | 0 % | `matrix.slot2Amount` |
| Mod 2 Offset | -100 % … 100 % | 0 % | `matrix.slot2Offset` |
| Mod 2 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot2Curve` |
| Mod 2 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot2Polarity` |
| Mod 2 Invert | 0 … 1 | 0 | `matrix.slot2Invert` |
| Mod 2 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot2Slew` |
| Mod 3 Enable | 0 … 1 | 1 | `matrix.slot3Enable` |
| Mod 3 Source | None … Seq Row C (26 choices) | None | `matrix.slot3Source` |
| Mod 3 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot3Via` |
| Mod 3 Destination | 0 … 16447 | 0 | `matrix.slot3Dest` |
| Mod 3 Amount | -100 % … 100 % | 0 % | `matrix.slot3Amount` |
| Mod 3 Offset | -100 % … 100 % | 0 % | `matrix.slot3Offset` |
| Mod 3 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot3Curve` |
| Mod 3 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot3Polarity` |
| Mod 3 Invert | 0 … 1 | 0 | `matrix.slot3Invert` |
| Mod 3 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot3Slew` |
| Mod 4 Enable | 0 … 1 | 1 | `matrix.slot4Enable` |
| Mod 4 Source | None … Seq Row C (26 choices) | None | `matrix.slot4Source` |
| Mod 4 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot4Via` |
| Mod 4 Destination | 0 … 16447 | 0 | `matrix.slot4Dest` |
| Mod 4 Amount | -100 % … 100 % | 0 % | `matrix.slot4Amount` |
| Mod 4 Offset | -100 % … 100 % | 0 % | `matrix.slot4Offset` |
| Mod 4 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot4Curve` |
| Mod 4 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot4Polarity` |
| Mod 4 Invert | 0 … 1 | 0 | `matrix.slot4Invert` |
| Mod 4 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot4Slew` |
| Mod 5 Enable | 0 … 1 | 1 | `matrix.slot5Enable` |
| Mod 5 Source | None … Seq Row C (26 choices) | None | `matrix.slot5Source` |
| Mod 5 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot5Via` |
| Mod 5 Destination | 0 … 16447 | 0 | `matrix.slot5Dest` |
| Mod 5 Amount | -100 % … 100 % | 0 % | `matrix.slot5Amount` |
| Mod 5 Offset | -100 % … 100 % | 0 % | `matrix.slot5Offset` |
| Mod 5 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot5Curve` |
| Mod 5 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot5Polarity` |
| Mod 5 Invert | 0 … 1 | 0 | `matrix.slot5Invert` |
| Mod 5 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot5Slew` |
| Mod 6 Enable | 0 … 1 | 1 | `matrix.slot6Enable` |
| Mod 6 Source | None … Seq Row C (26 choices) | None | `matrix.slot6Source` |
| Mod 6 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot6Via` |
| Mod 6 Destination | 0 … 16447 | 0 | `matrix.slot6Dest` |
| Mod 6 Amount | -100 % … 100 % | 0 % | `matrix.slot6Amount` |
| Mod 6 Offset | -100 % … 100 % | 0 % | `matrix.slot6Offset` |
| Mod 6 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot6Curve` |
| Mod 6 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot6Polarity` |
| Mod 6 Invert | 0 … 1 | 0 | `matrix.slot6Invert` |
| Mod 6 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot6Slew` |
| Mod 7 Enable | 0 … 1 | 1 | `matrix.slot7Enable` |
| Mod 7 Source | None … Seq Row C (26 choices) | None | `matrix.slot7Source` |
| Mod 7 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot7Via` |
| Mod 7 Destination | 0 … 16447 | 0 | `matrix.slot7Dest` |
| Mod 7 Amount | -100 % … 100 % | 0 % | `matrix.slot7Amount` |
| Mod 7 Offset | -100 % … 100 % | 0 % | `matrix.slot7Offset` |
| Mod 7 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot7Curve` |
| Mod 7 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot7Polarity` |
| Mod 7 Invert | 0 … 1 | 0 | `matrix.slot7Invert` |
| Mod 7 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot7Slew` |
| Mod 8 Enable | 0 … 1 | 1 | `matrix.slot8Enable` |
| Mod 8 Source | None … Seq Row C (26 choices) | None | `matrix.slot8Source` |
| Mod 8 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot8Via` |
| Mod 8 Destination | 0 … 16447 | 0 | `matrix.slot8Dest` |
| Mod 8 Amount | -100 % … 100 % | 0 % | `matrix.slot8Amount` |
| Mod 8 Offset | -100 % … 100 % | 0 % | `matrix.slot8Offset` |
| Mod 8 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot8Curve` |
| Mod 8 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot8Polarity` |
| Mod 8 Invert | 0 … 1 | 0 | `matrix.slot8Invert` |
| Mod 8 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot8Slew` |
| Mod 9 Enable | 0 … 1 | 1 | `matrix.slot9Enable` |
| Mod 9 Source | None … Seq Row C (26 choices) | None | `matrix.slot9Source` |
| Mod 9 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot9Via` |
| Mod 9 Destination | 0 … 16447 | 0 | `matrix.slot9Dest` |
| Mod 9 Amount | -100 % … 100 % | 0 % | `matrix.slot9Amount` |
| Mod 9 Offset | -100 % … 100 % | 0 % | `matrix.slot9Offset` |
| Mod 9 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot9Curve` |
| Mod 9 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot9Polarity` |
| Mod 9 Invert | 0 … 1 | 0 | `matrix.slot9Invert` |
| Mod 9 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot9Slew` |
| Mod 10 Enable | 0 … 1 | 1 | `matrix.slot10Enable` |
| Mod 10 Source | None … Seq Row C (26 choices) | None | `matrix.slot10Source` |
| Mod 10 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot10Via` |
| Mod 10 Destination | 0 … 16447 | 0 | `matrix.slot10Dest` |
| Mod 10 Amount | -100 % … 100 % | 0 % | `matrix.slot10Amount` |
| Mod 10 Offset | -100 % … 100 % | 0 % | `matrix.slot10Offset` |
| Mod 10 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot10Curve` |
| Mod 10 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot10Polarity` |
| Mod 10 Invert | 0 … 1 | 0 | `matrix.slot10Invert` |
| Mod 10 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot10Slew` |
| Mod 11 Enable | 0 … 1 | 1 | `matrix.slot11Enable` |
| Mod 11 Source | None … Seq Row C (26 choices) | None | `matrix.slot11Source` |
| Mod 11 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot11Via` |
| Mod 11 Destination | 0 … 16447 | 0 | `matrix.slot11Dest` |
| Mod 11 Amount | -100 % … 100 % | 0 % | `matrix.slot11Amount` |
| Mod 11 Offset | -100 % … 100 % | 0 % | `matrix.slot11Offset` |
| Mod 11 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot11Curve` |
| Mod 11 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot11Polarity` |
| Mod 11 Invert | 0 … 1 | 0 | `matrix.slot11Invert` |
| Mod 11 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot11Slew` |
| Mod 12 Enable | 0 … 1 | 1 | `matrix.slot12Enable` |
| Mod 12 Source | None … Seq Row C (26 choices) | None | `matrix.slot12Source` |
| Mod 12 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot12Via` |
| Mod 12 Destination | 0 … 16447 | 0 | `matrix.slot12Dest` |
| Mod 12 Amount | -100 % … 100 % | 0 % | `matrix.slot12Amount` |
| Mod 12 Offset | -100 % … 100 % | 0 % | `matrix.slot12Offset` |
| Mod 12 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot12Curve` |
| Mod 12 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot12Polarity` |
| Mod 12 Invert | 0 … 1 | 0 | `matrix.slot12Invert` |
| Mod 12 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot12Slew` |
| Mod 13 Enable | 0 … 1 | 1 | `matrix.slot13Enable` |
| Mod 13 Source | None … Seq Row C (26 choices) | None | `matrix.slot13Source` |
| Mod 13 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot13Via` |
| Mod 13 Destination | 0 … 16447 | 0 | `matrix.slot13Dest` |
| Mod 13 Amount | -100 % … 100 % | 0 % | `matrix.slot13Amount` |
| Mod 13 Offset | -100 % … 100 % | 0 % | `matrix.slot13Offset` |
| Mod 13 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot13Curve` |
| Mod 13 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot13Polarity` |
| Mod 13 Invert | 0 … 1 | 0 | `matrix.slot13Invert` |
| Mod 13 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot13Slew` |
| Mod 14 Enable | 0 … 1 | 1 | `matrix.slot14Enable` |
| Mod 14 Source | None … Seq Row C (26 choices) | None | `matrix.slot14Source` |
| Mod 14 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot14Via` |
| Mod 14 Destination | 0 … 16447 | 0 | `matrix.slot14Dest` |
| Mod 14 Amount | -100 % … 100 % | 0 % | `matrix.slot14Amount` |
| Mod 14 Offset | -100 % … 100 % | 0 % | `matrix.slot14Offset` |
| Mod 14 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot14Curve` |
| Mod 14 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot14Polarity` |
| Mod 14 Invert | 0 … 1 | 0 | `matrix.slot14Invert` |
| Mod 14 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot14Slew` |
| Mod 15 Enable | 0 … 1 | 1 | `matrix.slot15Enable` |
| Mod 15 Source | None … Seq Row C (26 choices) | None | `matrix.slot15Source` |
| Mod 15 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot15Via` |
| Mod 15 Destination | 0 … 16447 | 0 | `matrix.slot15Dest` |
| Mod 15 Amount | -100 % … 100 % | 0 % | `matrix.slot15Amount` |
| Mod 15 Offset | -100 % … 100 % | 0 % | `matrix.slot15Offset` |
| Mod 15 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot15Curve` |
| Mod 15 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot15Polarity` |
| Mod 15 Invert | 0 … 1 | 0 | `matrix.slot15Invert` |
| Mod 15 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot15Slew` |
| Mod 16 Enable | 0 … 1 | 1 | `matrix.slot16Enable` |
| Mod 16 Source | None … Seq Row C (26 choices) | None | `matrix.slot16Source` |
| Mod 16 Scale By | None … Seq Row C (26 choices) | None | `matrix.slot16Via` |
| Mod 16 Destination | 0 … 16447 | 0 | `matrix.slot16Dest` |
| Mod 16 Amount | -100 % … 100 % | 0 % | `matrix.slot16Amount` |
| Mod 16 Offset | -100 % … 100 % | 0 % | `matrix.slot16Offset` |
| Mod 16 Curve | Linear / Exponential / Logarithmic / S-Curve | Linear | `matrix.slot16Curve` |
| Mod 16 Polarity | As Source / Unipolar / Bipolar | As Source | `matrix.slot16Polarity` |
| Mod 16 Invert | 0 … 1 | 0 | `matrix.slot16Invert` |
| Mod 16 Slew | 0 ms … 2000 ms | 0 ms | `matrix.slot16Slew` |
