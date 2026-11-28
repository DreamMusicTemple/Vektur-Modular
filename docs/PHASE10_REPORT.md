# Phase 10 Report — Optimisation and release preparation

Date: 2026-11-27 · **Build 1.0.0 · AU version 65536** · State schema v1 (parameter 737, `global.oscillatorQuality`, added at the end — no renumbering)

## Summary
- **HQ oscillator option.** The VCOs run at 2× through the existing half-band oversampler. Alias energy falls by 7–10 dB on high notes, with the level unchanged. Cost: about 27 % more engine CPU at 16 voices.
- **A real bug fixed.** The oversampler relied on the order in which C++ evaluates function arguments. That order is unspecified, so HQ and the 2×/4× filters were wrong under some compilers. The sanitizer builds exposed it.
- **vDSP FFT** on Apple platforms, with a self-test API that checks either FFT path against a direct DFT.
- **Settings.** Battery Saving Mode, Keep Screen Awake, Background Audio, oscillator and filter quality, and tilt-reactive panel highlights (CoreMotion).
- **Profiling.** Method, targets and reference measurements (`docs/PERFORMANCE.md`); benchmark mode in `test_phase10 --bench`.
- **Release documents.** Accessibility review, privacy manifests for the app and the AUv3, App Store privacy answers, and the release checklist.
- **TestFlight builds require the iOS 27 / macOS 27 SDK** (Xcode 27+). A Release build phase in both targets enforces this.
- **Native look and identity matched to the web build, and improved.**
  - The aluminium top bar (DMT Cymatics logo, VEKTUR MODULAR wordmark, play, rec with a timer, tempo, wear, engine-load and live MIDI/chord readouts, panic, keyboard, help) is shared by the app and the AUv3.
  - The app icon, launch screen and amber accent all come from the logo and the panel palette.
  - The sidebar and tabs use the same 13 screens.

## Measurements
These are the reference host figures; see PERFORMANCE.md.

| Configuration | Share of one core |
|---|---:|
| 8 voices | 6.1 % |
| 16 voices | 12.2 % |
| 16 voices, HQ | 15.5 % |
| 16 voices, filters 4× | 17.2 % |
| 32 voices | 23.0 % |

- **Memory.** About 21 MB peak for the whole test process with 32 voices and all modules.
- **Latency.** 0 samples added by default. Look-ahead effects report their latency to the host and every path is compensated.
- **HQ aliasing.** On a held saw, note 96 improves from −39.4 to −46.7 dB and note 103 from −36.9 to −46.4 dB. The level difference at C4 is 0.00 dB.
- **FFT error against a direct DFT** (portable path): 1.5e-7 at 64 points, 3.0e-7 at 512, 3.6e-7 at 2048.

## Decisions (honest scope)
- **SIMD across voices is deferred to 1.1.** Every voice runs a fully patchable graph, so lane-parallel voices would mean rewriting every module kernel and the scheduler. Measured headroom does not justify that risk for 1.0. The plan (hot kernels across voices that share the normalled graph, with a scalar fallback) is in PERFORMANCE.md.
- **Per-voice swap fade is deferred to 1.1.** The whole-graph 1.3 ms crossfade is click-free today. A per-voice fade only matters once voices can hold different graphs.
- **Battery Saving is a UI and quality profile,** not a hidden reduction in voices. It does exactly what Settings says, and the quality settings are restored when it is turned off.

## Settings
- **VekturUI** stores the app-only settings (Keep Screen Awake, Background Audio) and shows them only in the standalone app. The app target applies them: `UIApplication.isIdleTimerDisabled` on iOS, a display-sleep activity on macOS, and `StandaloneAudioEngine.backgroundAudioEnabled`. The AUv3 never touches UIApplication.
- **Tilt highlights.** `TiltMotion` reads device gravity at 20 Hz and drives a soft radial highlight on the painted panels. It is off with Reduce Motion, with Battery Saving, or when the appearance setting is off. No permission prompt is needed.
- **Telemetry.** One shared, reference-counted `TelemetryModel` per kernel, because the telemetry queue has a single consumer. Likewise, one `RecordingService` per audio unit.

## Release readiness
- **Privacy.** `App/PrivacyInfo.xcprivacy` and `AUExtension/PrivacyInfo.xcprivacy`: no tracking and no collected data. Required-reason APIs: UserDefaults (CA92.1), system boot time (35F9.1) and file timestamps (C617.1). The App Store answer is "Data Not Collected".
- **Project.**
  - `project.yml` sets `xcodeVersion: "27.0"` and the AppIcon/AccentColor asset names, and adds the document types for `.vekturpreset`, `.vekturproject` and `.vekturpattern`.
  - It turns on `UIFileSharingEnabled` (so Recordings appear in Files) and the launch screen.
  - The pre-build phase `tools/check_sdk.sh` fails Release builds made with an SDK older than 27. The script is declared as an input because script sandboxing is on.
- **Versions.** MARKETING_VERSION 1.0.0; the AU component version 65536 is the same in `project.yml` and `AudioComponentInfo`.

## Test results
All 10 C++ suites pass in Release, ASan + UBSan and TSan. The web tests pass: Node 6/6 and headless Chrome 12/12. The Swift tests are written but not compiled here.

## After 1.0.0: keyboard ctrl pop-up (2026-11-28)
- **Schema.** `tools/panels.py` panels can set `popup=True`. The panel then keeps only its sliders and jacks on the front, and a **settings ▸** button opens the knobs and switches in a pop-up window. The keyboard ctrl panel uses it, and is now 4 hp wide. Older Panels.json files without the key still decode (tested).
- **Web.** `<dialog class="pop">` built inside the panel, so the existing control wiring is unchanged. Tested in headless Chrome: the front has no knobs, the button opens the window, a knob in the window changes the parameter, and both *done* and a tap outside close it.
- **App and AUv3.** `PanelPopup.swift` reproduces the web dialog on every platform: a centred window over a dimmed, lightly blurred screen. The title, the knob row and the switch row are each wrapped and centred with the web spacing (22/14 and 18/12 pt), followed by *done*. The window shrinks to fit its controls, up to 560 pt. It closes on *done*, on a tap outside, or with Escape. It is hosted by MainView above the top bar and the keyboard, as the web dialog is.

## Pending
- **Compile on Xcode 27 and run on devices**, then fill in the device table in PERFORMANCE.md and work through RELEASE_CHECKLIST.md.
- **Queued (not started, at the user's request):** a choice of dial appearances, as in Vektur Vortex.
