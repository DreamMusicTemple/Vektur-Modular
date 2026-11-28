# Vektur Modular — native vs web UI parity audit

Date 2026-11-28 · Standard: `docs/UI_PARITY_STANDARD.md` (the web version is the master design).
Web reference: `web/index.html` + `web/app.js` (what is served at dreammusictemple.github.io/Vektur-Modular).
Native: `Packages/VekturKit/Sources/VekturUI` (shared by the app and the AUv3).

## Summary

The **rack** screen matches closely. It was built from the same data file (`Panels.json`) as the web rack, with the same panels, knobs, faders, switches, jacks, cables, wear, the jack menu and the keyboard ctrl pop-up.

**Every other screen diverged.** They were built in Phases 4–7 as generic iOS pages: forms, lists, system pickers, toggles and plain sliders. The web version was then built in Phase 8 with hardware panels on every screen. The native app also has a different navigation structure (a sidebar or tab bar with 13 screens, where the web has 8 tabs) and six screens or sheets the web version does not have.

No change below touches the DSP, the parameter tree or the audio thread. Every native control already writes through `ParameterModel` → `AUParameter`, and that stays.

## Priority 1 — structural

| # | Area | Web (master) | Native now | Restore |
|---|---|---|---|---|
| S1 | Navigation | A tab row under the header: **rack · sequencer · arp·gen · effects · matrix · midi · presets · manual · about** | Sidebar (iPad/Mac) or tab bar (iPhone) with 13 screens, icons and "open in new window" buttons | Replace with the web's tab row inside the top bar, using the same 9 tabs in the same order. Remove the sidebar, tab bar and window buttons. |
| S2 | Extra screens | — | **Patch** (canvas), **Perform**, **Generative** (separate), **Mixer**, **Settings**, **Diagnostics** | Remove from navigation (see decisions D1–D3). Their controls go back to where the web has them: generative → arp·gen; output/HQ/battery saving → about; recorder → the rec button only. |
| S3 | Sequencer screen | One hardware panel "analogue sequencer · 16 steps × 3 rows". A row of settings switches, then 16 columns, each with an LED, a row A **fader**, row B and C **knobs**, gate **switch**, prob **knob** and ratchet **switch**. A note sheet below. | A long scrolling page combining clock, arpeggiator, sequencer, generative, capture and preset-creator sections in generic grids | Rebuild as the web panel. Arp and gen move to the arp·gen tab. |
| S4 | Arp · gen screen | One cabinet with two panels (arpeggiator 26 hp, generative 22 hp), then the preset-creator sheet (seed field + 8 category buttons) | Arpeggiator and Generative are separate screens; the preset creator is on Sequencer and Presets | Merge into one screen as the web has it. |
| S5 | Effects screen | Cabinet with 8 **fx slot panels** (13 hp): type menu, on/bus switches, mix fader, out knob, up to 12 param **knobs**. A second cabinet holds the buses panel. | Cards with a type menu, system toggle, segmented picker and plain **sliders**; buses elsewhere | Rebuild as the web panels. |
| S6 | Matrix screen | Sheet with a 16-row **table**: on · source · scale-by · destination · amount knob · offset knob · curve · polarity · invert · slew knob · live bar. Macros row of 4 big knobs. | "Modulation" page with performance wheels, chord readout, voicing, zones, LFO, macro and matrix sections; matrix rows use system pickers, toggles and **sliders** | Rebuild as the web table. Voicing, zones and LFO controls already live on the rack and the midi panel. |
| S7 | MIDI screen | "midi interface" hardware panel (switches + knobs) · devices sheet (connect, input, output) · monitor readouts · learn row (learn, mode, encoder, pickup, standard layout) · mapping table · profile copy/import · reference table + CSV/MD export | Generic sections with system pickers, a learn sheet, a mapping list, file-based profile export and a reference card that opens a sheet | Rebuild in the web order and styles. See D5 for devices and the profile text box. |
| S8 | Presets screen | Factory banks as a grid of buttons (name + category) · "your presets" sheet (name, save, initialise, copy state, load pasted state, table) · files sheet (export preset/project/pattern, import) | SwiftData library: search, category picker, favourites, tags, recents, preset creator, in a system Form | Rebuild as the web sheets. Favourites/tags/recents are native additions: see D4. |
| S9 | About tab | Performance sheet (oscillators, filter oversampling, battery saving, recording depth), about text, logo, contact line | Settings screen (Form) + About sheet | Rebuild as the web about tab. App-only settings (keep awake, background audio) go in the same sheet (D6). |
| S10 | Manual tab | Manual is a tab: table of contents + search on the left, chapter on the right | Manual under Settings + a help sheet | Make it a tab like the web; keep the ? button, which opens the tab at the right chapter, as the web's does. |

## Priority 2 — controls

| # | Where | Web | Native now | Restore |
|---|---|---|---|---|
| C1 | Effects params, mix, output | knobs / fader | `Slider`, `ParamControl` | `PanelKnob` / `PanelFader` |
| C2 | Matrix amount, offset, slew | knobs | `Slider` | `PanelKnob` (small) |
| C3 | Matrix on, curve, polarity, invert, source, via | hardware switches | `Toggle`, `Picker` | `PanelControl` switch style (as web `switchEl`) |
| C4 | Sequencer rows B, C, prob | knobs | `VerticalSlider` | `PanelKnob` |
| C5 | Sequencer gate, ratchet | switches | custom gate button | `PanelControl` switch |
| C6 | Effects on / bus | switches | `Toggle`, segmented `Picker` | `PanelControl` switch |
| C7 | MIDI settings | switches + knobs on a panel | `ParamControl` grid | `PanelControl` / `PanelKnob` on a panel |
| C8 | Keyboard dock | bend + mod **wheels**, oct − / range readout / oct +, **hold**, **vel** slider, keys, ▼ hide button | Keys only, with a chevron hide button; wheels live on Perform/Modulation | Rebuild the dock as the web's |
| C9 | Top bar | **power** button, **meter** menu (4/4, 3/4, 6/8, 5/4, 7/8) | Missing; extra ? button | Add power and meter; the ? button is approved (it's in the web header too). |
| C10 | Rack footer | Note line, **cable opacity** slider, **remove all cables** button | Missing (opacity is in Settings) | Add the footer sheet under the rack |

## Priority 3 — visual
| # | Item | Restore |
|---|---|---|
| V1 | "Sheet" boxes (the web's `.sheet`: dark rounded box, h3 headings in lowercase legend font, `.note` text) | A `WebSheet` view with the same padding, radius, colours and heading style |
| V2 | Tables (`table.matrix`: thin row lines, small caps headers) | A `WebTable` style for the matrix, mappings, presets and reference |
| V3 | Buttons (`.tbtn`) and fields (`.tfield`) everywhere | Use `LampButton` and a `WebField` (label + dark inset) instead of system buttons, pickers and text fields |
| V4 | Lowercase legends and headings, as on the web | Match the text casing |
| V5 | Room background and cabinet spacing on every screen | Same as the rack |

## Priority 4 — refinement (after parity)
Native improvements to propose one by one *after* parity, each needing your approval: haptics on detents, smoother knob drag, VoiceOver rotor for jacks, Dynamic Type in sheets.

## Decisions needed (native features the web does not have)
| # | Native-only item | Recommendation |
|---|---|---|
| D1 | **Patch canvas** screen (movable module tiles) | Remove from navigation. The web patches on the rack only. |
| D2 | **Diagnostics** screen | Remove from navigation. Keep the CPU readout in the top bar (web has it). |
| D3 | **Perform** and **Mixer** screens | Remove. Their controls already exist on the rack and in the dock. |
| D4 | Preset favourites, tags, recents, search (SwiftData) | Remove for parity, or approve as a later Priority 4 addition. |
| D5 | Web-only parts with iOS equivalents: "connect midi devices" + input/output menus; profile JSON text box | iOS gets MIDI devices from the system automatically, so the panel shows the sources list in the same place. For the profile, copy/paste JSON works on iOS too, so keep the web's text box. |
| D6 | App-only settings (Keep Screen Awake, Background Audio) and the recording format/rate choices | Put them in the about tab's performance sheet next to the web's settings (the web can't have them). |
| D7 | Window buttons (open a screen in its own window, Stage Manager) | Remove (web has none). |

## Status checklist
| Screen | Web reference | Native status | Restoration | Verified |
|---|---|---|---|---|
| Top bar | ✅ source | ⚠️ power, meter missing | ⬜ | ⬜ |
| Navigation (tabs) | ✅ | ❌ sidebar / tab bar | ⬜ | ⬜ |
| Rack | ✅ | ✅ matches (footer missing) | ⬜ footer | ⬜ |
| Sequencer | ✅ | ❌ | ⬜ | ⬜ |
| Arp · gen | ✅ | ❌ split, generic | ⬜ | ⬜ |
| Effects | ✅ | ❌ sliders | ⬜ | ⬜ |
| Matrix | ✅ | ❌ sliders/toggles | ⬜ | ⬜ |
| MIDI | ✅ | ❌ generic | ⬜ | ⬜ |
| Presets | ✅ | ❌ Form/library | ⬜ | ⬜ |
| Manual | ✅ | ⚠️ sheet not tab | ⬜ | ⬜ |
| About | ✅ | ❌ Settings Form | ⬜ | ⬜ |
| Keyboard dock | ✅ | ❌ keys only | ⬜ | ⬜ |

Verification per screen: web-vs-native screenshot pair (simulator), every control moves its parameter (DSP unaffected), and VoiceOver labels intact.
