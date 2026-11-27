# 29. MIDI input, MPE and key zones

Vektur Modular receives MIDI 1.0 and MIDI 2.0. Under MIDI 2.0, velocity and controllers keep their full resolution. 14-bit CC pairs, RPN and NRPN are supported.

- **Receive Channel** can be omni or a single channel.
- **Velocity Curve** can be linear, soft, hard or fixed.
- **MPE** supports a lower or an upper zone, with per-note pitch bend, pressure and slide (CC 74) on member channels and a configurable bend range.
- **Key Zones** can split or layer the keyboard, with a transpose for each zone.
- **MIDI Out** can send the generated notes (from the arpeggiator, sequencer and generator) or all notes, on a chosen channel, plus MIDI clock if you want it.
