#!/usr/bin/env python3
"""Single source of truth for every host-visible parameter.

Run `python3 tools/params.py` after editing. It regenerates:
  Packages/VekturKit/Sources/VekturDSP/include/VekturParams.h   (C address constants, Swift-importable)
  Packages/VekturKit/Sources/VekturDSP/ParamTable.hpp           (C++ ranges/defaults/smoothing)
  Packages/VekturKit/Sources/VekturCore/ParameterRegistry+Generated.swift
  web/params.json                                               (browser test build)

RULES — enforced by ParameterRegistryStabilityTests and by checks below:
  * Never change a shipped (module id, index) or identifier. Only append.
  * Module ids follow docs/ARCHITECTURE.md §6.
"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PER_MODULE = 256

WAVES = ["Sine", "Triangle", "Saw", "Reverse Saw", "Square", "Pulse"]
OCTAVES = ["-3", "-2", "-1", "0", "+1", "+2", "+3"]
CURVES = ["Linear", "Exponential", "Logarithmic", "Analogue"]
SYNC_DIVISIONS = ["Off", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32",
                  "1/2 dotted", "1/4 dotted", "1/8 dotted", "1/2 triplet", "1/4 triplet", "1/8 triplet", "1/16 triplet"]
NOTE_NAMES = [f"{['C','C#','D','Eb','E','F','F#','G','Ab','A','Bb','B'][k % 12]}{k // 12 - 1}" for k in range(128)]

# P(index, key, name, unit, min, max, default, smoothing_ms, taper='linear', strings=None, automatable=True)
def P(i, key, name, unit, lo, hi, default, sm=0.0, taper="linear", strings=None, auto=True, mod=None):
    if strings is not None:
        lo, hi = 0, len(strings) - 1
    return dict(i=i, key=key, name=name, unit=unit, lo=float(lo), hi=float(hi), default=float(default),
                sm=float(sm), taper=taper, strings=strings, auto=auto, mod=mod)

def vco(n, level):
    common = [
        P(0, "wave", f"VCO {n} Waveform", "indexed", 0, 0, 2, strings=WAVES),
        P(1, "octave", f"VCO {n} Octave", "indexed", 0, 0, 3, strings=OCTAVES),
        P(2, "coarse", f"VCO {n} Coarse", "semitones", -12, 12, 0),
        P(3, "fine", f"VCO {n} Fine", "cents", -100, 100, 0, 20),
        P(4, "pulseWidth", f"VCO {n} Pulse Width", "percent", 5, 95, 50, 10),
        P(5, "level", f"VCO {n} Level", "percent", 0, 100, level, 20),
        P(6, "lowFreqMode", f"VCO {n} Low Frequency", "boolean", 0, 1, 0),
        P(7, "phaseReset", f"VCO {n} Phase Reset", "boolean", 0, 1, 0),
        P(8, "startPhase", f"VCO {n} Start Phase", "degrees", 0, 360, 0),
    ]
    return common

MODULES = [
    # (id, identifier prefix, group display name, params)
    (0, "global", "Global", [
        P(0, "masterVolume", "Master Volume", "decibels", -60, 6, -6, 20),
        P(1, "masterTune", "Master Tune", "cents", -100, 100, 0, 20),
        P(2, "polyphony", "Polyphony", "indexed", 0, 0, 4,
          strings=["1 voice", "2 voices", "4 voices", "6 voices", "8 voices", "12 voices", "16 voices", "24 voices", "32 voices"]),
        P(3, "pitchBendRange", "Pitch Bend Range", "semitones", 0, 24, 2),
        P(4, "engineMode", "Engine Mode", "indexed", 0, 0, 0, strings=["Authentic", "Modern"]),
        P(5, "instability", "Analogue Instability", "percent", 0, 100, 35, 50),
        P(6, "glide", "Glide Time", "milliseconds", 0, 5000, 0, 20),
        P(7, "stereoSpread", "Stereo Spread", "percent", 0, 100, 40, 50),
        P(8, "filterRouting", "Filter Routing", "indexed", 0, 0, 1, strings=["Filter A", "Filter B", "A → B", "A + B"]),
        P(9, "oversampling", "Filter Oversampling", "indexed", 0, 0, 1, strings=["1×", "2×", "4×"], auto=False),
        # Phase 4A — keyboard & voicing (indices 10–16)
        P(10, "voiceMode", "Voice Mode", "indexed", 0, 0, 0, strings=["Poly", "Mono", "Legato", "Unison", "Duo"]),   # append only
        P(11, "notePriority", "Note Priority", "indexed", 0, 0, 0, strings=["Last", "Low", "High"]),
        P(12, "glideMode", "Glide Mode", "indexed", 0, 0, 0, strings=["Always", "Legato Only"]),
        P(13, "unisonVoices", "Unison Voices", "indexed", 0, 0, 2, strings=[f"{k} voices" for k in range(2, 9)]),
        P(14, "unisonDetune", "Unison Detune", "cents", 0, 50, 12, 20, mod=False),
        P(15, "bendDown", "Pitch Bend Down Range", "semitones", -1, 24, -1, mod=False),   # -1 = same as up
        P(16, "glideConstantTime", "Glide Constant Time", "boolean", 0, 1, 1),
        # Phase 4B — tempo, MPE, key zones (indices 17–24)
        P(17, "tempo", "Internal Tempo", "generic", 20, 300, 120, 0),   # used when the host provides none
        P(18, "mpeMode", "MPE", "indexed", 0, 0, 0, strings=["Off", "Lower Zone", "Upper Zone"]),
        P(19, "mpeMemberChannels", "MPE Member Channels", "indexed", 0, 0, 14, strings=[str(k) for k in range(1, 16)]),
        P(20, "mpePitchRange", "MPE Per-Note Bend Range", "semitones", 0, 96, 48),
        P(21, "zoneMode", "Key Zones", "indexed", 0, 0, 0, strings=["Off", "Split", "Layer"]),
        P(22, "splitPoint", "Split Point", "indexed", 0, 0, 60, strings=NOTE_NAMES),
        P(23, "zoneATranspose", "Zone A Transpose", "semitones", -24, 24, 0),
        P(24, "zoneBTranspose", "Zone B Transpose", "semitones", -24, 24, 0),
        # Phase 7 — MIDI engine (indices 25–31)
        P(25, "syncSource", "Sync Source", "indexed", 0, 0, 0, strings=["Auto", "Internal", "MIDI Clock"], auto=False),
        P(26, "receiveChannel", "MIDI Receive Channel", "indexed", 0, 0, 0, strings=["Omni"] + [str(k) for k in range(1, 17)], auto=False),
        P(27, "velocityCurve", "Velocity Curve", "indexed", 0, 0, 0, strings=["Linear", "Soft", "Hard", "Fixed"]),
        P(28, "programChange", "Respond to Program Change", "boolean", 0, 1, 1, auto=False),
        P(29, "midiOut", "MIDI Out", "indexed", 0, 0, 0, strings=["Off", "Generated Notes", "All Notes"], auto=False),
        P(30, "midiOutChannel", "MIDI Out Channel", "indexed", 0, 0, 0, strings=[str(k) for k in range(1, 17)], auto=False),
        P(31, "midiClockOut", "Send MIDI Clock", "boolean", 0, 1, 0, auto=False),
        # Phase 10
        P(32, "oscillatorQuality", "Oscillator Quality", "indexed", 0, 0, 0, strings=["Standard", "HQ (2× oversampled)"], auto=False),
    ]),
    (1, "vco1", "VCO 1", vco(1, 100) + [
        P(9, "subLevel", "VCO 1 Sub Level", "percent", 0, 100, 0, 20),
        P(10, "subOctave", "VCO 1 Sub Octave", "indexed", 0, 0, 0, strings=["-1 oct", "-2 oct"]),
        # Phase 3: inputs that only matter when patched
        P(11, "fmAmount", "VCO 1 FM Amount", "percent", 0, 100, 50, 20),
        P(12, "fmMode", "VCO 1 FM Mode", "indexed", 0, 0, 0, strings=["Exponential", "Linear"]),
        P(13, "syncMode", "VCO 1 Sync", "indexed", 0, 0, 1, strings=["Off", "Hard", "Soft"]),
    ]),
    (2, "vco2", "VCO 2", vco(2, 0) + [
        P(9, "syncMode", "VCO 2 Sync", "indexed", 0, 0, 0, strings=["Off", "Hard", "Soft"]),
        P(10, "fmAmount", "VCO 2 FM from VCO 1", "percent", 0, 100, 0, 20),
        P(11, "fmMode", "VCO 2 FM Mode", "indexed", 0, 0, 0, strings=["Exponential", "Linear"]),
    ]),
    (3, "vco3", "VCO 3", vco(3, 0) + [
        P(9, "pitchModAmount", "VCO 3 → VCO 1+2 Pitch", "semitones", -24, 24, 0, 20),
        P(10, "fmAmount", "VCO 3 FM Amount", "percent", 0, 100, 50, 20),
        P(11, "fmMode", "VCO 3 FM Mode", "indexed", 0, 0, 0, strings=["Exponential", "Linear"]),
        P(12, "syncMode", "VCO 3 Sync", "indexed", 0, 0, 1, strings=["Off", "Hard", "Soft"]),
    ]),
] + [
    (3 + b, f"vco{3 + b}", f"VCO {3 + b} (bank)", vco(3 + b, 100) + [
        P(9, "fmAmount", f"VCO {3 + b} FM Amount", "percent", 0, 100, 50, 20),
        P(10, "fmMode", f"VCO {3 + b} FM Mode", "indexed", 0, 0, 0, strings=["Exponential", "Linear"]),
        P(11, "syncMode", f"VCO {3 + b} Sync", "indexed", 0, 0, 1, strings=["Off", "Hard", "Soft"]),
    ]) for b in range(1, 7)
] + [
    (10, "vcfA", "Filter A (multimode)", [
        P(0, "mode", "Filter A Mode", "indexed", 0, 0, 0, strings=["Low-pass", "Band-pass", "High-pass", "Notch"]),
        P(1, "cutoff", "Filter A Cutoff", "hertz", 20, 20000, 2000, 10, taper="log"),
        P(2, "resonance", "Filter A Resonance", "percent", 0, 100, 0, 10),
        P(3, "drive", "Filter A Drive", "decibels", 0, 24, 0, 20),
        P(4, "envAmount", "Filter A EG1 Amount", "octaves", -8, 8, 0, 10),
        P(5, "keyTrack", "Filter A Key Track", "percent", 0, 100, 50, 10),
        P(6, "shAmount", "Filter A S&H Amount", "octaves", -4, 4, 0, 10),
        P(7, "vco3Amount", "Filter A VCO 3 Amount", "octaves", -4, 4, 0, 10),
    ]),
    (11, "vcfB", "Filter B (4-pole ladder)", [
        P(0, "cutoff", "Filter B Cutoff", "hertz", 20, 20000, 1200, 10, taper="log"),
        P(1, "resonance", "Filter B Resonance", "percent", 0, 100, 20, 10),
        P(2, "drive", "Filter B Drive", "decibels", 0, 24, 0, 20),
        P(3, "envAmount", "Filter B EG1 Amount", "octaves", -8, 8, 3, 10),
        P(4, "keyTrack", "Filter B Key Track", "percent", 0, 100, 50, 10),
        P(5, "shAmount", "Filter B S&H Amount", "octaves", -4, 4, 0, 10),
        P(6, "vco3Amount", "Filter B VCO 3 Amount", "octaves", -4, 4, 0, 10),
    ]),
    (18, "vca", "VCA", [
        P(0, "response", "VCA Response", "indexed", 0, 0, 1, strings=["Linear", "Exponential"]),
        P(1, "saturation", "VCA Saturation", "percent", 0, 100, 0, 20),
        P(2, "velocity", "VCA Velocity", "percent", 0, 100, 50, 20),
        P(3, "initialGain", "VCA Initial Gain", "percent", 0, 100, 0, 20),
    ]),
    (19, "vca2", "VCA 2", [
        P(0, "response", "VCA 2 Response", "indexed", 0, 0, 0, strings=["Linear", "Exponential"]),
        P(1, "saturation", "VCA 2 Saturation", "percent", 0, 100, 0, 20),
        P(2, "initialGain", "VCA 2 Initial Gain", "percent", 0, 100, 0, 20),
        P(3, "cvAmount", "VCA 2 CV Amount", "percent", 0, 100, 100, 20),
    ]),
    (13, "ota", "OTA Multimode Filter", [
        P(0, "mode", "OTA Mode", "indexed", 0, 0, 0, strings=["Low-pass", "Band-pass", "High-pass"]),
        P(1, "cutoff", "OTA Cutoff", "hertz", 20, 20000, 1000, 10, taper="log"),
        P(2, "resonance", "OTA Resonance", "percent", 0, 100, 30, 10),
        P(3, "drive", "OTA Drive", "decibels", 0, 24, 6, 20),
        P(4, "cv1Amount", "OTA CV 1 Amount", "octaves", -10, 10, 5, 10),
        P(5, "cv2Amount", "OTA CV 2 Amount", "octaves", -10, 10, 0, 10),
    ]),
    (14, "comb", "Comb Filter", [
        P(0, "frequency", "Comb Frequency", "hertz", 20, 5000, 220, 10, taper="log"),
        P(1, "feedback", "Comb Feedback", "percent", -99, 99, 70, 10),
        P(2, "damping", "Comb Damping", "percent", 0, 100, 20, 10),
        P(3, "mix", "Comb Mix", "percent", 0, 100, 100, 10),
        P(4, "cvAmount", "Comb CV Amount", "octaves", -10, 10, 5, 10),
    ]),
    (15, "formant", "Formant Filter", [
        P(0, "vowel", "Formant Vowel", "percent", 0, 100, 0, 20),
        P(1, "resonance", "Formant Resonance", "percent", 0, 100, 60, 20),
        P(2, "shift", "Formant Shift", "semitones", -12, 12, 0, 20),
        P(3, "mix", "Formant Mix", "percent", 0, 100, 100, 20),
        P(4, "cvAmount", "Formant CV Amount", "percent", -100, 100, 100, 20),
    ]),
    (16, "filterBank", "Fixed Filter Bank", [
        P(i, f"band{i + 1}", f"Bank {hz}", "percent", 0, 100, 100, 20) for i, hz in enumerate(
            ["125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "12 kHz"])
    ] + [P(8, "level", "Bank Level", "percent", 0, 200, 100, 20)]),
    (17, "vcbpf", "VC Band-pass Filter", [
        P(0, "centre", "VCBPF Centre", "hertz", 20, 20000, 1000, 10, taper="log"),
        P(1, "q", "VCBPF Q", "generic", 0.5, 40, 4, 10, taper="log"),
        P(2, "cv1Amount", "VCBPF CV 1 Amount", "octaves", -10, 10, 5, 10),
        P(3, "cv2Amount", "VCBPF CV 2 Amount", "octaves", -10, 10, 0, 10),
        P(4, "level", "VCBPF Level", "percent", 0, 200, 100, 20),
    ]),
]

def eg(n, a, d, s, r, vel, extra):
    return [
        P(0, "attack", f"EG {n} Attack", "milliseconds", 0.5, 20000, a, 0, taper="log"),
        P(1, "decay", f"EG {n} Decay", "milliseconds", 0.5, 20000, d, 0, taper="log"),
        P(2, "sustain", f"EG {n} Sustain", "percent", 0, 100, s, 10),
        P(3, "release", f"EG {n} Release", "milliseconds", 0.5, 20000, r, 0, taper="log"),
        P(4, "curve", f"EG {n} Curve", "indexed", 0, 0, 3, strings=CURVES),
        P(5, "velocity", f"EG {n} Velocity", "percent", 0, 100, vel, 0),
        P(6, "retrigger", f"EG {n} Retrigger", "indexed", 0, 0, 0, strings=["From current", "From zero"]),
        P(7, "loop", f"EG {n} Loop", "boolean", 0, 1, 0),
    ] + extra

MODULES += [
    (20, "eg1", "EG 1 (filter)", eg(1, 5, 400, 30, 300, 0, [
        P(8, "pitchAmount", "EG 1 → Pitch", "semitones", -24, 24, 0, 0)])),
    (21, "eg2", "EG 2 (amplifier)", eg(2, 3, 300, 100, 250, 0, [])),
    (30, "noise", "Noise", [
        P(0, "type", "Noise Type", "indexed", 0, 0, 0, strings=["White", "Pink"]),
        P(1, "colour", "Noise Colour", "percent", -100, 100, 0, 20),
        P(2, "level", "Noise Level", "percent", 0, 100, 0, 20),
    ]),
    (31, "ringMod", "Ring Modulator", [
        P(0, "level", "Ring Mod Level", "percent", 0, 100, 0, 20),
        P(1, "carrierLeak", "Ring Mod Carrier Leak", "percent", 0, 20, 0, 20),
    ]),
    (33, "slew", "Slew Limiter", [
        P(0, "rise", "Slew Rise", "milliseconds", 0, 10000, 100, 0),
        P(1, "fall", "Slew Fall", "milliseconds", 0, 10000, 100, 0),
        P(2, "mode", "Slew Mode", "indexed", 0, 0, 0, strings=["Exponential", "Linear"]),
    ]),
    (34, "follower", "Envelope Follower", [
        P(0, "attack", "Follower Attack", "milliseconds", 0.1, 1000, 5, 0, taper="log"),
        P(1, "release", "Follower Release", "milliseconds", 1, 5000, 100, 0, taper="log"),
        P(2, "detector", "Follower Detector", "indexed", 0, 0, 0, strings=["Peak", "RMS"]),
        P(3, "threshold", "Follower Threshold", "percent", 0, 100, 0, 20),
        P(4, "gain", "Follower Gain", "percent", 0, 400, 100, 20),
    ]),
    (35, "vproc", "Voltage Processor", [
        P(0, "gain1", "VP Input 1 Gain", "percent", -200, 200, 100, 20),
        P(1, "gain2", "VP Input 2 Gain", "percent", -200, 200, 100, 20),
        P(2, "offset", "VP Offset", "generic", -2, 2, 0, 20),
        P(3, "invert", "VP Invert", "boolean", 0, 1, 0),
        P(4, "limit", "VP Limit", "percent", 1, 200, 200, 20),
    ]),
    (12, "diodeLadder", "Diode Ladder Filter", [
        P(0, "cutoff", "Diode Cutoff", "hertz", 20, 20000, 800, 10, taper="log"),
        P(1, "resonance", "Diode Resonance", "percent", 0, 100, 40, 10),
        P(2, "drive", "Diode Drive", "decibels", 0, 24, 3, 20),
        P(3, "cv1Amount", "Diode CV 1 Amount", "octaves", -10, 10, 5, 10),
        P(4, "cv2Amount", "Diode CV 2 Amount", "octaves", -10, 10, 0, 10),
    ]),
    (59, "transistorLadder", "Transistor Ladder Filter", [
        P(0, "cutoff", "Transistor Cutoff", "hertz", 20, 20000, 800, 10, taper="log"),
        P(1, "resonance", "Transistor Resonance", "percent", 0, 100, 40, 10),
        P(2, "drive", "Transistor Drive", "decibels", 0, 24, 6, 20),
        P(3, "cv1Amount", "Transistor CV 1 Amount", "octaves", -10, 10, 5, 10),
        P(4, "cv2Amount", "Transistor CV 2 Amount", "octaves", -10, 10, 0, 10),
    ]),
    (56, "mixer4", "Mixer (4-channel)", [
        P(i, f"gain{i + 1}", f"Mixer Ch {i + 1}", "percent", -100, 100, 100, 20) for i in range(4)
    ] + [P(4, "level", "Mixer Level", "percent", 0, 200, 100, 20)]),
    (57, "switch", "Analogue Switch", [
        P(0, "mode", "Switch Mode", "indexed", 0, 0, 0, strings=["Manual", "CV select", "Sequential"]),
        P(1, "position", "Switch Position", "indexed", 0, 0, 0, strings=["1", "2", "3", "4"]),
        P(2, "steps", "Switch Steps", "indexed", 0, 0, 2, strings=["2", "3", "4"]),
    ]),
    (58, "gateDelay", "Gate Delay", [
        P(0, "delay", "Gate Delay Time", "milliseconds", 0, 2000, 100, 0),
        P(1, "length", "Gate Length", "milliseconds", 0, 2000, 0, 0),
    ]),
    (32, "sampleHold", "Sample & Hold", [
        P(0, "rate", "S&H Rate", "hertz", 0.05, 50, 4, 20, taper="log"),
        P(1, "slew", "S&H Slew", "milliseconds", 0, 1000, 0, 20),
        P(2, "source", "S&H Source", "indexed", 0, 0, 0, strings=["White noise", "Pink noise"]),
    ]),
]

# ---------------------------------------------------------------------------
# Phase 4A — modulation & performance. Source ids are permanent (append only);
# they are mirrored in modules/ModMatrix.hpp and VekturCore/ModMatrix.swift.
MOD_SOURCES = ["None", "LFO 1", "LFO 2", "LFO 3", "EG 1", "EG 2", "Velocity", "Key Track",
               "Mod Wheel", "Aftertouch", "Poly Aftertouch", "Pitch Bend", "Breath", "Expression",
               "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Random (per note)", "Voice Spread", "Constant",
               # Phase 4B — appended
               "MPE Timbre", "Key Zone",
               # Phase 5 — appended (analogue sequencer rows, global, stepped)
               "Seq Row A", "Seq Row B", "Seq Row C"]
LFO_SHAPES = ["Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random Step", "Random Smooth"]
MOD_SLOTS = 16
VIRTUAL_DESTS = {"pitch": 16384, "amp": 16385, "pan": 16386, "cutoffAll": 16387}
DEST_MAX = 16447   # 0 = none; 1…16383 = parameter address; 16384+ = virtual destinations

def lfo(n, rate):
    return [
        P(0, "shape", f"LFO {n} Shape", "indexed", 0, 0, 1 if n == 1 else 0, strings=LFO_SHAPES),
        P(1, "rate", f"LFO {n} Rate", "hertz", 0.01, 50, rate, 20, taper="log"),
        P(2, "sync", f"LFO {n} Retrigger", "indexed", 0, 0, 0, strings=["Free", "Key"]),
        P(3, "delay", f"LFO {n} Delay", "milliseconds", 0, 5000, 0, 0, mod=False),
        P(4, "phase", f"LFO {n} Phase", "degrees", 0, 360, 0, 0, mod=False),
        P(5, "level", f"LFO {n} Level", "percent", 0, 100, 100, 20),
        # Phase 4B — tempo sync (beats per cycle in modules/ModMatrix.hpp kSyncBeats)
        P(6, "tempoSync", f"LFO {n} Tempo Sync", "indexed", 0, 0, 0, strings=SYNC_DIVISIONS),
    ]

def matrix():
    out = []
    for s in range(MOD_SLOTS):
        b, n = s * 16, s + 1
        out += [
            P(b + 0, f"slot{n}Enable", f"Mod {n} Enable", "boolean", 0, 1, 1),
            P(b + 1, f"slot{n}Source", f"Mod {n} Source", "indexed", 0, 0, 0, strings=MOD_SOURCES, auto=False),
            P(b + 2, f"slot{n}Via", f"Mod {n} Scale By", "indexed", 0, 0, 0, strings=MOD_SOURCES, auto=False),
            P(b + 3, f"slot{n}Dest", f"Mod {n} Destination", "generic", 0, DEST_MAX, 0, auto=False, mod=False),
            P(b + 4, f"slot{n}Amount", f"Mod {n} Amount", "percent", -100, 100, 0, 10, mod=False),
            P(b + 5, f"slot{n}Offset", f"Mod {n} Offset", "percent", -100, 100, 0, 10, mod=False),
            P(b + 6, f"slot{n}Curve", f"Mod {n} Curve", "indexed", 0, 0, 0,
              strings=["Linear", "Exponential", "Logarithmic", "S-Curve"]),
            P(b + 7, f"slot{n}Polarity", f"Mod {n} Polarity", "indexed", 0, 0, 0,
              strings=["As Source", "Unipolar", "Bipolar"]),
            P(b + 8, f"slot{n}Invert", f"Mod {n} Invert", "boolean", 0, 1, 0),
            P(b + 9, f"slot{n}Slew", f"Mod {n} Slew", "milliseconds", 0, 2000, 0, 0, mod=False),
        ]
    return out

MODULES += [
    (26, "lfo1", "LFO 1", lfo(1, 5.0)),
    (27, "lfo2", "LFO 2", lfo(2, 0.5)),
    (28, "lfo3", "LFO 3", lfo(3, 0.1)),
    (29, "macro", "Macros", [P(k, f"macro{k + 1}", f"Macro {k + 1}", "percent", 0, 100, 0, 20, mod=False) for k in range(4)]),
    (60, "matrix", "Modulation Matrix", matrix()),
]

# ---------------------------------------------------------------------------
# Phase 5 — sequencing & generative (modules 36–38, ARCHITECTURE.md §6).
# The lists below are mirrored in VekturDSP/seq/SeqCommon.hpp (append only).
DIVISIONS = ["1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
             "1/2 dotted", "1/4 dotted", "1/8 dotted", "1/16 dotted",
             "1/2 triplet", "1/4 triplet", "1/8 triplet", "1/16 triplet", "1/32 triplet"]
SCALES = ["Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian",
          "Lydian", "Mixolydian", "Locrian", "Major Pentatonic", "Minor Pentatonic", "Blues",
          "Whole Tone", "Hungarian Minor", "Phrygian Dominant"]
PITCH_CLASSES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
ARP_PATTERNS = ["Up", "Down", "Up-Down", "Down-Up", "Up-Down (repeat ends)", "Down-Up (repeat ends)",
                "As Played", "Random", "Random Walk", "Converge", "Diverge", "Thumb", "Pinky", "Chord"]
NUMS = lambda a, b: [str(k) for k in range(a, b + 1)]

def arp():
    return [
        P(0, "mode", "Arp Mode", "indexed", 0, 0, 0, strings=["Off", "On", "Latch"]),
        P(1, "pattern", "Arp Pattern", "indexed", 0, 0, 0, strings=ARP_PATTERNS),
        P(2, "octaves", "Arp Octaves", "indexed", 0, 0, 0, strings=NUMS(1, 4)),
        P(3, "octaveMode", "Arp Octave Direction", "indexed", 0, 0, 0, strings=["Up", "Down", "Up-Down"]),
        P(4, "rate", "Arp Rate", "indexed", 0, 0, 4, strings=DIVISIONS),
        P(5, "gate", "Arp Gate", "percent", 5, 200, 50),
        P(6, "swing", "Arp Swing", "percent", 50, 75, 50),
        P(7, "rhythm", "Arp Rhythm", "indexed", 0, 0, 0, strings=["Straight", "Euclidean"]),
        P(8, "euclidSteps", "Arp Euclid Steps", "indexed", 0, 0, 7, strings=NUMS(1, 32)),
        P(9, "euclidPulses", "Arp Euclid Pulses", "indexed", 0, 0, 5, strings=NUMS(0, 32)),
        P(10, "euclidRotate", "Arp Euclid Rotate", "indexed", 0, 0, 0, strings=NUMS(0, 31)),
        P(11, "probability", "Arp Probability", "percent", 0, 100, 100),
        P(12, "ratchet", "Arp Ratchet", "indexed", 0, 0, 0, strings=[f"{k}×" for k in range(1, 9)]),
        P(13, "ratchetChance", "Arp Ratchet Chance", "percent", 0, 100, 100),
        P(14, "velocityMode", "Arp Velocity", "indexed", 0, 0, 0, strings=["As Played", "Fixed", "Accent"]),
        P(15, "fixedVelocity", "Arp Fixed Velocity", "generic", 1, 127, 100),
        P(16, "restart", "Arp Restart", "indexed", 0, 0, 0, strings=["Auto", "Beat-locked", "Key Restart"]),
        P(17, "seed", "Arp Seed", "generic", 0, 65535, 1, auto=False),
        P(18, "chain", "Arp Pattern Chain", "indexed", 0, 0, 0, strings=["Off", "2 patterns", "3 patterns", "4 patterns"]),
        P(19, "chainPattern2", "Arp Chain Pattern 2", "indexed", 0, 0, 1, strings=ARP_PATTERNS),
        P(20, "chainPattern3", "Arp Chain Pattern 3", "indexed", 0, 0, 2, strings=ARP_PATTERNS),
        P(21, "chainPattern4", "Arp Chain Pattern 4", "indexed", 0, 0, 7, strings=ARP_PATTERNS),
        P(22, "chainPasses", "Arp Chain Passes", "indexed", 0, 0, 0, strings=NUMS(1, 16)),
        P(23, "seqTranspose", "Arp Transpose by Sequencer", "boolean", 0, 1, 0),
    ]

SEQ_STEPS = 16
def sequencer():
    out = [
        P(0, "run", "Seq Run", "indexed", 0, 0, 0, strings=["Off", "Run", "Transport", "Key"]),
        P(1, "length", "Seq Length", "indexed", 0, 0, SEQ_STEPS - 1, strings=NUMS(1, SEQ_STEPS)),
        P(2, "direction", "Seq Direction", "indexed", 0, 0, 0, strings=["Forward", "Reverse", "Ping-Pong", "Random", "Random Walk"]),
        P(3, "rate", "Seq Rate", "indexed", 0, 0, 4, strings=DIVISIONS),
        P(4, "swing", "Seq Swing", "percent", 50, 75, 50),
        P(5, "gate", "Seq Gate", "percent", 5, 200, 50),
        P(6, "pitchRange", "Seq Row A Range", "indexed", 0, 0, 1, strings=["1 octave", "2 octaves", "3 octaves", "5 octaves"]),
        P(7, "scale", "Seq Quantise Scale", "indexed", 0, 0, 0, strings=SCALES),
        P(8, "root", "Seq Root", "indexed", 0, 0, 0, strings=PITCH_CLASSES),
        P(9, "transpose", "Seq Transpose", "semitones", -24, 24, 0),
        P(10, "playNotes", "Seq Plays Notes", "boolean", 0, 1, 1),
        P(11, "rowBVelocity", "Seq Row B → Velocity", "boolean", 0, 1, 0),
        P(12, "seed", "Seq Seed", "generic", 0, 65535, 1, auto=False),
    ]
    for s in range(SEQ_STEPS):
        b, n = 32 + s * 8, s + 1
        out += [
            P(b + 0, f"step{n}A", f"Seq Step {n} Row A", "percent", 0, 100, 0, 5),
            P(b + 1, f"step{n}B", f"Seq Step {n} Row B", "percent", 0, 100, 50, 5),
            P(b + 2, f"step{n}C", f"Seq Step {n} Row C", "percent", 0, 100, 50, 5),
            P(b + 3, f"step{n}Gate", f"Seq Step {n} Gate", "indexed", 0, 0, 1, strings=["Off", "On", "Tie", "Skip"]),
            P(b + 4, f"step{n}Probability", f"Seq Step {n} Probability", "percent", 0, 100, 100),
            P(b + 5, f"step{n}Ratchet", f"Seq Step {n} Ratchet", "indexed", 0, 0, 0, strings=["1×", "2×", "3×", "4×"]),
        ]
    return out

def generative():
    return [
        P(0, "mode", "Generate", "indexed", 0, 0, 0, strings=["Off", "On"]),
        P(1, "style", "Generate Style", "indexed", 0, 0, 0, strings=["Melody", "Bassline", "Chords", "Ambient", "Broken Chords"]),
        P(2, "scale", "Generate Scale", "indexed", 0, 0, 2, strings=SCALES),
        P(3, "root", "Generate Root", "indexed", 0, 0, 9, strings=PITCH_CLASSES),
        P(4, "density", "Generate Density", "percent", 0, 100, 60),
        P(5, "complexity", "Generate Complexity", "percent", 0, 100, 40),
        P(6, "range", "Generate Range", "indexed", 0, 0, 1, strings=["1 octave", "2 octaves", "3 octaves", "4 octaves"]),
        P(7, "register", "Generate Register", "indexed", 0, 0, 60, strings=NOTE_NAMES),
        P(8, "phraseBars", "Generate Phrase Length", "indexed", 0, 0, 2, strings=["1 bar", "2 bars", "4 bars", "8 bars"]),
        P(9, "rate", "Generate Grid", "indexed", 0, 0, 4, strings=DIVISIONS),
        P(10, "seed", "Generate Seed", "generic", 0, 65535, 1, auto=False),
        P(11, "evolve", "Generate Evolve", "percent", 0, 100, 25),
        P(12, "humanise", "Generate Humanise", "percent", 0, 100, 10),
        P(13, "followKeys", "Generate Follow Keys", "indexed", 0, 0, 0, strings=["Off", "Transpose", "Hold to Play"]),
        P(14, "swing", "Generate Swing", "percent", 50, 75, 50),
        P(15, "gate", "Generate Gate", "percent", 5, 200, 85),
    ]

MODULES += [
    (36, "arp", "Arpeggiator", arp()),
    (37, "seq", "Step Sequencer", sequencer()),
    (38, "gen", "Generative Engine", generative()),
]

# ---------------------------------------------------------------------------
# Phase 6 — effects. Eight slots (modules 39–46) and the bus section (47).
# A slot's 12 generic parameters (p1…p12, stored 0…1) take their meaning,
# range and display from the slot's effect type (VekturDSP/fx/FxSpecs.hpp,
# exposed through vk_fx_param_info / vk_fx_format_value). FX_TYPES is
# PERMANENT and mirrors kFxSpecs order (append only).
FX_TYPES = ["Empty",
            "Stereo Delay", "Ping-Pong Delay", "Tape Echo", "BBD Delay",
            "Plate Reverb", "Hall Reverb", "Room Reverb", "Spring Reverb", "Convolution Reverb",
            "Parametric EQ", "Graphic EQ",
            "Spectral", "Granular", "Tape", "Stutter", "Lo-Fi",
            "Amp", "Cabinet",
            "Chorus", "Flanger", "Phaser", "Ensemble", "Tremolo / Pan",
            "Compressor", "Limiter", "Gate", "Stereo Width"]
FX_SLOTS = 8
FX_PARAMS = 12

def fx_slot(n):
    out = [
        P(0, "type", f"FX {n} Type", "indexed", 0, 0, 0, strings=FX_TYPES, auto=False),
        P(1, "enabled", f"FX {n} On", "boolean", 0, 1, 1),
        P(2, "mix", f"FX {n} Mix", "percent", 0, 100, 100, 20),
        P(3, "output", f"FX {n} Output", "decibels", -24, 12, 0, 20),
        P(4, "bus", f"FX {n} Bus", "indexed", 0, 0, 0, strings=["Insert", "Send A", "Send B"], auto=False),
    ]
    out += [P(4 + k, f"p{k}", f"FX {n} Param {k}", "generic", 0, 1, 0.5, 20) for k in range(1, FX_PARAMS + 1)]
    return out

MODULES += [(39 + s, f"fx{s + 1}", f"Effect Slot {s + 1}", fx_slot(s + 1)) for s in range(FX_SLOTS)]
MODULES += [(47, "fxBus", "Effects Buses", [
    P(0, "enabled", "Effects On", "boolean", 0, 1, 1),
    P(1, "insertMode", "Insert Chain", "indexed", 0, 0, 0, strings=["Series", "Parallel"], auto=False),
    P(2, "sendAMode", "Send A Chain", "indexed", 0, 0, 0, strings=["Series", "Parallel"], auto=False),
    P(3, "sendBMode", "Send B Chain", "indexed", 0, 0, 0, strings=["Series", "Parallel"], auto=False),
    P(4, "sendA", "Send A Level", "percent", 0, 100, 30, 20),
    P(5, "sendB", "Send B Level", "percent", 0, 100, 30, 20),
    P(6, "sendAPosition", "Send A Tap", "indexed", 0, 0, 1, strings=["Pre Inserts", "Post Inserts"], auto=False),
    P(7, "sendBPosition", "Send B Tap", "indexed", 0, 0, 1, strings=["Pre Inserts", "Post Inserts"], auto=False),
    P(8, "returnA", "Return A Level", "decibels", -60, 6, 0, 20),
    P(9, "returnB", "Return B Level", "decibels", -60, 6, 0, 20),
    P(10, "dry", "Insert Chain Level", "decibels", -60, 6, 0, 20),
    P(11, "output", "Effects Output", "decibels", -24, 12, 0, 20),
])]

# Addresses that have shipped and must never move (Phase 1).
SHIPPED = {"global.masterVolume": 0, "global.masterTune": 1, "global.polyphony": 2, "global.pitchBendRange": 3}

def build():
    rows = []
    seen_ids, seen_addr = set(), set()
    for mid, prefix, group, params in MODULES:
        assert 0 <= mid < 64
        for p in params:
            assert 0 <= p["i"] < PER_MODULE
            ident = f"{prefix}.{p['key']}"
            addr = mid * PER_MODULE + p["i"]
            assert ident not in seen_ids, ident
            assert addr not in seen_addr, (ident, addr)
            assert p["lo"] < p["hi"] and p["lo"] <= p["default"] <= p["hi"], ident
            if p["taper"] == "log": assert p["lo"] > 0, ident
            seen_ids.add(ident); seen_addr.add(addr)
            if p["mod"] is None:   # default: continuous per-voice parameters are modulatable
                p["mod"] = p["strings"] is None and p["unit"] != "boolean" and mid not in (0, 29, 36, 37, 38, 60) and not (39 <= mid <= 47)
            assert not (p["mod"] and (mid in (0, 36, 37, 38) or 39 <= mid <= 47)), ident   # kernel-level, not per voice
            rows.append(dict(p, mid=mid, prefix=prefix, group=group, ident=ident, addr=addr))
    for ident, addr in SHIPPED.items():
        r = next((r for r in rows if r["ident"] == ident), None)
        assert r and r["addr"] == addr, f"shipped parameter {ident} moved"
    return rows

def cname(r):
    import re
    key = re.sub(r"(?<!^)(?=[A-Z])", "_", r["key"]).upper()
    return f"VK_P_{r['prefix'].upper()}_{key}"

def fnum(x):
    s = repr(float(x))
    return s + "f" if "e" not in s else s + "f"

def write(path, text):
    full = os.path.join(ROOT, path)
    with open(full, "w") as f: f.write(text)
    print("wrote", path)

def main():
    rows = build()
    hdr = ["// GENERATED by tools/params.py — do not edit.", "#ifndef VEKTUR_PARAMS_H", "#define VEKTUR_PARAMS_H", "",
           "// Full parameter addresses (module * 256 + index). Permanent once shipped.", ""]
    group = None
    for r in rows:
        if r["group"] != group:
            group = r["group"]; hdr.append(f"// {group} (module {r['mid']})")
        hdr.append(f"#define {cname(r):40s} {r['addr']}u")
    hdr += ["", f"#define VK_PARAM_COUNT {len(rows)}u", "",
            "// Modulation matrix (Phase 4A)",
            f"#define VK_MOD_SLOTS {MOD_SLOTS}u", f"#define VK_MOD_SOURCE_COUNT {len(MOD_SOURCES)}u",
            f"#define VK_MOD_DEST_MAX {DEST_MAX}u"]
    hdr += [f"#define VK_MOD_DEST_{k.upper()} {v}u" for k, v in VIRTUAL_DESTS.items()]
    hdr += ["", "#endif", ""]
    write("Packages/VekturKit/Sources/VekturDSP/include/VekturParams.h", "\n".join(hdr))

    cpp = ["// GENERATED by tools/params.py — do not edit.", "#pragma once", "#include <cstdint>", "",
           "namespace vektur {", "// flags: bit 0 = log taper, bit 1 = modulatable by the matrix",
           "struct ParamDef { uint64_t address; float minValue, maxValue, defaultValue, smoothingMs; uint32_t flags; };",
           "inline constexpr ParamDef kParamTable[] = {"]
    for r in rows:
        cpp.append(f"    {{{r['addr']}u, {fnum(r['lo'])}, {fnum(r['hi'])}, {fnum(r['default'])}, {fnum(r['sm'])}, {(1 if r['taper'] == 'log' else 0) | (2 if r['mod'] else 0)}u}}, // {r['ident']}")
    cpp += ["};", "} // namespace vektur", ""]
    write("Packages/VekturKit/Sources/VekturDSP/ParamTable.hpp", "\n".join(cpp))

    sw = ["// GENERATED by tools/params.py — do not edit. Edit tools/params.py and re-run.", "import Foundation", "",
          "extension ParameterRegistry {", "    /// Every registered parameter, in tree order.", "    static let generated: [ParameterSpec] = ["]
    for r in rows:
        strings = "nil" if r["strings"] is None else "[" + ", ".join('"' + s + '"' for s in r["strings"]) + "]"
        sw.append(f'        ParameterSpec(identifier: "{r["ident"]}", address: {r["addr"]}, displayName: "{r["name"]}", '
                  f'unit: .{r["unit"]}, minValue: {r["lo"]}, maxValue: {r["hi"]}, defaultValue: {r["default"]}, '
                  f'smoothingMs: {r["sm"]}, valueStrings: {strings}, isAutomatable: {"true" if r["auto"] else "false"}, '
                  f'taper: .{r["taper"]}, group: "{r["prefix"]}", isModulatable: {"true" if r["mod"] else "false"}),')
    sw += ["    ]", "", "    /// Tree groups (identifier, display name), in order.", "    static let generatedGroups: [(String, String)] = ["]
    for mid, prefix, group, _ in MODULES:
        sw.append(f'        ("{prefix}", "{group}"),')
    sw += ["    ]", "}", ""]
    write("Packages/VekturKit/Sources/VekturCore/ParameterRegistry+Generated.swift", "\n".join(sw))

    # Web test build (Phase 8): the same table as JSON for the browser UI.
    import json
    web = {"groups": [[prefix, group] for _, prefix, group, _ in MODULES], "virtualDests": VIRTUAL_DESTS,
           "params": [{"id": r["ident"], "a": r["addr"], "n": r["name"], "u": r["unit"], "lo": r["lo"], "hi": r["hi"],
                       "d": r["default"], "log": r["taper"] == "log", "s": r["strings"], "auto": r["auto"], "g": r["prefix"], "m": bool(r["mod"])}
                      for r in rows]}
    write("web/params.json", json.dumps(web, separators=(",", ":"), ensure_ascii=False) + "\n")
    print(f"{len(rows)} parameters")

if __name__ == "__main__":
    main()
