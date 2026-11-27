# MIDI Implementation (Phase 7)

Vektur Modular handles all MIDI inside the real-time kernel (`VekturDSP/KernelMidi.cpp`), sample-accurately. The AUv3, the standalone app and the tests all use this same code. The host's events reach the kernel in one of two forms:
- MIDI 1.0 bytes (`VK_EVENT_MIDI1`);
- Universal MIDI Packets (`VK_EVENT_UMP`).

The AU advertises `audioUnitMIDIProtocol = MIDI 2.0`. Hosts that only speak MIDI 1.0 still work.

## Receive

| Message | MIDI 1.0 | MIDI 2.0 (UMP type 4) | Behaviour |
|---|---|---|---|
| Note on / off | ✅ | ✅ 16-bit velocity | Velocity curve: Linear, Soft (v^0.6), Hard (v^1.6), or Fixed (100). MIDI 2.0 velocity keeps its full resolution all the way to the voice. In MIDI 2.0, velocity 0 is a note-on. |
| Poly pressure | ✅ | ✅ 32-bit | Poly aftertouch source |
| Channel pressure | ✅ | ✅ 32-bit | Aftertouch source; learnable |
| Pitch bend | ✅ 14-bit | ✅ 32-bit | Range: Pitch Bend Range (up) / Bend Down; learnable |
| CC 0 / 32 | ✅ | ✅ | Bank select, reported with Program Change |
| CC 1, 2, 11 | ✅ | ✅ 32-bit | Mod wheel, breath, expression sources |
| CC 64 | ✅ | ✅ | Sustain. Never captured by Learn. |
| CC 120 / 123 | ✅ | ✅ | All Sound Off (panic) / All Notes Off |
| CC 0–31 + 32–63 | ✅ | — | 14-bit controller pairs (learn source "CC 14-bit") |
| Other CCs | ✅ | ✅ 32-bit | Available to MIDI Learn |
| RPN 0 (bend sensitivity) | ✅ | ✅ (registered controller) | Sets Pitch Bend Range: semitones + cents. On an MPE member channel, sets the per-note bend range. |
| RPN 1 (fine tuning) | ✅ | ✅ | Master Tune ±100 cents |
| RPN 6 (MPE Configuration Message) | ✅ | ✅ | Sent on channel 1 or 16. Sets the lower or upper zone and its member count; 0 turns the zone off. |
| RPN 127/127 (null) | ✅ | — | Data entry is ignored |
| NRPN, data entry 6/38, increment 96 / decrement 97 | ✅ | ✅ (assignable controller, 32-bit) | Available to MIDI Learn. Increment and decrement step relative mappings. |
| Program change | ✅ | ✅ (with bank-valid flag) | When "Respond to Program Change" is on, it selects a preset. Bank MSB 0 and 1 = the two factory banks (programs 0–31). Bank MSB 2 = user presets in name order. Other banks are ignored. |
| Per-note pitch bend | — | ✅ | That note only, × the MPE per-note bend range |
| Per-note controller 74 (registered or assignable) | — | ✅ | That note's timbre (the "MPE Timbre" matrix source) |
| Per-note management (reset) | — | ✅ | Resets that note's bend and timbre |
| Timing clock F8 | ✅ | ✅ (UMP type 1) | 24 ppqn. Tempo is measured with fast-then-slow smoothing (jitter of ±40 samples gives an error under 0.6 BPM). |
| Start FA / Continue FB / Stop FC | ✅ | ✅ | Transport for the arpeggiator, sequencer, generative engine and synced LFOs and effects |
| Song Position F2 | ✅ | ✅ | Locates (sixteenths) |
| Receive channel | — | — | Omni or 1–16. In MPE mode the zone's channels are always received. |

**MPE.** Notes, bend, pressure and CC 74 on member channels are per note. The master channel and every other channel are channel-wide. Per-note expression on member channels is never applied to MIDI Learn mappings, so CC 74 timbre does not move a global cutoff mapping.

## Sync source

Global *Sync Source* chooses where tempo and transport come from:
- **Auto:** the host transport when the host provides it, else incoming MIDI clock (if a tick arrived in the last 0.5 s), else the Tempo control.
- **Internal:** always the Tempo control.
- **MIDI Clock:** incoming clock if present, else the Tempo control.

The host's time signature (`currentMeasureDownbeatPosition`, numerator and denominator) sets the bar length. Generative phrases are whole bars of the host's meter, including pick-up bars. Stutter "1 bar" and "2 bars" grids follow the meter. Without a host meter the engine assumes 4/4.

## Transmit (AU MIDI output "Vektur Out"; standalone virtual source "Vektur Modular")

| Setting | Sends |
|---|---|
| MIDI Out = Generated Notes | Arpeggiator, sequencer and generative notes, on the MIDI Out Channel, at their exact sample offsets |
| MIDI Out = All Notes | The above, plus played notes |
| Send MIDI Clock | F8 at 24 ppqn on the engine's beat grid (locked to the host's beats when synced), with FA when enabled and FC when disabled. Ticks re-align after a host locate or loop. |

At most 512 events go out per render buffer. Every note-on sent has its note-off.

## MIDI Learn

- **Sources:** CC, 14-bit CC (pairs 0–31/32–63), NRPN, pitch bend, channel pressure, and notes.
- **Range:** minimum and maximum along the parameter's own taper (log for frequencies). Setting minimum above maximum inverts the control.
- **Absolute mode:** optional *pickup*. The control is ignored until it reaches or crosses the parameter's current value. Pickup re-arms whenever the host, the UI or automation moves the parameter.
- **Toggle mode:** each press flips between the range ends (CC ≥ 64, or note-on). Notes learnt in absolute mode become toggles.
- **Relative mode:** for endless encoders.
  - Encodings: two's complement (1…63 up, 127…65 down), binary offset (65 up / 63 down), or signed bit (1…63 up, 65…127 down).
  - Sensitivity sets the step size, default 1/128 of the range.
  - Results are clamped to the range.
- **Scopes:**
  - **This preset:** saved in the state's `midi` section, so it travels with the project or preset.
  - **All presets:** saved on the device in the app group, so the app and every AUv3 instance share it.
  - Where a preset mapping uses the same control or targets the same parameter as an all-presets mapping, the preset mapping wins. At most 256 mappings are active.
- **Learning:** arm it, then move a control. It reports the first mappable message; reserved controllers (bank, data entry, RPN/NRPN selectors, 120+) and sustain are skipped. The message still plays.
- **Host sync:** values changed by MIDI (learnt controls, RPN, MCM) are pushed back into the AU parameter tree at 30 Hz, so the host and the UI show them.

## Hardware profiles (`.vekturmidi`)

A profile is a JSON file:

```json
{
  "format": "com.dmtcymatics.vekturmodular.midi-profile",
  "version": 1,
  "name": "My controller",
  "device": "Knobs 16",
  "mappings": [
    { "source": "cc", "channel": 0, "number": 74, "parameter": "vcfB.cutoff",
      "minimum": 0, "maximum": 1, "mode": "absolute", "relative": "twosComplement",
      "pickup": false, "sensitivity": 0.0078125 }
  ]
}
```

- `channel` is 0–15; leave it out for any channel.
- `parameter` is the stable parameter identifier, never the address.
- Unknown keys and invalid entries are ignored.
- A profile from a newer format version is refused with a message.

**Standard layout** (factory profile, General MIDI sound controllers):

| CC | Parameter |
|---|---|
| 74 | VCF B cutoff |
| 71 | VCF B resonance |
| 73 | EG 2 attack |
| 75 | EG 2 decay |
| 72 | EG 2 release |
| 76 | LFO 1 rate |
| 5 | Glide time |
| 7 | Master volume (with pickup) |

## Ableton Link (standalone)

The standalone app hosts the audio unit, so with Link on, the app gives the unit the Link session's tempo, beat and start/stop state. The beat uses a 4-beat quantum and is compensated for output latency. These go through the same host musical-context blocks a DAW uses.

Link requires all of the following:
- **LinkKit.xcframework**, which needs a licence from Ableton and is not in this repository. Add it to the app target and `LinkTransport.swift` compiles the Link code in.
- `NSLocalNetworkUsageDescription`, already set.
- On iOS, the `com.apple.developer.networking.multicast` entitlement, requested from Apple. It is commented in `project.yml` until granted.

Without LinkKit the app follows its own Tempo control.
