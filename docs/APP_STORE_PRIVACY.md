# App Store privacy disclosures and privacy manifest (Phase 10)

## App Privacy ("nutrition label") answers

- **Data collection: "Data Not Collected."** Vektur Modular and its AUv3 extension collect no data. They have no accounts, analytics, crash-reporting SDK, advertising or third-party SDKs. Apple's own opt-in crash reports are not counted as the developer's collection.
- **Tracking: No.** There are no tracking domains and no IDFA, and the App Tracking Transparency prompt is not used.

## Privacy manifests

`App/PrivacyInfo.xcprivacy` and `AUExtension/PrivacyInfo.xcprivacy` are identical:

| Key | Value |
|---|---|
| NSPrivacyTracking | false |
| NSPrivacyTrackingDomains | — |
| NSPrivacyCollectedDataTypes | — |
| UserDefaults | **CA92.1**. Settings read and written only by this app and extension. The shared app group holds the IR library and global MIDI map, written by the same developer's app and extension. |
| System boot time | **35F9.1**. Elapsed time for audio timing, DSP load and the Ableton Link clock (`mach_absolute_time`, `steady_clock`); never sent off the device. |
| File timestamp | **C617.1**. Creation dates of files in the app's own container, used to sort the Recordings list. |

The disk-space and active-keyboard APIs are not used.

## Permission prompts (Info.plist)

- **NSLocalNetworkUsageDescription.** Ableton Link, standalone app only.
- There are no microphone, camera, location, contacts, photos or motion prompts. CoreMotion device-motion, used for the tilt highlights, needs no permission.

## Third-party code

None is linked by default. Ableton LinkKit is optional: if it is added, it uses the local network only and collects nothing. List it in the acknowledgements and check its own privacy manifest.
