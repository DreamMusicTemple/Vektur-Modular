# Accessibility review (Phase 10)

Reviewed against Apple's Human Interface Guidelines (Accessibility) and the App Store accessibility "nutrition labels" (VoiceOver, Voice Control, Larger Text, Dark Interface, Differentiate Without Colour, Sufficient Contrast, Reduced Motion).

| Area | Result | Notes |
|---|---|---|
| VoiceOver: controls | ✅ | Knobs, faders and switches are adjustable elements with a label (module + control), value (with units) and increment/decrement. Double-tap resets to the default. |
| VoiceOver: patching | ✅ | Jacks are buttons ("VCO 1, output, saw"). The jack menu (remove, level, invert, colour, patch from here, normal source) is in the accessibility actions (contextMenu). Tapping two jacks connects them without dragging. |
| VoiceOver: screens | ✅ | Section headers carry `.isHeader`. The manual's chapter headings are headers. Preset rows combine name, category and tags, and the favourite button has its own label. |
| Voice Control | ✅ | Every button has a visible or accessibility label; there are no icon-only controls without a label. |
| Larger Text | ⚠️ partial | Forms (Presets, Settings, MIDI, Manual, MIDI reference, Recorder) follow Dynamic Type. Panel legends use fixed sizes to keep the hardware layout. VoiceOver and the Large Content Viewer still read them. Declared in the App Store label as supported "on most screens". |
| Dark Interface | ✅ | The app is dark by design (`preferredColorScheme(.dark)`). |
| Differentiate Without Colour | ✅ | Cable colour is never the only information: jack names, LEDs plus text, the Clip LED plus "clip" in the meter label. |
| Sufficient Contrast | ✅ | White legends on near-black: contrast 15:1. Amber value text on black: 9.6:1. Worn legends never fall below 4.5:1, because wear is capped for legibility (PanelLayout tests). |
| Reduced Motion | ✅ | Cable sway, spring drop-in, flow dashes and tilt highlights are off with Reduce Motion (and with Battery Saving). |
| Keyboard (iPad, Mac) | ✅ | ⌘? opens help, ⇧⌘R records, and the computer keyboard plays notes on the web build. All controls can be focused. |
| Haptics / audio cues | n/a | No information is carried by sound alone, other than the instrument's own output. |

**Open items for 1.1:** Dynamic Type for panel legends (a scalable panel layout), and VoiceOver rotor actions for "next jack" and "previous jack".
