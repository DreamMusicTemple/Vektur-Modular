#!/usr/bin/env python3
"""Factory presets (Phase 9): 2 banks × 32, authored here as differences
from the default patch, then gain-staged by rendering every preset through
the real kernel (DSPTests/build/preset_audit) and written to:

  Packages/VekturKit/Sources/VekturCore/Resources/FactoryPresets.json
  web/presets.json                          (browser build)
  DSPTests/factory_presets.inc              (C++ audition test)

Each preset: name, bank (0 "Classic", 1 "Modular & Generative"), number in
bank, category (as VKPresetCategory: 0 bass … 7 drone), audition style,
parameters (identifier → value, only non-defaults), cables (module.port →
module.port, gain). Effect parameters are authored in real units and stored
normalised via the effect catalogue (DSPTests/build/fxmap).

    python3 tools/factory_presets.py            # author + audit + write
    python3 tools/factory_presets.py --no-audit # skip the gain staging pass
"""
import json, os, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PARAMS = {p["id"]: p for p in json.load(open(os.path.join(ROOT, "web/params.json")))["params"]}
FX = {n: i for i, n in enumerate(["Empty", "Stereo Delay", "Ping-Pong Delay", "Tape Echo", "BBD Delay", "Plate Reverb", "Hall Reverb",
      "Room Reverb", "Spring Reverb", "Convolution Reverb", "Parametric EQ", "Graphic EQ", "Spectral", "Granular", "Tape", "Stutter",
      "Lo-Fi", "Amp", "Cabinet", "Chorus", "Flanger", "Phaser", "Ensemble", "Tremolo / Pan", "Compressor", "Limiter", "Gate", "Stereo Width"])}
BASS, LEAD, PAD, PLUCK, KEYS, BRASS, FXC, DRONE = range(8)
CAT_NAMES = ["Bass", "Lead", "Pad", "Pluck", "Keys", "Brass", "FX", "Drone"]

def fx(slot, type_name, mix=None, bus=0, **real):
    """real: p<index>=value in the parameter's own units (choices: index)."""
    return {"slot": slot, "type": FX[type_name], "mix": mix, "bus": bus, "real": {int(k[1:]): v for k, v in real.items()}}

def mod(slot, src, dest, amount, via=0, offset=0, curve=0, polarity=0):
    """Matrix slot. src: source index (see params 'matrix.slot1Source'), dest: parameter id or virtual name."""
    vd = {"pitch": 16384, "amp": 16385, "pan": 16386, "cutoffAll": 16387}
    d = vd[dest] if dest in vd else PARAMS[dest]["a"]
    b = f"matrix.slot{slot}"
    return {f"{b}Enable": 1, f"{b}Source": src, f"{b}Via": via, f"{b}Dest": d, f"{b}Amount": amount, f"{b}Offset": offset,
            f"{b}Curve": curve, f"{b}Polarity": polarity}
LFO1, LFO2, LFO3, EG1, EG2, VEL, KEY, MODW, AT, PAT, BEND, BREATH, EXPR, M1, M2, M3, M4, RND, SPREAD, CONST, TIMBRE, ZONE, ROWA, ROWB, ROWC = range(1, 26)

PRESETS = []
def P(bank, name, cat, audition, params, cables=(), fxs=(), mods=()):
    p = dict(params)
    for m in mods: p.update(m)
    PRESETS.append({"bank": bank, "name": name, "category": cat, "audition": audition, "params": p,
                    "cables": [list(c) for c in cables], "fx": list(fxs)})

# Shorthands for common voicings ------------------------------------------------
def two_osc(w1=2, w2=2, detune=7, oct2=3, lvl2=80):
    return {"vco1.wave": w1, "vco2.wave": w2, "vco2.level": lvl2, "vco2.fine": detune, "vco2.octave": oct2}
def env(n, a, d, s, r, **kw):
    return {f"eg{n}.attack": a, f"eg{n}.decay": d, f"eg{n}.sustain": s, f"eg{n}.release": r, **{f"eg{n}.{k}": v for k, v in kw.items()}}
def ladder(cut, res, env_oct, drive=0, key=50):
    return {"global.filterRouting": 1, "vcfB.cutoff": cut, "vcfB.resonance": res, "vcfB.envAmount": env_oct, "vcfB.drive": drive, "vcfB.keyTrack": key}
def multimode(cut, res, env_oct, mode=0, key=50):
    return {"global.filterRouting": 0, "vcfA.mode": mode, "vcfA.cutoff": cut, "vcfA.resonance": res, "vcfA.envAmount": env_oct, "vcfA.keyTrack": key}
MONO = {"global.voiceMode": 1, "global.polyphony": 0}
LEGATO = {"global.voiceMode": 2, "global.polyphony": 0}

# =============================================================================
# Bank A — Classic (subtractive voices on the normalled patch)
# =============================================================================
A = 0
P(A, "Ladder Bass", BASS, "bass", {**MONO, **ladder(260, 45, 3.5, 4), **env(1, 1, 220, 5, 120), **env(2, 1, 400, 80, 90), "vco1.subLevel": 55})
P(A, "Rubber Sub", BASS, "bass", {**MONO, "vco1.wave": 1, "vco1.subLevel": 80, "vco1.subOctave": 1, **ladder(180, 20, 2.2), **env(1, 2, 300, 0, 100), **env(2, 1, 600, 70, 120), "global.glide": 40})
P(A, "Acid Line", BASS, "bass", {**LEGATO, "vco1.wave": 4, **ladder(320, 82, 4.5, 8), **env(1, 1, 160, 0, 80), **env(2, 1, 250, 90, 60), "global.glide": 60, "global.glideMode": 1, "eg1.velocity": 60},
  fxs=[fx(0, "BBD Delay", 18, 0, p1=270, p2=35)])
P(A, "Sync Bass", BASS, "bass", {**MONO, "vco1.wave": 2, "vco2.level": 90, "vco2.syncMode": 1, "vco2.coarse": 9, **ladder(700, 30, 2.5, 3), **env(1, 1, 260, 10, 90), **env(2, 1, 400, 85, 90)},
  mods=[mod(1, EG1, "vco2.coarse", 35)])
P(A, "FM Growl", BASS, "bass", {**MONO, **two_osc(2, 0, 0, 3, 0), "vco1.fmAmount": 60, "vco2.level": 0, **ladder(500, 35, 2.5, 6), **env(2, 1, 500, 80, 120)},
  cables=[("vco2.out", "vco1.fm", 0.6)], mods=[mod(1, LFO1, "vco1.fmAmount", 30)])
P(A, "Deep Reese", BASS, "bass", {**MONO, **two_osc(2, 2, 22, 3, 100), "vco3.level": 70, "vco3.wave": 2, "vco3.fine": -18, "vco3.octave": 2, **ladder(420, 15, 1.2), **env(2, 4, 600, 90, 200)},
  fxs=[fx(0, "Stereo Width", 100, 0, p0=140, p3=150)])
P(A, "Saw Lead", LEAD, "lead", {**LEGATO, **two_osc(2, 2, 9), **ladder(1800, 30, 2.5), **env(1, 3, 600, 40, 300), "global.glide": 70},
  fxs=[fx(0, "Ping-Pong Delay", 22, 0, p1=375, p2=40)], mods=[mod(1, LFO1, "pitch", 6, via=MODW)])
P(A, "Square Whistle", LEAD, "lead", {**LEGATO, "vco1.wave": 4, **multimode(2600, 55, 1.5), **env(2, 25, 200, 90, 250), "global.glide": 50, "lfo1.rate": 5.5, "lfo1.delay": 450},
  mods=[mod(1, LFO1, "pitch", 8)], fxs=[fx(0, "Plate Reverb", 25, 0, p1=1.8)])
P(A, "Screamer", LEAD, "lead", {**LEGATO, **two_osc(4, 2, 5, 4, 70), **ladder(900, 70, 4, 18), **env(1, 2, 900, 55, 300), "global.glide": 30},
  fxs=[fx(0, "Tape", 70), fx(1, "Stereo Delay", 18)])
P(A, "Hard Sync Lead", LEAD, "lead", {**LEGATO, "vco1.level": 0, "vco2.level": 100, "vco2.syncMode": 1, "vco2.coarse": 7, **multimode(5200, 20, 0.5), **env(2, 2, 400, 90, 200), "global.glide": 40},
  mods=[mod(1, EG1, "vco2.coarse", 45), mod(2, MODW, "vco2.coarse", 25)])
P(A, "Thin Pulse Solo", LEAD, "lead", {**LEGATO, "vco1.wave": 5, "vco1.pulseWidth": 18, **ladder(2400, 25, 1.5), **env(2, 8, 300, 85, 260)},
  mods=[mod(1, LFO2, "vco1.pulseWidth", 35)], fxs=[fx(0, "Tape Echo", 20, 0, p1=330, p2=45)])
P(A, "Unison Supersaw", LEAD, "chord", {"global.voiceMode": 3, "global.unisonVoices": 5, "global.unisonDetune": 22, **ladder(3500, 12, 1), **env(2, 10, 300, 90, 350), "global.stereoSpread": 90},
  fxs=[fx(0, "Hall Reverb", 22, 0, p1=2.2)])
P(A, "Warm Strings", PAD, "chord", {**two_osc(2, 2, 6), **ladder(1600, 10, 0.8), **env(1, 900, 800, 70, 1500), **env(2, 700, 500, 90, 1800), "global.stereoSpread": 80},
  fxs=[fx(0, "Ensemble", 70), fx(1, "Hall Reverb", 30, 0, p1=3.5)])
P(A, "Analogue Pad", PAD, "chord", {**two_osc(5, 2, 8), "vco1.pulseWidth": 35, **ladder(800, 25, 2), **env(1, 1500, 1500, 50, 2000), **env(2, 900, 800, 90, 2200), "global.instability": 70},
  mods=[mod(1, LFO1, "vco1.pulseWidth", 25)], fxs=[fx(0, "Chorus", 50), fx(1, "Plate Reverb", 30, 0, p1=3.0)])
P(A, "Glass Choir", PAD, "chord", {**two_osc(1, 0, 4, 4, 60), **multimode(3200, 45, 1), **env(2, 1200, 600, 85, 2600)},
  fxs=[fx(0, "Hall Reverb", 45, 0, p1=5.5)])
P(A, "Sweep Pad", PAD, "chord", {**two_osc(2, 2, 10), **ladder(400, 55, 0), **env(2, 1600, 900, 90, 2500), "lfo1.rate": 0.08, "lfo1.shape": 0},
  mods=[mod(1, LFO1, "vcfB.cutoff", 45)], fxs=[fx(0, "Phaser", 40), fx(1, "Hall Reverb", 35, 0, p1=4.0)])
P(A, "Funk Pluck", PLUCK, "chord", {**ladder(600, 55, 4.5), **env(1, 1, 160, 0, 120), **env(2, 1, 280, 0, 150), "eg1.velocity": 50},
  fxs=[fx(0, "Room Reverb", 18)])
P(A, "Harp Pluck", PLUCK, "chord", {"vco1.wave": 1, **two_osc(1, 0, 0, 4, 35), **multimode(1500, 15, 3.5), **env(1, 1, 400, 0, 400), **env(2, 1, 1400, 0, 1200)},
  fxs=[fx(0, "Plate Reverb", 30, 0, p1=2.4)])
P(A, "Bell Pluck", PLUCK, "chord", {"vco1.level": 0, "ringMod.level": 90, "vco1.wave": 0, "vco2.wave": 0, "vco2.coarse": 7, "vco2.fine": 23, **multimode(9000, 0, 0), **env(2, 1, 1800, 0, 1800)},
  fxs=[fx(0, "Hall Reverb", 30, 0, p1=3.0)])
P(A, "Muted Guitar", PLUCK, "bass", {**MONO, "vco1.wave": 5, "vco1.pulseWidth": 30, **ladder(900, 10, 3), **env(1, 1, 90, 0, 60), **env(2, 1, 140, 0, 80)},
  fxs=[fx(0, "Spring Reverb", 25)])
P(A, "Electric Piano", KEYS, "chord", {"vco1.wave": 0, **two_osc(0, 1, 0, 4, 25), **multimode(4000, 0, 2), **env(1, 1, 700, 20, 400), **env(2, 1, 2200, 20, 500), "eg2.velocity": 60, "vca.velocity": 70},
  mods=[mod(1, VEL, "vcfA.cutoff", 30)], fxs=[fx(0, "Tremolo / Pan", 100, 0, p0=1, p1=4.5), fx(1, "Chorus", 30)])
P(A, "Clavi", KEYS, "chord", {"vco1.wave": 5, "vco1.pulseWidth": 22, **multimode(2400, 25, 2.5), **env(1, 1, 180, 10, 90), **env(2, 1, 400, 20, 90)},
  fxs=[fx(0, "Phaser", 25)])
P(A, "Organ Drawbars", KEYS, "chord", {"vco1.wave": 0, "vco2.wave": 0, "vco2.level": 70, "vco2.octave": 4, "vco3.wave": 0, "vco3.level": 50, "vco3.octave": 4, "vco3.coarse": 7,
  **multimode(8000, 0, 0), **env(2, 3, 100, 100, 40)}, fxs=[fx(0, "Chorus", 40), fx(1, "Amp", 35, 0, p1=25)])
P(A, "Toy Piano", KEYS, "chord", {"vco1.wave": 1, "vco2.wave": 4, "vco2.level": 30, "vco2.octave": 5, **multimode(5000, 30, 1), **env(2, 1, 600, 0, 500)},
  fxs=[fx(0, "Lo-Fi", 40, 0, p0=10), fx(1, "Room Reverb", 25)])
P(A, "Brass Section", BRASS, "chord", {**two_osc(2, 2, 7), **ladder(500, 15, 3), **env(1, 60, 700, 60, 300), **env(2, 30, 300, 90, 300), "eg1.velocity": 40},
  fxs=[fx(0, "Ensemble", 25), fx(1, "Hall Reverb", 20)])
P(A, "Sync Horn", BRASS, "lead", {**LEGATO, "vco2.level": 90, "vco2.syncMode": 1, "vco2.coarse": 5, **ladder(1100, 25, 2.5), **env(1, 80, 500, 50, 250), **env(2, 40, 300, 95, 250)},
  mods=[mod(1, EG1, "vco2.coarse", 20)])
P(A, "Soft Brass", BRASS, "chord", {**two_osc(2, 5, 6, 3, 60), "vco2.pulseWidth": 30, **ladder(350, 8, 2.5), **env(1, 220, 900, 70, 600), **env(2, 150, 400, 90, 700)},
  fxs=[fx(0, "Plate Reverb", 25)])
P(A, "Laser Zap", FXC, "fx", {**MONO, "vco1.wave": 4, **multimode(6000, 60, 0), "eg1.pitchAmount": 24, **env(1, 1, 180, 0, 100), **env(2, 1, 250, 0, 150)},
  fxs=[fx(0, "Ping-Pong Delay", 30)])
P(A, "Noise Wind", FXC, "drone", {"vco1.level": 0, "noise.level": 100, "noise.type": 1, **multimode(900, 75, 0, mode=1), **env(2, 1500, 500, 90, 2500), "lfo1.rate": 0.15},
  mods=[mod(1, LFO1, "vcfA.cutoff", 50), mod(2, LFO2, "pan", 60)], fxs=[fx(0, "Hall Reverb", 40, 0, p1=5)])
P(A, "Siren", FXC, "drone", {**MONO, "vco1.wave": 1, **multimode(3000, 20, 0), **env(2, 50, 100, 100, 600), "lfo1.rate": 0.6, "lfo1.shape": 1},
  mods=[mod(1, LFO1, "pitch", 60)])
P(A, "Dark Drone", DRONE, "drone", {**two_osc(2, 2, 3, 2, 100), "vco3.level": 80, "vco3.octave": 1, "vco3.fine": -4, **ladder(220, 55, 0, 6), **env(2, 2500, 500, 100, 4000), "lfo1.rate": 0.05},
  mods=[mod(1, LFO1, "vcfB.cutoff", 30)], fxs=[fx(0, "Tape", 60), fx(1, "Hall Reverb", 45, 0, p1=8)])
P(A, "Fifth Drone", DRONE, "drone", {**two_osc(5, 5, 4, 3, 100), "vco2.coarse": 7, "vco1.pulseWidth": 40, **multimode(1200, 35, 0, mode=0), **env(2, 1800, 500, 100, 3500)},
  mods=[mod(1, LFO1, "vco1.pulseWidth", 30), mod(2, LFO2, "vcfA.cutoff", 20)], fxs=[fx(0, "Chorus", 40), fx(1, "Plate Reverb", 40, 0, p1=4)])

# =============================================================================
# Bank B — Modular & Generative (cables, expanders, sequencing, effects)
# =============================================================================
B = 1
P(B, "Analogue Sequence", BASS, "transport", {**MONO, **ladder(450, 55, 3, 4), **env(1, 1, 180, 0, 80), **env(2, 1, 220, 0, 80),
  "seq.run": 2, "seq.rate": 4, "seq.length": 15, "seq.scale": 12, "seq.root": 9, **{f"seq.step{k + 1}A": v for k, v in enumerate([0, 0, 25, 0, 42, 0, 17, 0, 0, 0, 25, 50, 42, 25, 17, 8])},
  **{f"seq.step{k + 1}C": v for k, v in enumerate([10, 30, 60, 20, 90, 10, 40, 70, 15, 35, 65, 25, 100, 50, 20, 80])}},
  mods=[mod(1, ROWC, "vcfB.cutoff", 40)], fxs=[fx(0, "Tape Echo", 22)])
P(B, "Arp Cascade", PLUCK, "arp", {**ladder(1200, 35, 3), **env(1, 1, 200, 0, 120), **env(2, 1, 300, 0, 200), "arp.mode": 1, "arp.pattern": 2, "arp.octaves": 2, "arp.rate": 4},
  fxs=[fx(0, "Ping-Pong Delay", 30), fx(1, "Hall Reverb", 25)])
P(B, "Euclid Pulse", PLUCK, "arp", {"vco1.wave": 4, **multimode(1800, 50, 3), **env(2, 1, 200, 0, 150), "arp.mode": 1, "arp.rhythm": 1, "arp.euclidSteps": 13, "arp.euclidPulses": 5, "arp.rate": 4},
  fxs=[fx(0, "Room Reverb", 20)])
P(B, "Generative Keys", KEYS, "transport", {"vco1.wave": 1, **two_osc(1, 0, 0, 4, 30), **multimode(3000, 10, 2), **env(2, 1, 900, 20, 700),
  "gen.mode": 1, "gen.style": 4, "gen.scale": 1, "gen.density": 60, "gen.evolve": 30}, fxs=[fx(0, "Plate Reverb", 30)])
P(B, "Generative Bassline", BASS, "transport", {**MONO, **ladder(380, 50, 3, 5), **env(1, 1, 200, 0, 90), **env(2, 1, 250, 60, 90),
  "gen.mode": 1, "gen.style": 1, "gen.scale": 11, "gen.root": 4, "gen.density": 70})
P(B, "Ambient Generator", PAD, "transport", {**two_osc(1, 0, 4, 4, 50), **multimode(2500, 20, 0.5), **env(2, 400, 800, 70, 2500),
  "gen.mode": 1, "gen.style": 3, "gen.scale": 7, "gen.density": 35, "gen.evolve": 50}, fxs=[fx(0, "Granular", 35), fx(1, "Hall Reverb", 45, 0, p1=7)])
P(B, "Diode Squelch", BASS, "bass", {**MONO, "global.filterRouting": 1, "vco1.wave": 2, **env(1, 1, 220, 0, 90), **env(2, 1, 300, 80, 90), "diodeLadder.cutoff": 700, "diodeLadder.resonance": 70, "diodeLadder.drive": 8},
  cables=[("vco1.out", "diodeLadder.in", 1), ("diodeLadder.out", "vca.in", 1), ("eg1.out", "diodeLadder.cv1", 0.8)])
P(B, "OTA Ripples", PLUCK, "chord", {"global.filterRouting": 0, **env(1, 1, 300, 0, 200), **env(2, 1, 500, 0, 300), "ota.mode": 1, "ota.cutoff": 900, "ota.resonance": 60},
  cables=[("mixer.out", "ota.in", 1), ("ota.out", "vca.in", 1), ("eg1.out", "ota.cv1", 0.9)], fxs=[fx(0, "Chorus", 30)])
P(B, "Formant Voice", LEAD, "lead", {**LEGATO, **two_osc(2, 5, 5, 3, 60), **env(2, 40, 300, 90, 300), "formant.vowel": 30, "formant.resonance": 70, "formant.mix": 100, "lfo2.rate": 0.4},
  cables=[("mixer.out", "formant.in", 1), ("formant.out", "vca.in", 1), ("lfo2.out", "formant.cv", 0.7)], mods=[mod(1, LFO1, "pitch", 5, via=MODW)])
P(B, "Comb Metal", FXC, "chord", {**env(2, 1, 900, 0, 900), "vco1.wave": 4, "comb.frequency": 400, "comb.feedback": 85, "comb.mix": 80, "comb.damping": 30},
  cables=[("mixer.out", "comb.in", 1), ("comb.out", "vca.in", 1)], fxs=[fx(0, "Plate Reverb", 25)])
P(B, "Fixed Bank Strings", PAD, "chord", {**two_osc(2, 2, 7), **env(2, 600, 500, 90, 1600), "filterBank.band3": 80, "filterBank.band5": 70, "filterBank.band6": 60, "filterBank.band8": 40, "filterBank.level": 90},
  cables=[("mixer.out", "filterBank.in", 1), ("filterBank.out", "vca.in", 1)], fxs=[fx(0, "Ensemble", 60), fx(1, "Hall Reverb", 30)])
P(B, "S&H Bleeps", FXC, "drone", {"vco1.wave": 4, **multimode(1400, 70, 0, mode=1), **env(2, 1, 100, 100, 400), "sampleHold.rate": 8, "vcfA.shAmount": 2.5},
  fxs=[fx(0, "Ping-Pong Delay", 35)])
P(B, "Ring Bells Cluster", KEYS, "chord", {"vco1.level": 0, "ringMod.level": 100, "vco1.wave": 1, "vco2.wave": 0, "vco2.coarse": 3, "vco2.fine": 41, **multimode(7000, 10, 0), **env(2, 1, 2500, 0, 2500)},
  fxs=[fx(0, "Hall Reverb", 40, 0, p1=4.5)])
P(B, "Wavefolded Lead", LEAD, "lead", {**LEGATO, "vco1.wave": 1, **ladder(2600, 20, 1.5, 20), **env(1, 1, 600, 40, 300), **env(2, 2, 300, 90, 300), "vca.saturation": 70},
  mods=[mod(1, M1, "vcfB.drive", 50), mod(2, LFO1, "vcfB.cutoff", 15)], fxs=[fx(0, "Tape", 50)])
P(B, "Cross-Mod Chaos", FXC, "drone", {"vco1.fmAmount": 80, "vco2.level": 0, "vco2.wave": 2, "vco2.octave": 2, "vco2.coarse": 5, **ladder(1800, 40, 0), **env(2, 400, 300, 100, 1500)},
  cables=[("vco2.out", "vco1.fm", 0.9), ("vco1.out", "vco2.fm", 0.4)], fxs=[fx(0, "BBD Delay", 30)])
P(B, "Duo Split Bass Lead", LEAD, "chord", {"global.zoneMode": 1, "global.splitPoint": 55, "global.zoneATranspose": -12, **ladder(900, 30, 2.5), **env(2, 2, 400, 85, 200)})
P(B, "Stutter Chords", KEYS, "transport", {**two_osc(2, 2, 7), **ladder(1500, 20, 1.5), **env(2, 5, 300, 90, 300), "arp.mode": 1, "arp.pattern": 13, "arp.rate": 3},
  fxs=[fx(0, "Stutter", 100, 0, p1=60), fx(1, "Plate Reverb", 25)])
P(B, "Granular Cloud", PAD, "chord", {**two_osc(1, 1, 6, 4, 60), **multimode(4000, 10, 0), **env(2, 800, 500, 90, 3000)},
  fxs=[fx(0, "Granular", 70, 0, p1=40, p2=12), fx(1, "Hall Reverb", 40, 0, p1=6)])
P(B, "Spectral Freeze Pad", PAD, "chord", {**two_osc(2, 2, 9), **ladder(1200, 15, 0), **env(2, 300, 500, 90, 2500)},
  fxs=[fx(0, "Spectral", 60, 0, p1=60, p2=12), fx(1, "Hall Reverb", 35)])
P(B, "Tape Memory Keys", KEYS, "chord", {"vco1.wave": 0, "vco2.wave": 1, "vco2.level": 40, "vco2.octave": 4, **multimode(3500, 0, 1.5), **env(2, 2, 1500, 30, 800)},
  fxs=[fx(0, "Tape", 80), fx(1, "Tape Echo", 25), fx(2, "Spring Reverb", 20)])
P(B, "Lo-Fi Bass", BASS, "bass", {**MONO, "vco1.wave": 4, "vco1.subLevel": 60, **ladder(600, 20, 2), **env(2, 1, 300, 60, 100)},
  fxs=[fx(0, "Lo-Fi", 70, 0, p0=8), fx(1, "Compressor", 100)])
P(B, "Tremolo Organ", KEYS, "chord", {"vco1.wave": 1, "vco2.wave": 0, "vco2.level": 60, "vco2.octave": 2, **multimode(5000, 0, 0), **env(2, 5, 100, 100, 60)},
  fxs=[fx(0, "Tremolo / Pan", 100, 0, p1=6), fx(1, "Spring Reverb", 30)])
P(B, "Phased Brass", BRASS, "chord", {**two_osc(2, 2, 8), **ladder(600, 20, 3), **env(1, 90, 700, 55, 400), **env(2, 50, 300, 90, 400)},
  fxs=[fx(0, "Phaser", 50), fx(1, "Hall Reverb", 25)])
P(B, "Macro Morph Pad", PAD, "chord", {**two_osc(5, 2, 7), **ladder(900, 20, 1), **env(2, 700, 600, 90, 2200), "macro.macro1": 40},
  mods=[mod(1, M1, "vcfB.cutoff", 60), mod(2, M1, "vco1.pulseWidth", -40), mod(3, M2, "global.instability", 80)], fxs=[fx(0, "Chorus", 40), fx(1, "Plate Reverb", 35)])
P(B, "Velocity Mallets", PLUCK, "chord", {"vco1.wave": 0, "vco2.wave": 1, "vco2.level": 50, "vco2.octave": 5, "vco2.coarse": 7, **multimode(2500, 30, 2.5), **env(1, 1, 300, 0, 300), **env(2, 1, 900, 0, 800), "eg2.velocity": 60, "eg1.velocity": 70},
  fxs=[fx(0, "Room Reverb", 30)])
P(B, "Slew Glide Lead", LEAD, "lead", {**LEGATO, "vco1.wave": 3, "vco2.level": 70, "vco2.wave": 5, "vco2.fine": 6, **ladder(2000, 35, 2), **env(2, 5, 300, 90, 250), "global.glide": 180, "global.glideConstantTime": 0})
P(B, "Pink Noise Surf", FXC, "drone", {"vco1.level": 0, "noise.level": 100, "noise.type": 1, **multimode(500, 30, 0), **env(2, 2000, 500, 100, 3000), "lfo1.rate": 0.1},
  mods=[mod(1, LFO1, "vcfA.cutoff", 60), mod(2, LFO1, "amp", 30)], fxs=[fx(0, "Hall Reverb", 35)])
P(B, "Haunted Spring", DRONE, "drone", {"vco1.wave": 1, "vco2.level": 70, "vco2.wave": 1, "vco2.fine": 13, **multimode(1500, 40, 0), **env(2, 1000, 500, 100, 3000), "lfo1.rate": 0.3},
  mods=[mod(1, LFO1, "pitch", 4)], fxs=[fx(0, "Spring Reverb", 80, 0, p0=4)])
P(B, "Sub Pressure", BASS, "bass", {**MONO, "vco1.wave": 0, "vco1.subLevel": 50, **multimode(400, 0, 1), **env(2, 1, 900, 70, 200)},
  mods=[mod(1, AT, "vcfA.cutoff", 60)], fxs=[fx(0, "Compressor", 100)])
P(B, "Dual Filter Vox", PAD, "chord", {"global.filterRouting": 3, **two_osc(2, 2, 6), "vcfA.mode": 1, "vcfA.cutoff": 900, "vcfA.resonance": 60, "vcfB.cutoff": 2400, "vcfB.resonance": 50, **env(2, 500, 500, 90, 1800)},
  mods=[mod(1, LFO1, "vcfA.cutoff", 25)], fxs=[fx(0, "Ensemble", 40)])
P(B, "Clock Tick Percussion", FXC, "transport", {**MONO, "vco1.level": 30, "noise.level": 100, "eg1.pitchAmount": 18, **ladder(2500, 20, 2), **env(1, 1, 60, 0, 40), **env(2, 0.5, 140, 0, 140),
  "seq.run": 2, "seq.rate": 4, **{f"seq.step{k + 1}Gate": g for k, g in enumerate([1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1])}}, fxs=[fx(0, "Room Reverb", 20)])
P(B, "Shimmer Drone", DRONE, "drone", {**two_osc(1, 1, 5, 4, 60), "vco2.coarse": 7, **multimode(3000, 10, 0), **env(2, 3000, 500, 100, 5000)},
  fxs=[fx(0, "Spectral", 40, 0, p2=12), fx(1, "Hall Reverb", 55, 0, p1=9)])

assert len([p for p in PRESETS if p["bank"] == 0]) == 32, len([p for p in PRESETS if p["bank"] == 0])
assert len([p for p in PRESETS if p["bank"] == 1]) == 32, len([p for p in PRESETS if p["bank"] == 1])

# =============================================================================
def build(gains, lifts={}):
    fxmap = os.path.join(ROOT, "DSPTests/build/fxmap")
    reqs = []
    for i, p in enumerate(PRESETS):
        # Quiet, percussive voices get a make-up limiter in slot 8 (input gain in dB).
        p["fx"] = [f for f in p["fx"] if f.get("auto") is None]
        if i in lifts: p["fx"].append({"slot": 7, "type": FX["Limiter"], "mix": 100, "bus": 0, "real": {0: lifts[i], 1: -1.0}, "auto": True})
        for f in p["fx"]:
            for k, v in f["real"].items(): reqs.append(f"{f['type']} {k} {v}")
    out = subprocess.run([fxmap], input="\n".join(reqs) + "\n", capture_output=True, text=True, check=True).stdout.split()
    it = iter(float(x) for x in out)
    defaults = {}
    for line in subprocess.run([fxmap, "--defaults"], capture_output=True, text=True, check=True).stdout.splitlines():
        t, k, v = line.split(); defaults[(int(t), int(k))] = float(v)
    result = []
    for i, p in enumerate(PRESETS):
        params = dict(p["params"])
        for f in p["fx"]:
            b = f"fx{f['slot'] + 1}"
            params[f"{b}.type"] = f["type"]
            if f["mix"] is not None: params[f"{b}.mix"] = f["mix"]
            params[f"{b}.bus"] = f["bus"]
            for k in range(12):   # the type's own defaults, so every host loads the identical sound
                if (f["type"], k) in defaults: params[f"{b}.p{k + 1}"] = round(defaults[(f["type"], k)], 6)
            for k in f["real"]: params[f"{b}.p{k + 1}"] = round(next(it), 6)
        params["global.masterVolume"] = round(min(6.0, max(-40.0, params.get("global.masterVolume", PARAMS["global.masterVolume"]["d"]) + gains.get(i, 0.0))), 2)
        for k, v in params.items():
            assert k in PARAMS, f"{p['name']}: unknown parameter {k}"
            q = PARAMS[k]; assert q["lo"] <= v <= q["hi"], f"{p['name']}: {k}={v} outside {q['lo']}…{q['hi']}"
        num = sum(1 for x in PRESETS[:i] if x["bank"] == p["bank"])
        result.append({"name": p["name"], "bank": p["bank"], "number": num, "category": CAT_NAMES[p["category"]], "audition": p["audition"],
                       "fxTypes": {str(f["slot"]): f["type"] for f in p["fx"]},
                       "parameters": {k: params[k] for k in sorted(params)},
                       "cables": [{"from": c[0], "to": c[1], "gain": c[2]} for c in p["cables"]]})
    return result

def write(result):
    doc = {"version": 1, "banks": ["Classic", "Modular & Generative"], "presets": result}
    text = json.dumps(doc, indent=1, ensure_ascii=False) + "\n"
    for path in ("Packages/VekturKit/Sources/VekturCore/Resources/FactoryPresets.json", "web/presets.json"):
        open(os.path.join(ROOT, path), "w").write(text)
    inc = ["// GENERATED by tools/factory_presets.py — do not edit.", "struct FPParam { uint64_t a; float v; };",
           "struct FPCable { const char* from; const char* to; float gain; };",
           "struct FPreset { const char* name; int bank; const char* audition; std::vector<FPParam> params; std::vector<FPCable> cables; };",
           "static const std::vector<FPreset> kFactory = {"]
    for r in result:
        ps = ", ".join(f"{{{PARAMS[k]['a']}u, {float(v)}f}}" for k, v in r["parameters"].items())
        cs = ", ".join(f'{{"{c["from"]}", "{c["to"]}", {float(c["gain"])}f}}' for c in r["cables"])
        inc.append(f'    {{"{r["name"]}", {r["bank"]}, "{r["audition"]}", {{{ps}}}, {{{cs}}}}},')
    inc += ["};", ""]
    open(os.path.join(ROOT, "DSPTests/factory_presets.inc"), "w").write("\n".join(inc))

def audit():
    exe = os.path.join(ROOT, "DSPTests/build/preset_audit")
    subprocess.run(["cmake", "--build", os.path.join(ROOT, "DSPTests/build"), "--target", "preset_audit"], check=True, capture_output=True)
    out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout
    return [json.loads(l) for l in out.splitlines() if l.startswith("{")]

if __name__ == "__main__":
    gains, lifts = {}, {}
    write(build(gains))
    if "--no-audit" not in sys.argv:
        # Gain staging: loudness (RMS of the audition) to −20 dBFS, peaks no higher than −1 dBFS.
        for row in audit():
            target = -20.0 - row["rms"]
            headroom = -1.0 - row["peak"]
            gains[row["i"]] = round(max(-24.0, min(target, headroom, 12.0)), 1)
            if target > 10.0: lifts[row["i"]] = round(min(target - 6.0, 18.0), 1)
        write(build({}, lifts))
        for row in audit():   # second pass with the make-up limiters in place
            gains[row["i"]] = round(max(-24.0, min(-20.0 - row["rms"], -1.0 - row["peak"], 12.0)), 1)
        write(build(gains, lifts))
        rows = audit()
        print(f"{len(rows)} presets audited; loudness {min(r['rms'] for r in rows):.1f} … {max(r['rms'] for r in rows):.1f} dBFS, max peak {max(r['peak'] for r in rows):.1f} dBFS")
    print(f"wrote {len(PRESETS)} factory presets")
