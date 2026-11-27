# Getting Vektur Modular onto TestFlight (no Mac needed)

GitHub builds, signs and uploads the app for you
(`.github/workflows/testflight.yml`, on GitHub's Xcode 27 image). You do the
one-time Apple account setup below, about 20 minutes, in a browser. An iPad or
iPhone is enough.

## 1. Find your Team ID
1. Go to developer.apple.com/account and choose **Membership details**.
2. Copy the **Team ID** (10 characters, for example `A1B2C3D4E5`).

## 2. Check your App Store Connect API key
You already have a `.p8` key file.
1. In App Store Connect, open **Users and Access → Integrations → App Store Connect API → Team Keys**.
2. Find your key and check that its **Access** is **Admin**. Admin lets the build create its own signing certificate and profiles. If the key is not Admin, generate a new key with Admin access and download it. Apple lets you download a key only once.
3. Copy the **Key ID** for your key, and the **Issuer ID** shown above the list.

## 3. Register the identifiers
Go to developer.apple.com/account → **Certificates, Identifiers & Profiles → Identifiers**.

1. **+ → App Groups → Continue.**
   - Description: `Vektur Modular`
   - Identifier: `group.com.dmtcymatics.vekturmodular`
2. **+ → App IDs → App → Continue.**
   - Description: `Vektur Modular`
   - Bundle ID: Explicit, `com.dmtcymatics.vekturmodular`
   - Under Capabilities, tick **App Groups**, then *Configure* and choose the group from step 1.
   - Continue, then Register.
3. Repeat step 2 for the plug-in:
   - Description: `Vektur Modular AU`
   - Bundle ID: `com.dmtcymatics.vekturmodular.au`
   - Tick **App Groups** and choose the same group.

The bundle ID prefix `com.dmtcymatics` is permanent once the first build is uploaded. To use a different prefix, change it now in `Config/Shared.xcconfig` (`VK_BUNDLE_ID_PREFIX`) and in the app group name in `project.yml`, and tell me so the two stay consistent.

## 4. Create the app record
In App Store Connect, go to **Apps → + → New App**.
- **Platforms:** iOS. Also tick macOS if you want the Mac version.
- **Name:** `Vektur Modular`. Names must be unique on the App Store; if it is taken, try `Vektur Modular Synth`. This name can differ from the app's on-device name.
- **Primary language:** English (U.K.) or English (U.S.).
- **Bundle ID:** `com.dmtcymatics.vekturmodular`
- **SKU:** `VEKTURMODULAR`
- **User access:** Full access

## 5. Add four secrets to GitHub
In the repository DreamMusicTemple/Vektur-Modular, go to **Settings → Secrets and variables → Actions → New repository secret**.

| Name | Value |
|---|---|
| `ASC_KEY_ID` | the Key ID from step 2 |
| `ASC_ISSUER_ID` | the Issuer ID from step 2 |
| `ASC_PRIVATE_KEY` | open the `.p8` file in a text editor and paste all of it, including the `-----BEGIN PRIVATE KEY-----` and `-----END PRIVATE KEY-----` lines |
| `APPLE_TEAM_ID` | the Team ID from step 1 |

These are the same values as your Vektur Vortex repo uses, but secrets don't copy between repositories, so add them here too.

## 6. Run the build
1. In the repository, open **Actions → TestFlight → Run workflow**.
2. Choose **iOS**, then **Run workflow**.
   - The first time, also tick **Skip the test job**. That gets you to the compile step sooner. This is the project's first build with Swift, so compile errors are likely.
   - If a step fails, open the run, download **logs-iOS-N** from the Artifacts section, and send me the `archive.log` (or a screenshot of the red step). I'll fix it and you run the build again.
3. A green run ends with "Uploaded iOS build N". About 5–30 minutes later the build appears in App Store Connect → your app → **TestFlight**.

## 7. Install it
1. In App Store Connect → TestFlight, go to **Internal Testing → +**, create a group (for example "Me"), and add yourself. Your Apple ID must be a user on the team.
2. Install the **TestFlight** app on your iPad or iPhone and accept the invitation.
3. Encryption is already declared in the app (`ITSAppUsesNonExemptEncryption = NO`), so TestFlight won't ask the export-compliance question.

Each later build: Actions → TestFlight → Run workflow. The build number rises on its own.
