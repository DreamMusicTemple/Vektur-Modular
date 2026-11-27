#!/usr/bin/env python3
"""Vektur Modular manual (Phase 9): 38 chapters.

The prose is written here. The parameter tables are generated from
tools/params.py's output (web/params.json), so they always match the build.

Writes
  docs/manual/NN-slug.md                        the manual (GitHub-readable)
  docs/manual/README.md                         contents
  Packages/VekturKit/Sources/VekturCore/Resources/Manual/  bundled copy + index.json
  web/manual.json                               the same chapters for the web build
Each chapter lists the screens it documents; the app's Help button opens the
first chapter for the current screen ("contextual help").
"""
import json, os, re, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PARAMS = json.load(open(os.path.join(ROOT, "web", "params.json")))
PARAMS = PARAMS["params"] if isinstance(PARAMS, dict) else PARAMS

UNIT = {"decibels": "dB", "cents": "ct", "semitones": "st", "hertz": "Hz", "seconds": "s", "milliseconds": "ms",
        "percent": "%", "octaves": "oct", "degrees": "°"}


def table(*prefixes):
    rows = [p for p in PARAMS if any(p["id"].startswith(x + ".") for x in prefixes)]
    if not rows:
        return ""
    out = ["", "| Control | Range | Default | Identifier |", "|---|---|---|---|"]
    for p in rows:
        if p.get("s"):
            rng = " / ".join(p["s"]) if len(p["s"]) <= 9 else f"{p['s'][0]} … {p['s'][-1]} ({len(p['s'])} choices)"
            d = p["s"][int(p["d"])] if 0 <= int(p["d"]) < len(p["s"]) else p["d"]
        else:
            u = UNIT.get(p.get("u", ""), "")
            f = lambda v: (str(int(v)) if float(v).is_integer() else f"{v:g}") + (f" {u}" if u else "")
            rng, d = f"{f(p['lo'])} … {f(p['hi'])}", f(p["d"])
        out.append(f"| {p['n']} | {rng} | {d} | `{p['id']}` |")
    return "\n".join(out) + "\n"


C = []  # (slug, title, screens, body)


def ch(slug, title, screens, body):
    C.append((slug, title, screens, body.strip() + "\n"))


ch("welcome", "Welcome", ["rack"], """
Vektur Modular is a vintage-style modular analogue synthesiser for iPhone, iPad and Mac. It runs on its own (the standalone app) and inside any AUv3 host such as Logic Pro, GarageBand, AUM, Cubasis or Loopy Pro. Both use the same sound engine, so a patch sounds the same everywhere.

The instrument is a pre-patched synthesiser you can play straight away: three main oscillators, noise and a ring modulator feed two filters, two envelopes and a VCA. Every module also has patch points. Drag a cable from any output to any input to change how it works, as you would on a modular system.

Around the voice there are six more oscillators, specialist filters, utilities, a 16-slot modulation matrix, a step sequencer, an arpeggiator, a generative phrase engine, eight effect slots and full MIDI 2.0 and MPE support.

This manual has 38 chapters. In the app, the **?** button on each screen opens the chapter for that screen.
""")

ch("first-sound", "Getting started: the first sound", ["rack", "performance"], """
1. Open the app. The audio engine starts on its own, and the status line at the bottom of the sidebar shows the sample rate and buffer size.
2. Play the on-screen keyboard at the bottom of the screen, a connected MIDI keyboard, or a MIDI app. Use the keyboard button in the toolbar, or the chevron above the keys, to show or hide it.
3. Open **Presets** and pick a sound from the *Classic* bank to hear what the instrument can do.
4. Go back to **Rack** and turn a knob. Drag up or down to change it. Double-tap to return it to its default. With VoiceOver, swipe up or down.

If you hear nothing, check the master volume on the **Mixer** screen and the device volume. Then check that the MIDI Receive Channel (Settings → MIDI) matches your keyboard. Diagnostics shows whether notes are arriving.
""")

ch("standalone-and-au", "Standalone app and AUv3 plug-in", ["settings"], """
**Standalone.** The app opens its own audio session, listens to every MIDI source (USB, Bluetooth, network and other apps) and offers a virtual MIDI source called *Vektur Modular*. Your session is saved when the app goes to the background and restored the next time it opens.

**AUv3.** Add *DMT Cymatics: Vektur Modular* as an instrument in your host. The host saves the complete state with the project: parameters, patch, clips, effects, MIDI map and appearance. All parameters can be automated and are listed in chapter 31. The plug-in reports its latency and tail time, so hosts can compensate.

Some settings belong only to the standalone app and are hidden in a plug-in: Keep Screen Awake, Background Audio and the audio buffer size. In a host, the host controls those.
""")

ch("interface", "The interface", ["rack", "settings"], """
On iPad and Mac, a sidebar lists the screens and the selected screen fills the rest of the window. On iPhone, the screens are tabs. Hold a screen's name, or use the window button in the toolbar, to open it in its own window. This works with Stage Manager, an external display or the Mac.

The top of the sidebar shows the DMT Cymatics logo and the instrument's name. **About** (in Settings) shows the version, the build and the legal notices.

Screens: Rack, Patch, Perform, Sequencer, Arpeggiator, Generative, Effects, Modulation, Mixer, MIDI, Presets, Settings, Diagnostics. Screens you play from show the keyboard underneath.
""")

ch("signal-flow", "Signal flow", ["rack", "patch"], """
Each voice runs this default path: **VCO 1–3 + sub + noise + ring modulator → mixer → Filter A and/or Filter B (Filter Routing) → VCA → effects → output.** EG 1 controls the filters (and pitch, if you set an amount). EG 2 controls the VCA. LFO 1 and the sample & hold feed the filter CV inputs through normalled connections.

These default connections are called *normals* (chapter 17). Plugging a cable into an input replaces the normal on that input. The Patch screen can show the normals as faint lines.

The extra modules (VCO 4–9, specialist filters, utilities) are not connected until you patch them.
""")

ch("oscillators", "Oscillators VCO 1–3", ["rack"], """
VCO 1–3 are the main oscillators. Each has six waveforms, a ±3-octave switch, coarse and fine tuning, pulse width, a level on the mixer, linear or exponential FM, and hard or soft sync. VCO 1 also has a sub-oscillator one or two octaves down.

**Low Frequency** turns an oscillator into a slow modulation source (1/256 of its pitch). **Phase Reset** restarts the waveform at Start Phase on every note, for consistent attacks. Use VCO 3 in low-frequency mode as a third LFO: its output is normalled to the filter's VCO 3 amount.

**Oscillator Quality** (Settings → Performance) switches all oscillators to *HQ*. HQ runs them at twice the sample rate with a half-band decimator, which reduces aliasing on high notes by 7–10 dB. It costs roughly a quarter more engine CPU (measured: 12 % → 15.5 % of one core at 16 voices).
""" + table("vco1", "vco2", "vco3"))

ch("extra-oscillators", "Extra oscillators VCO 4–9", ["rack", "patch"], """
VCO 4–9 are six more oscillators with the same controls as VCO 1–3, except for the sub-oscillator. They are not connected by default. Patch their outputs into the mixer's inputs, an FM input, a filter's audio input or a VCA, or use them as modulation sources.

They track the keyboard through the voice's pitch, plus their own octave and tuning settings.
""" + table("vco4"))

ch("noise-ring-mixer", "Noise, ring modulator and mixer", ["rack", "mixer"], """
**Noise** produces white or pink noise. Colour tilts the spectrum darker or brighter. **Ring Mod** multiplies VCO 1 by VCO 2 by default. Carrier Leak lets some of the dry signal through, as worn ring modulators do.

The voice mixer sets the level of each source going into the filters. Overdriving it saturates gently, so it is safe to push for a fatter sound. **Mixer 4** is a separate four-channel utility mixer with polarity (−100…100 %) per channel. Use it for CV as well as audio.
""" + table("noise", "ringMod", "mixer4"))

ch("filter-a", "Filter A: the multimode filter", ["rack"], """
Filter A is a 12 dB/octave state-variable filter with low-pass, band-pass, high-pass and notch modes. It stays stable when VCO 3 sweeps its cutoff at audio rate. Resonance starts to self-oscillate at about 95 %. Drive adds input saturation with level make-up.

The EG 1, VCO 3 and S&H amounts are in octaves: +1 opens the filter one octave per unit of modulation. Key Track at 100 % makes the cutoff follow the keyboard exactly, so a self-oscillating filter can be played in tune.

**Filter Oversampling** (1×, 2× or 4×, in Settings) runs the filters at a higher internal rate. This reduces aliasing from drive and resonance.
""" + table("vcfA"))

ch("filter-b-routing", "Filter B: the ladder, and filter routing", ["rack"], """
Filter B is a four-pole (24 dB/octave) transistor-ladder low-pass filter with saturation at its input. It self-oscillates from about 87 % resonance, in tune. As on the hardware it is modelled on, raising the resonance thins the bass. *Authentic* mode keeps most of that loss; *Modern* restores more of the low end.

**Filter Routing** chooses how the voice uses the two filters: A only, B only, A into B in series, or A and B in parallel. A high-pass A in series with B gives a resonant band-pass. Parallel routing gives two-peak, vocal-like sounds.
""" + table("vcfB"))

ch("specialist-filters", "Specialist filters", ["rack", "patch"], """
Seven extra filters are available as patchable modules. None of them is connected by default:

- **Diode ladder.** A squelchy filter in the style of classic bass machines.
- **Transistor ladder.** A second ladder filter with two CV inputs.
- **OTA.** A state-variable filter with low-pass, band-pass and high-pass modes.
- **VCBPF.** A voltage-controlled band-pass filter with Q from 0.5 to 40.
- **Comb.** A tuned comb filter. Positive feedback gives metallic sounds; negative feedback gives hollow ones.
- **Formant.** Sweeps between vowels.
- **Filter bank.** A fixed eight-band bank, 125 Hz to 12 kHz.

Patch the voice mixer, or any oscillator, into a filter's input and patch its output into the VCA's audio input.
""" + table("diodeLadder", "transistorLadder", "ota", "vcbpf", "comb", "formant", "filterBank"))

ch("envelopes", "Envelopes EG 1 and EG 2", ["rack"], """
Both envelopes are ADSR envelopes with times from 0.5 ms to 20 s. There are four curve types; *Analogue* imitates an RC charge. Velocity scales the envelope's depth. **Retrigger** chooses whether a new note restarts the envelope from zero or from its current level. **Loop** cycles attack and decay while the key is held, which turns the envelope into an LFO with a shaped curve.

EG 1 is normalled to the filters and has a pitch amount for drum and sweep sounds. EG 2 is normalled to the VCA.
""" + table("eg1", "eg2"))

ch("vcas", "VCAs", ["rack"], """
The main VCA shapes each voice's level, with a linear or exponential response. Saturation adds gentle warmth when the VCA is driven hard. Initial Gain lets sound through without an envelope, which is useful for drones and for external processing.

**VCA 2** is a free utility VCA. Use it to make a modulation depth itself modulated, or to gate a second sound source.
""" + table("vca", "vca2"))

ch("lfos", "LFOs", ["rack", "modulation"], """
There are three LFOs. Each has seven shapes, a rate from 0.01 Hz to 50 Hz or a tempo-synced division, key or free retrigger, a fade-in delay, a start phase and a level. Tempo-synced LFOs follow the internal tempo, the host or MIDI clock (chapter 32).

LFO 1 is normalled to vibrato and the filter. LFO 2 and LFO 3 are free sources for the modulation matrix and for cables.
""" + table("lfo1", "lfo2", "lfo3"))

ch("utilities", "Utilities", ["rack", "patch"], """
- **Sample & Hold** samples noise at its own rate, with optional slew. It is normalled to the filters' S&H amounts.
- **Slew** limits how fast a signal can rise or fall: portamento for CV, or smoothing for stepped modulation.
- **Switch** routes one of up to four inputs, chosen manually, by CV or by stepping through them on each gate.
- **Gate Delay** delays and re-times gates, for echoes and offset envelopes.
- **Envelope Follower** turns the level of any audio into CV.
- **Voltage Processor** mixes two inputs with gain, offset, inversion and limiting.
- **Macros 1–4** are performance knobs. You can patch them, map them to MIDI and automate them.
""" + table("sampleHold", "slew", "switch", "gateDelay", "follower", "vproc", "macro"))

ch("patching", "Patching with cables", ["patch", "rack"], """
To patch, **drag from any jack to another jack.** You can drag in either direction, from an output to an input or from an input to an output. While you drag, a live cable follows your finger, and jacks that can accept the connection light up. Release over a jack to connect, or anywhere else to cancel. Near the edge of the screen, the rack scrolls with you.

Tap one jack and then another to connect them without dragging. Cables sag and sway gently. A moving dash along a cable shows the signal flowing from output to input, and the dash moves faster when the signal is louder.

**Long-press (or right-click) a jack** to open its options:

- remove its cables;
- set a cable's level, or invert it;
- choose a cable colour;
- start a patch from that jack;
- see where it is normalled.

An input accepts one cable. An output can feed any number of inputs. Up to 128 cables can be connected at once. Feedback loops are allowed and run with one sample of delay.
""")

ch("normals", "Normalled connections", ["patch"], """
The pre-patched voice uses *normals*: default connections that work without cables, as on many semi-modular synthesisers. A cable plugged into an input breaks that input's normal, so the cable replaces the default connection rather than adding to it.

Turn on *Show default connections* in Settings → Cables to see normals as faint lines on the Patch screen. Long-press an input to see what it is normalled from.
""")

ch("patch-canvas", "The patch canvas", ["patch"], """
The Patch screen shows every module as a movable tile. Drag a tile to rearrange the canvas, pinch to zoom from 10 % to 500 %, and drag the background to pan. The layout is saved with the patch, but it is only visual and never changes the sound.

Drag from a port to patch, in either direction, just as in the rack. Long-press a port for the same options as on a rack jack. Cable opacity and thickness are in Settings.
""")

ch("mod-matrix", "The modulation matrix", ["modulation"], """
The matrix has 16 slots. Each slot connects a **source** (an envelope, LFO, velocity, key, aftertouch, mod wheel, MPE expression, macro, sequencer row and more) to a **destination** (most continuous parameters). A second source can scale the slot's depth. For example, the mod wheel can scale the depth of LFO vibrato.

Each slot also has an amount, an offset, a curve, a polarity setting (unipolar or bipolar), invert and slew. A slot's live value is shown while you play. Destinations are modulated per voice, so every note can move differently.
""" + table("matrix"))

ch("voices", "Voices, polyphony and unison", ["performance", "settings"], """
**Polyphony** can be set from 1 to 32 voices. **Voice Mode** offers:

- **Poly.**
- **Mono** (retriggers).
- **Legato** (no retrigger while notes overlap).
- **Unison** (2 to 8 stacked voices with detune and stereo spread).
- **Duo** (two-note paraphony).

Note Priority applies to the mono modes. **Glide** (portamento) can be always on or only when playing legato, and can use a constant rate or a constant time.

On the rack, the **keyboard ctrl** panel shows only the Glide fader. Its **settings ▸** button opens a pop-up window with Voice Mode, Note Priority, Glide Mode, Unison Voices, Polyphony, pitch-bend ranges and Unison Detune. Tap *done*, or outside the window, to close it.

**Stereo Spread** pans voices across the stereo field. When more notes arrive than there are voices, the oldest released voice is stolen first, with a short fade so the change doesn't click.
""" + table("global"))

ch("instability", "Analogue instability and engine modes", ["settings", "performance"], """
**Analogue Instability** adds the small imperfections of real hardware:

- slow pitch drift (an Ornstein–Uhlenbeck process with a two-second time constant);
- per-voice tuning tolerance;
- thermal detune;
- envelope and filter tolerances.

At 0 % the instrument is perfectly clean and repeatable.

**Engine Mode**: *Authentic* scales the imperfections to vintage levels and adds exponential-converter droop at high pitches. *Modern* keeps them subtle.
""")

ch("sequencer", "The step sequencer", ["sequencer"], """
The sequencer has 16 steps and three rows (A, B and C). Each step also has a gate, a probability and a ratchet setting (1–8 repeats).

- **Row A** plays notes, quantised to a scale and root.
- **Row B** can control velocity.
- **Row C** is a free modulation row.

You can set the direction, rate, swing and gate length. A seed makes the random directions repeatable.

The sequencer runs from the internal tempo, the host or MIDI clock. When *Plays Notes* is off, the rows act only as modulation sources for the matrix and the arpeggiator.
""" + table("seq"))

ch("arpeggiator", "The arpeggiator", ["arpeggiator"], """
The arpeggiator has 14 patterns, including converge, diverge, thumb, pinky, chord and a random walk. It spans 1–4 octaves in three octave directions, at straight, dotted or triplet rates from 1/1 to 1/32, with gate and swing. A Euclidean rhythm mode (steps, pulses, rotate) can replace the straight rhythm.

Probability and ratchets add variation, and the same seed always gives the same variation. Velocity can be as played, fixed or accented. **Pattern Chain** plays up to four patterns in turn. **Latch** keeps the arpeggio running after you lift your hands. Turn on *Transpose by Sequencer* to let the step sequencer's row A move the arpeggio.
""" + table("arp"))

ch("generative", "The generative engine", ["generative"], """
The generator writes musical phrases as you listen, in five styles: melody, bassline, chords, ambient and broken chords. It works within any of 16 scales, a root, a range and a register.

- **Density** sets how many notes play.
- **Complexity** sets how adventurous the rhythm and the intervals are.
- **Evolve** lets the phrase change a little on each repeat.
- **Humanise** loosens the timing and velocity.

*Follow Keys* can transpose the phrase from the keyboard, or play it only while keys are held. The seed makes the output repeatable. Use *Capture* on the Sequencer screen to keep a phrase as a note clip (chapter 25).
""" + table("gen"))

ch("clips", "Note clips and MIDI files", ["sequencer", "generative"], """
Whatever the sequencer, arpeggiator, generator or keyboard plays can be captured as a **note clip**. A project holds up to 16 clips. Clips are saved with the project and the host state.

To use a clip in a DAW, either export it as a Standard MIDI File (*Export MIDI* on the Sequencer screen), or turn on MIDI Out (chapter 29) and record the plug-in's MIDI output in the host.
""")

ch("effects", "Effects slots", ["effects"], """
There are eight effect slots. Each can hold any of 27 effect types:

- **Delays:** stereo, ping-pong, tape echo and BBD.
- **Reverbs:** plate, hall, room, spring and convolution.
- **Equalisers:** parametric and graphic.
- **Spectral and granular** processors.
- **Saturation and character:** tape, stutter and lo-fi.
- **Amp and cabinet.**
- **Modulation:** chorus, flanger, phaser, ensemble and tremolo/pan.
- **Dynamics:** compressor, limiter and gate.
- **Stereo width.**

Every slot has an on/off switch, mix, output level and up to 12 settings, all automatable.

Changing a slot's type crossfades to the new effect with no click. Latency from look-ahead effects is reported to the host and compensated across every path.
""" + table("fx1"))

ch("effects-bus", "Effects routing: inserts, sends and returns", ["effects"], """
Each slot is assigned to one of three buses: the **insert** chain, **Send A** or **Send B**. Each bus runs its slots in series or in parallel. The sends tap the signal before or after the inserts, and each return has its own level. This lets one reverb be shared by several sounds, or a delay feed a reverb.

**Effects On** bypasses the whole section.
""" + table("fxBus"))

ch("impulse-responses", "Impulse responses", ["effects"], """
The convolution reverb loads your own impulse response files (WAV, AIFF or CAF, up to 10 seconds). Files are copied into the app's shared library and referred to by content hash, so a project opens with the right IR on any of your devices that has the file. If the file is missing, the slot stays silent and says so. It never loads a different file instead.
""")

ch("midi-input", "MIDI input, MPE and key zones", ["midi"], """
Vektur Modular receives MIDI 1.0 and MIDI 2.0. Under MIDI 2.0, velocity and controllers keep their full resolution. 14-bit CC pairs, RPN and NRPN are supported.

- **Receive Channel** can be omni or a single channel.
- **Velocity Curve** can be linear, soft, hard or fixed.
- **MPE** supports a lower or an upper zone, with per-note pitch bend, pressure and slide (CC 74) on member channels and a configurable bend range.
- **Key Zones** can split or layer the keyboard, with a transpose for each zone.
- **MIDI Out** can send the generated notes (from the arpeggiator, sequencer and generator) or all notes, on a chosen channel, plus MIDI clock if you want it.
""")

ch("midi-learn", "MIDI Learn and mappings", ["midi"], """
To map a controller, turn on **Learn**, touch a control, then move a knob or fader on your MIDI controller. Mappings can be absolute, toggle, or relative (three encoder formats), each with a range, pickup and sensitivity.

There are two scopes:

- **Global** mappings belong to your controller and apply to every project.
- **Preset** mappings are saved with the sound.

Export and import mapping profiles (.vekturmidi) to move a controller set-up between devices.

Learn ignores the sustain pedal (CC 64). It also ignores MPE per-note expression, so pressing a key harder never re-maps a knob.
""")

ch("midi-reference", "The MIDI CC and automation reference", ["midi"], """
The **MIDI Reference** screen, under MIDI, lists every automatable parameter. For each one it shows the display name, the stable identifier, the address, the range and the controllers currently mapped to it. You can search the list. Export it as CSV for a spreadsheet, or as Markdown for printing.

Addresses never change between versions, so host automation recorded today keeps working after an update.
""")

ch("sync", "Tempo and sync", ["sequencer", "settings"], """
**Sync Source**:

- **Auto** follows the host when running as a plug-in, and incoming MIDI clock when it is present.
- **Internal** uses the instrument's own tempo, from 20 to 300 BPM.
- **MIDI Clock** follows an external clock, with start, stop and song position.

The standalone app can join an **Ableton Link** session, once the Link library is included in the build. The transport panel shows the tempo, the play state and the time signature. The sequencer, arpeggiator, generator, tempo-synced LFOs and tempo-synced delays all follow the same clock.
""")

ch("presets", "Presets", ["presets"], """
There are **64 factory presets** in two banks of 32:

- **Classic** covers basses, leads, pads, keys, brass, strings, plucks and effects.
- **Modular & Generative** covers patched sounds that use cables, the sequencer, the arpeggiator and the generator.

Every factory preset is checked automatically: its level is gain-staged to the same loudness, and no two presets sound alike.

**User presets** are saved with *Save*. They also appear in your host's preset menu. Search by name, category or tag. Star a preset to make it a favourite, and long-press it to edit its tags. The six most recent presets are listed at the top. Tags and favourites are saved on the device; the presets themselves are not affected if that data is lost.

The **preset creator** generates a new sound in a chosen category from a seed. The same seed always makes the same sound.

**Program Change**:

- **bank MSB 0** selects the Classic bank, programs 1–32;
- **bank MSB 1** selects the Modular & Generative bank;
- **bank MSB 2** selects user presets in name order.

Turn on *Respond to Program Change* first.
""")

ch("import-export", "Import and export", ["presets", "sequencer"], """
Everything can be exported as a file and opened on another device, or by a later version:

- **Preset** (.vekturpreset). One sound: parameters, cables and the preset's MIDI map. Your setup settings, such as the MIDI channel and zones, are not included.
- **Project** (.vekturproject). Everything: the sound, clips, appearance and layout.
- **Pattern** (.vekturpattern). Only the step sequencer and arpeggiator settings. Importing a pattern changes nothing else.
- **MIDI clip** (.mid). A Standard MIDI File, from the Sequencer screen.
- **MIDI profile** (.vekturmidi). Controller mappings.
- **MIDI reference** (.csv or .md). The parameter list.

All the Vektur formats are versioned JSON. A file written by a newer version is refused with a clear message, so it is never misread.
""")

ch("recording", "Recording", ["mixer"], """
The recorder on the Mixer screen captures the instrument's output, including effects, to a file. You can choose:

- **Format:** WAV, AIFF or CAF.
- **Bit depth:** 16-bit, 24-bit or 32-bit float.
- **Sample rate:** the session rate, or converted to 44.1, 48, 88.2 or 96 kHz.

Recordings are saved in *Recordings* in the app's Documents folder, which you can see in the Files app on iPhone and iPad. Share them with the share button.

The audio thread only copies samples into an 11-second buffer, and a background task writes the file. This means recording never causes dropouts. If the device ever falls that far behind, the recorder counts the frames it had to skip and shows the count, rather than interrupting the audio.

Keyboard shortcut: ⇧⌘R starts and stops recording.
""")

ch("appearance", "Appearance and panels", ["settings", "rack"], """
The panels imitate black painted steel with white legends, coloured knob caps, toggle switches and rack rails. **Wear** ages the instrument, from factory new to a well-used studio unit, adding scratches, edge rub, dust and yellowed legends. Every module's wear pattern is unique but stays the same every time you open it. A single module can have its own wear setting. Legends always stay readable.

**Tilt-reactive highlights** (iPhone and iPad) move the light on the metal parts as you tilt the device. Battery Saving Mode turns them off. Appearance is saved with the project.
""")

ch("performance-settings", "Performance, battery and app settings", ["settings", "diagnostics"], """
- **Oscillator Quality.** *Standard*, or *HQ*, which runs the oscillators at 2× (chapter 6).
- **Filter Oversampling.** 1×, 2× or 4×.
- **Battery Saving Mode.** Lowers the screen refresh rate of the meters and the cable animation to 15 Hz, stops the cable motion and tilt highlights, and uses Standard oscillators with 1× filters while it is on. Your saved settings are restored when you turn it off.
- **Keep Screen Awake** (standalone app only). Stops the screen from locking while you play.
- **Background Audio** (standalone app only). Keeps the instrument running when you switch apps, so other apps can play it over MIDI. When this is off, audio stops in the background to save battery.

**Diagnostics** shows CPU load, voices, dropped events and audio problems the engine has recovered from. Measured performance figures for each device are listed in the performance notes that come with the app.
""")

ch("accessibility-privacy", "Accessibility, privacy and troubleshooting", ["settings", "diagnostics"], """
**Accessibility.**

- Every control has a VoiceOver label, its current value and adjustable actions (swipe up or down).
- Jacks announce their module and port, and the patching options are available as VoiceOver actions.
- Text follows Dynamic Type where the layout allows.
- Colours are never the only way information is shown.
- Reduce Motion turns off cable sway and the flow animation.
- Every function can be reached with a keyboard on iPad and Mac.

**Privacy.** Vektur Modular collects no data. It has no accounts, no analytics and no advertising, and it does not track you. It uses the network only for Ableton Link on your local network. Files you import stay on your device. The app's privacy manifest declares the only system APIs it uses: preferences (UserDefaults) and the system uptime used for timing.

**Troubleshooting.**

- *No sound:* check the master volume, the MIDI channel and Diagnostics.
- *Crackles:* raise the buffer size, turn off HQ oscillators or 4× filter oversampling, or turn on Battery Saving.
- *A host can't find the plug-in:* open the standalone app once after installing, then rescan in the host.
""")

assert len(C) == 38, len(C)


def slugfile(i, slug):
    return f"{i + 1:02d}-{slug}.md"


def main():
    docs = os.path.join(ROOT, "docs", "manual")
    res = os.path.join(ROOT, "Packages", "VekturKit", "Sources", "VekturCore", "Resources", "Manual")
    for d in (docs, res):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    index = []
    for i, (slug, title, screens, body) in enumerate(C):
        md = f"# {i + 1}. {title}\n\n{body}"
        name = slugfile(i, slug)
        for d in (docs, res):
            open(os.path.join(d, name), "w").write(md)
        index.append({"number": i + 1, "slug": slug, "title": title, "screens": screens, "file": name})
    json.dump({"chapters": index}, open(os.path.join(res, "index.json"), "w"), indent=1)
    toc = "# Vektur Modular — Manual\n\n" + "\n".join(f"{c['number']}. [{c['title']}]({c['file']})" for c in index) + "\n"
    open(os.path.join(docs, "README.md"), "w").write(toc)
    web = [{"n": c["number"], "slug": c["slug"], "title": c["title"], "screens": c["screens"],
            "md": open(os.path.join(docs, c["file"])).read()} for c in index]
    json.dump(web, open(os.path.join(ROOT, "web", "manual.json"), "w"), separators=(",", ":"))
    words = sum(len(re.findall(r"\w+", c[3])) for c in C)
    print(f"manual: {len(C)} chapters, {words} words of prose, tables from {len(PARAMS)} parameters")


if __name__ == "__main__":
    main()
