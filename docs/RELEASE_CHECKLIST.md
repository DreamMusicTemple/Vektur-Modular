# Release and compliance checklist — 1.0.0 (Phase 10)

## Build
- [ ] **Xcode 27 or later, iOS 27 / macOS 27 SDK.** This is required for TestFlight and the App Store, and is enforced: `tools/check_sdk.sh` runs as a pre-build phase in the app and the AUv3 target, and fails any Release build made with an older SDK. `project.yml` sets `xcodeVersion: "27.0"`. The deployment targets stay at iOS 17 and macOS 14.
- [ ] `xcodegen generate`; set `DEVELOPMENT_TEAM` and the bundle prefix in `Config/Shared.xcconfig` (the prefix is permanent after the first upload).
- [ ] Register the app group `group.com.dreammusictemple.vekturmodular` for the team.
- [ ] Versions: `MARKETING_VERSION` 1.0.0; AU component version **65536**, which must equal `AudioComponentInfo.componentVersion`. Raise the build number for every upload.
- [ ] `swift test` in `Packages/VekturKit`, which runs VekturCore, VekturDSP (including the vDSP FFT self-test) and the AU tests.
- [ ] `DSPTests`: all 10 suites in Release, ASan + UBSan and TSan (`cmake -DVK_SANITIZE=ON` / `-DVK_TSAN=ON`).
- [ ] `auval -v aumu VkMd DMTm` passes (`tools/auval.sh`) on macOS.

## Store metadata
- [ ] App Privacy: "Data Not Collected" (docs/APP_STORE_PRIVACY.md).
- [ ] Accessibility labels per docs/ACCESSIBILITY.md.
- [ ] Category: Music. Age rating: 4+. Export compliance: `ITSAppUsesNonExemptEncryption = false`.
- [ ] Screenshots: iPhone 6.9″, iPad 13″, Mac. The DMT Cymatics logo may appear; no third-party marks.
- [ ] Description and keywords use generic terms only. The reference hardware is named only in the legal disclaimer (`ProductInfo.legalDisclaimer`), with counsel review for each market.
- [ ] Support URL DreamMusicTemple.com, support e-mail DMTcymatics@humanoid.net, and a privacy-policy URL stating that no data is collected.

## IP and legal
- [ ] No manufacturer logos, wordmarks or trade dress copied on any panel (Phase 8 design review).
- [ ] Factory presets are original sounds with original names.
- [ ] Ableton Link, if included: licence signed, acknowledgement in About.

## Functional (on device)
- [ ] DAW matrix: Logic, GarageBand, AUM, Cubasis, Loopy Pro, Ableton Live (macOS) — docs/DAW_TEST_MATRIX.md.
- [ ] Program Change: banks 0/1 factory, bank 2 user.
- [ ] Recording: WAV, AIFF and CAF at 16/24/32f, at the session rate and at 44.1 and 96 kHz conversion; a 10-minute take with no dropped frames.
- [ ] Background Audio on and off; Keep Screen Awake; Battery Saving restores the quality settings.
- [ ] Performance table in docs/PERFORMANCE.md filled in for three devices.
