# Hardware aesthetic: design direction

Status: adopted (from user-supplied reference photos, Nov 2026). Black-painted
panels sit on brushed-aluminium rails and trim. It is implemented in Phase 8.

## Panel language
- Panels are painted matte black (#141414 range). Legends are white silkscreen; secondary legends are light grey.
- Grey aluminium rack rails run top and bottom, with visible screws. Black tolex
  cabinet sides surround the rack, which is organised in rows of cabinets.
- Each module carries a printed block diagram: thin white boxes and arrows that
  describe its signal flow, drawn in our own words and layout.
- Main controls are vertical slider banks: a black slot, a white cap with an
  index line, and tick scales.
- Secondary controls are small knobs with silver caps and a black pointer.
- Jacks sit in rows along the panel bottom. Each has a silver ring with a black hole.
- Indicators: red and green LEDs, red manual-gate push buttons, and needle VU
  meters with a cream face.
- Patch cables are colour-coded with drooping catenary curves (the canvas already has adjustable opacity and thickness).

## IP rules (non-negotiable)
- No manufacturer logo, wordmark, model numbers or series name.
- No traced or copied panel artwork. Every layout and legend is original.
- Legends use generic function names (vco, vcf, vca, env, lfo, s/h, mix, out).

## Planned modules (from the reference photos)
- Keyboard controller with portamento and bender → Phase 4
- LFO bank → Phase 4
- Analog sequencer and analog switch → Phase 5
- Reverb, phaser, delay and pan/mixer → Phase 6
- VU meters on the output and mixer modules → Phase 8

## Surface realism and wear (user requirement: match the reference photos closely)
- Brushed metal on rails, trim, knob caps and jack rings: fine directional
  grain with an anisotropic highlight that shifts subtly with device tilt
  (optional, off when Reduce Motion is on).
- Panel paint: slight satin sheen and faint orange-peel texture, not flat black.
- Wear marks are procedural and seeded per module, so they stay stable and
  repeatable:
  - hairline scratches and scuffs near jacks and slider slots
  - paint rub-through on panel edges and around screws
  - fingerprint smudges on slider caps
  - slight yellowing of white legends and caps
  - dust in the rail channels
- A "Wear" control (0 = factory new … 1 = well-used studio unit) is available per rack and per module. Wear never reduces
  legibility: legend contrast stays at or above WCAG AA at every wear level.
- Wear is rendered with layered vector textures and noise shaders (SwiftUI
  Canvas / Metal layer effects). No photographic textures are copied from the
  reference images.
- Before Phase 8 sign-off, renders are compared side by side against the reference photos.

## As built (Phase 8)
- **Layouts are data.** `tools/panels.py` describes every panel: title legend, block-diagram kind, slider bank, knobs, switches, width in hp (22 pt) and its rack module (for jacks). It writes `VekturCore/Resources/Panels.json`, used by the SwiftUI rack, and `web/panels.json`, used by the browser build.
- **Control styles** follow the parameter: the slider bank uses faders; booleans and lists of up to 4 use lever switches; longer lists use rotary selectors; everything else is a knob.
- **Wear** is seeded per module key (FNV-1a + LCG, identical in Swift and JavaScript). It draws:
  - 26 hairline scratches in the lower half of the panel, near jacks and slots;
  - 10 rub-throughs on the edges;
  - 4 smudges;
  - dust in the rail channels.

  Fingerprints on fader caps and legend yellowing scale with Wear. At full Wear the legends still have a 15:1 contrast ratio against the panel (AA requires 4.5:1).
- **Screens (13):** rack, patch canvas, perform, sequencer, arpeggiator, generative, effects, modulation, mixer & output, MIDI, presets, settings, diagnostics.
- **Still to do:** tilt-reactive anisotropic highlights (needs the Metal layer effect, Phase 10), and the side-by-side comparison with the reference photos before sign-off.
