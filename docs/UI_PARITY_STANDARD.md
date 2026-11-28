CRITICAL UI/UX RESTORATION — NATIVE iOS MUST MATCH THE ESTABLISHED WEB VERSION

Project Objective

We are developing the native iOS version of this application for TestFlight. However, the current native implementation has diverged considerably from the established web version.

This divergence is unacceptable and must be corrected.

The existing web version is the established, approved and definitive UI/UX reference for this application. It already contains the design, layouts, controls, navigation, visual hierarchy, component placement and workflow that I want.

The native iOS application is intended to deliver improved performance through native architecture, optimised DSP, C++ processing, lower latency, improved efficiency and better integration with iOS.

It is NOT intended to redesign, reinterpret, simplify, modernise or replace the existing interface.

⸻

1. THE WEB VERSION IS THE MASTER DESIGN

Treat the current established web application as the authoritative visual and functional blueprint.

The native iOS version must faithfully reproduce the web application’s:

* Screen layouts
* Component positioning
* Control sizes and proportions
* Rotary dial controls
* Knobs and their visual appearance
* Slider placement (only where sliders already exist in the web version)
* Buttons
* Switches
* Dropdowns
* Menus
* Tab arrangements
* Navigation structure
* Colour schemes
* Backgrounds
* Panels
* Borders
* Spacing
* Control groupings
* Parameter organisation
* Page structure
* User workflows
* Visual hierarchy
* Interaction behaviour
* Overall character and identity

Pixel-perfect reproduction is not necessarily required. Fonts, rendering differences and minor spacing variations inherent to native iOS are acceptable.

However, the interface must be recognisably the same application, with the same controls in the same places and the same overall layout.

⸻

2. DO NOT SUBSTITUTE CONTROLS

This is particularly important.

If the web version uses a rotary dial, the native version must use a rotary dial.

Do NOT replace rotary dials with:

* Horizontal sliders
* Vertical sliders
* iOS default sliders
* Numeric entry fields
* Generic SwiftUI controls
* Alternative control designs

If the web version uses a specific knob design, recreate that design in native iOS.

The native implementation should use custom native controls where necessary, including custom rotary controls, rather than default Apple UI components that change the appearance of the instrument.

The underlying control technology may differ, but the user-facing appearance and behaviour must remain faithful.

⸻

3. DO NOT INVENT, ADD OR REMOVE MENUS

Do not add new menus, settings panels, navigation systems, popovers, toolbars, tabs or control groups simply because they seem more appropriate for a native iOS application.

Do not reorganise existing features.

Do not move controls to different locations.

Do not introduce additional layers of navigation.

Do not simplify the existing interface by hiding functionality behind new menus.

Do not change the workflow.

Do not remove functionality to make the native interface easier to implement.

If the web version has a control visible in a particular location, the native version should have that control in the equivalent location.

If the web version has a particular menu or panel, reproduce it as it exists.

If a native iOS limitation requires a change, identify the limitation and explain it before implementing a workaround.

⸻

4. SCREENSHOTS WILL BE PROVIDED AS DESIGN REFERENCES

I will provide screenshots of the established web version, potentially including:

* Individual pages
* Full screen layouts
* Specific panels
* Synthesiser sections
* Drum machine sections
* FX sections
* Settings
* Modulation pages
* Menus
* Specific controls
* Detailed component arrangements

Treat these screenshots as visual specifications.

Use them to compare the current native implementation against the web version and systematically identify discrepancies.

When screenshots are provided:

1. Identify the corresponding native screen or component.
2. Examine the screenshot carefully.
3. Compare the current native implementation against it.
4. List the differences.
5. Identify the relevant native source files and UI components.
6. Correct the differences.
7. Preserve existing functionality.
8. Verify that the changes have not broken audio, DSP, MIDI, parameter control or application state.

Do not guess what the web interface looks like when a screenshot or existing source code is available.

⸻

5. PRESERVE THE NATIVE DSP AND PERFORMANCE ADVANTAGES

The reason for developing a native version is to obtain genuine technical advantages.

Preserve and improve:

* Native DSP processing
* C++ optimisation where appropriate
* Audio performance
* Low-latency processing
* CPU efficiency
* Memory efficiency
* Stable audio rendering
* Oscillator performance
* Voice management
* Polyphony
* Envelopes
* Filters
* Modulation
* Effects
* MIDI
* Audio routing
* Parameter automation
* AUv3 integration where applicable
* iOS integration
* Thread safety
* Audio-thread stability

UI restoration must not be achieved by replacing, weakening or removing the native DSP architecture.

Keep the visual interface and DSP architecture properly separated.

The UI should communicate with the native audio engine through the existing parameter architecture, bridges, parameter trees or other established mechanisms.

The UI must not introduce unnecessary allocations, locks, file operations or expensive processing into the real-time audio thread.

⸻

6. DO NOT DAMAGE EXISTING FUNCTIONALITY

This is a restoration and alignment task, not an excuse to rebuild the application from scratch.

Before modifying anything:

* Audit the existing native UI.
* Audit the established web UI.
* Identify reusable assets.
* Identify existing custom controls.
* Identify discrepancies.
* Identify any functionality that is missing from the native implementation.
* Determine which changes can be made without affecting DSP or audio architecture.

Avoid unnecessary rewrites.

Do not delete functional code simply because it is easier to replace it.

Do not introduce placeholder controls.

Do not create controls that look correct but are disconnected from the audio engine.

Every functional control must continue to work.

⸻

7. ESTABLISH A SCREEN-BY-SCREEN RESTORATION PROCESS

We will progressively align the native app with the established web version using screenshots.

Create a UI parity checklist covering every major screen and section.

For each screen, record:

* Web reference status
* Native current status
* Missing controls
* Incorrect controls
* Incorrect positioning
* Incorrect layout
* Incorrect navigation
* Incorrect menus
* Functional discrepancies
* Restoration status
* Verification status

Use a prioritised workflow:

Priority 1 — Major structural discrepancies

* Incorrect screen layouts
* Missing panels
* Incorrect navigation
* Added menus
* Missing functionality

Priority 2 — Control discrepancies

* Rotary knobs replaced by sliders
* Missing controls
* Incorrect control types
* Incorrect groupings
* Incorrect parameter arrangements

Priority 3 — Visual discrepancies

* Positioning
* Sizing
* Spacing
* Colours
* Borders
* Proportions
* Backgrounds

Priority 4 — Refinement

* Animations
* Touch behaviour
* Fine visual adjustments
* Native rendering improvements

⸻

8. DEVELOPMENT RULE: NO CREATIVE UI INTERPRETATION

I do not want you to make creative decisions about the user interface.

The UI design decisions have already been made in the established web version.

Your job is to faithfully translate that interface into native iOS while maintaining the native application’s technical advantages.

If you believe a design change would genuinely improve usability, do not implement it automatically.

Instead:

1. Explain the proposed change.
2. Explain why you believe it is necessary.
3. Show how it differs from the web version.
4. Wait for my approval before implementing it.

The default decision must always be to reproduce the web version.

⸻

9. START WITH AN AUDIT — DO NOT IMMEDIATELY REWRITE

First, inspect the repository and current native implementation.

Identify the current state of the project and establish:

* Which screens already match the web version?
* Which screens are substantially different?
* Where have rotary controls been replaced with sliders?
* Which menus or navigation structures have been unnecessarily changed?
* Which controls have been moved?
* Which features are missing?
* Which features have been added that do not exist in the web version?
* Which UI components are reusable?
* Which existing components should be restored?
* What files will need modification?

Produce a clear discrepancy report and restoration plan before undertaking substantial changes.

Do not halt development indefinitely waiting for screenshots if the existing web source code, assets, components or repository already contain sufficient information to restore a screen.

Use all available reference material first.

⸻

10. ACCEPTANCE CRITERIA

The native iOS version will not be considered visually acceptable merely because it compiles, runs or passes TestFlight validation.

It must satisfy these requirements:

* The interface is recognisably the established web version.
* The original layouts have been retained.
* Rotary dials remain rotary dials.
* Controls are in their original relative positions.
* Menus and navigation remain consistent with the web version.
* No unapproved menus or redesigns have been introduced.
* All existing features are preserved.
* Controls genuinely operate the underlying native DSP.
* Audio performance and native optimisation remain intact.
* The interface is adapted to native iOS rendering without compromising the established design.

The web application is the blueprint.

Native iOS is the performance-enhanced implementation of that blueprint — not a different product.

Begin by auditing the current repository and producing the UI discrepancy report. I will then provide screenshots of the established web version, screen by screen, so that we can systematically restore UI parity and bring the native TestFlight build to an acceptable standard.