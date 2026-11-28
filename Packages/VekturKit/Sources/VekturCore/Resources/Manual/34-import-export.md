# 34. Import and export

Everything can be exported as a file and opened on another device, or by a later version:

- **Preset** (.vekturpreset). One sound: parameters, cables and the preset's MIDI map. Your setup settings, such as the MIDI channel and zones, are not included.
- **Project** (.vekturproject). Everything: the sound, clips, appearance and layout.
- **Pattern** (.vekturpattern). Only the step sequencer and arpeggiator settings. Importing a pattern changes nothing else.
- **MIDI clip** (.mid). A Standard MIDI File, from the Sequencer screen.
- **MIDI profile** (.vekturmidi). Controller mappings.
- **MIDI reference** (.csv or .md). The parameter list.

All the Vektur formats are versioned JSON. A file written by a newer version is refused with a clear message, so it is never misread.
