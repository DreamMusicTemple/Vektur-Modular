#!/usr/bin/env python3
"""Hardware panel layouts (Phase 8) — the single description of every
module panel, used by the SwiftUI rack (VekturCore/Resources/Panels.json, PanelLayout.swift) and
the web test build (web/panels.json).

A panel names its controls by parameter identifier. The control style comes
from the parameter (boolean → toggle, ≤ 4 choices → lever switch, more →
rotary selector, continuous → knob) unless the panel lists it under
"sliders" (the main slider bank). Jacks come from the rack catalogue at run
time (module "key"). Legends are generic function names only (see
docs/design/HARDWARE_AESTHETIC.md, IP rules).

Run `python3 tools/panels.py` after editing; it validates every identifier.
"""
import json, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def M(key, title, diagram, sliders=(), knobs=(), switches=(), hp=None, rack=None, popup=False):
    """popup=True: the knobs and switches live in a pop-up window opened from a
    button on the panel, keeping only the sliders (and jacks) on the front."""
    front_knobs = 0 if popup else (len(knobs) + 1) // 2
    return {"key": key, "rack": rack if rack is not None else key, "title": title, "diagram": diagram,
            "sliders": list(sliders), "knobs": list(knobs), "switches": list(switches), "popup": bool(popup),
            "hp": hp or max(4, (len(sliders) + 1) // 1 + front_knobs)}

def vco(n, extra_knobs=()):
    p = f"vco{n}"
    return M(p, f"vco {n}", "osc", sliders=[f"{p}.pulseWidth", f"{p}.fmAmount"],
             knobs=[f"{p}.coarse", f"{p}.fine", *extra_knobs, f"{p}.startPhase"],
             switches=[f"{p}.wave", f"{p}.octave", f"{p}.syncMode", f"{p}.fmMode", f"{p}.lowFreqMode", f"{p}.phaseReset"])

def lfo(n):
    p = f"lfo{n}"
    return M(p, f"lfo {n}", "lfo", sliders=[f"{p}.rate", f"{p}.level"], knobs=[f"{p}.delay", f"{p}.phase"],
             switches=[f"{p}.shape", f"{p}.sync", f"{p}.tempoSync"])

def env(n):
    p = f"eg{n}"
    extra = [f"{p}.pitchAmount"] if n == 1 else []
    return M(p, f"env {n}", "env", sliders=[f"{p}.attack", f"{p}.decay", f"{p}.sustain", f"{p}.release"],
             knobs=[f"{p}.velocity", *extra], switches=[f"{p}.curve", f"{p}.retrigger", f"{p}.loop"])

ROWS = [
    {"id": "sources", "name": "sources", "modules": [
        vco(1, ["vco1.subLevel"]) | {"switches": ["vco1.wave", "vco1.octave", "vco1.subOctave", "vco1.syncMode", "vco1.fmMode", "vco1.lowFreqMode", "vco1.phaseReset"]},
        vco(2), vco(3, ["vco3.pitchModAmount"]),
        M("noise", "noise", "noise", sliders=["noise.level"], knobs=["noise.colour"], switches=["noise.type"]),
        M("ringMod", "ring mod", "ring", sliders=["ringMod.level"], knobs=["ringMod.carrierLeak"]),
        M("mixer", "mix", "mixer", sliders=["vco1.level", "vco2.level", "vco3.level", "vco1.subLevel", "noise.level", "ringMod.level"]),
    ]},
    {"id": "voice", "name": "filters · amplifiers · envelopes", "modules": [
        M("vcfA", "vcf a", "filter", sliders=["vcfA.cutoff", "vcfA.resonance", "vcfA.envAmount"],
          knobs=["vcfA.drive", "vcfA.keyTrack", "vcfA.shAmount", "vcfA.vco3Amount"], switches=["vcfA.mode", "global.filterRouting"]),
        M("vcfB", "vcf b", "ladder", sliders=["vcfB.cutoff", "vcfB.resonance", "vcfB.envAmount"],
          knobs=["vcfB.drive", "vcfB.keyTrack", "vcfB.shAmount", "vcfB.vco3Amount"]),
        env(1), env(2),
        M("vca", "vca", "amp", sliders=["vca.initialGain"], knobs=["vca.velocity", "vca.saturation"], switches=["vca.response"]),
    ]},
    {"id": "modulation", "name": "modulation · control", "modules": [
        lfo(1), lfo(2), lfo(3),
        M("sampleHold", "s/h", "sh", sliders=["sampleHold.rate"], knobs=["sampleHold.slew"], switches=["sampleHold.source"]),
        M("macro", "macro", "macro", sliders=["macro.macro1", "macro.macro2", "macro.macro3", "macro.macro4"], rack=""),
        M("keyboard", "keyboard ctrl", "keys", sliders=["global.glide"], knobs=["global.pitchBendRange", "global.bendDown", "global.unisonDetune"],
          switches=["global.voiceMode", "global.notePriority", "global.glideMode", "global.unisonVoices", "global.polyphony"], popup=True),
    ]},
    {"id": "expanders", "name": "expanders", "modules": [
        M("ota", "ota vcf", "filter", sliders=["ota.cutoff", "ota.resonance"], knobs=["ota.drive", "ota.cv1Amount", "ota.cv2Amount"], switches=["ota.mode"]),
        M("diodeLadder", "diode vcf", "ladder", sliders=["diodeLadder.cutoff", "diodeLadder.resonance"], knobs=["diodeLadder.drive", "diodeLadder.cv1Amount", "diodeLadder.cv2Amount"]),
        M("transistorLadder", "ladder vcf", "ladder", sliders=["transistorLadder.cutoff", "transistorLadder.resonance"], knobs=["transistorLadder.drive", "transistorLadder.cv1Amount", "transistorLadder.cv2Amount"]),
        M("vcbpf", "vc bpf", "filter", sliders=["vcbpf.centre", "vcbpf.q"], knobs=["vcbpf.cv1Amount", "vcbpf.cv2Amount", "vcbpf.level"]),
        M("comb", "comb", "comb", sliders=["comb.frequency", "comb.feedback"], knobs=["comb.damping", "comb.mix", "comb.cvAmount"]),
        M("formant", "formant", "formant", sliders=["formant.vowel", "formant.shift"], knobs=["formant.resonance", "formant.mix", "formant.cvAmount"]),
        M("filterBank", "fixed bank", "bank", sliders=[f"filterBank.band{k}" for k in range(1, 9)], knobs=["filterBank.level"]),
        M("vca2", "vca 2", "amp", sliders=["vca2.initialGain"], knobs=["vca2.cvAmount", "vca2.saturation"], switches=["vca2.response"]),
        M("mixer4", "mixer", "mixer", sliders=["mixer4.gain1", "mixer4.gain2", "mixer4.gain3", "mixer4.gain4"], knobs=["mixer4.level"]),
        M("slew", "slew", "util", sliders=["slew.rise", "slew.fall"], switches=["slew.mode"]),
        M("follower", "env follow", "util", sliders=["follower.attack", "follower.release"], knobs=["follower.threshold", "follower.gain"], switches=["follower.detector"]),
        M("vproc", "v proc", "util", knobs=["vproc.gain1", "vproc.gain2", "vproc.offset"], switches=["vproc.invert", "vproc.limit"]),
        M("switch", "a switch", "switch", switches=["switch.mode", "switch.position", "switch.steps"]),
        M("gateDelay", "gate delay", "util", knobs=["gateDelay.delay", "gateDelay.length"]),
        M("mult1", "mult", "mult", rack="mult1"),
    ]},
    {"id": "output", "name": "output", "modules": [
        M("output", "output", "out", sliders=["global.masterVolume"], knobs=["global.stereoSpread", "global.instability", "global.masterTune", "global.tempo"],
          switches=["global.engineMode", "global.oversampling"], hp=8),
    ]},
]

def main():
    params = {p["id"]: p for p in json.load(open(os.path.join(ROOT, "web/params.json")))["params"]}
    bad = []
    for row in ROWS:
        for m in row["modules"]:
            for k in ("sliders", "knobs", "switches"):
                for pid in m[k]:
                    if pid not in params: bad.append(f"{m['key']}.{k}: {pid}")
            m["hp"] = max(m["hp"], len(m["sliders"]) * 2 + 2 if m["sliders"] else 4)
    if bad:
        print("unknown parameters:\n  " + "\n  ".join(bad)); sys.exit(1)
    doc = {"version": 1, "hpWidth": 22, "rows": ROWS}
    text = json.dumps(doc, indent=1, ensure_ascii=False) + "\n"
    for path in ("Packages/VekturKit/Sources/VekturCore/Resources/Panels.json", "web/panels.json"):
        with open(os.path.join(ROOT, path), "w") as f: f.write(text)
        print("wrote", path)
    print(sum(len(r["modules"]) for r in ROWS), "panels")

if __name__ == "__main__":
    main()
