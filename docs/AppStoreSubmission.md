# NullPerator App Store submission draft

Last updated: 2026-08-30

This file is a working submission sheet. Copy the approved values into App Store Connect before TestFlight or App Review submission.

## Build identity

- App name: `NullPerator`
- Bundle ID: `io.nullperator.client`
- Version: `1.0`
- Build: `1`
- Primary category: Music
- Secondary category: Utilities (optional)
- Copyright: `© 2026 203 Electronics LLC`
- Export compliance: The app does not use non-exempt encryption (`ITSAppUsesNonExemptEncryption = false`). Reconfirm this if networking or cryptography is added.

## Store copy (English draft)

### Subtitle (30 characters maximum)

`A pocket music tracker`

### Promotional text

`Create and perform tracker-based music on iPhone and iPad with touch controls, game controllers, MIDI, and local project files.`

### Description

`NullPerator brings the focused tracker workflow to iPhone and iPad.`

`Compose patterns, arrange songs, and play them back through a compact interface designed around the NullPerator hardware. Use the on-screen controls or connect a compatible game controller. Route MIDI, keep projects in the Files app, and continue working without an account.`

`Features:`

`• Tracker composition and playback`

`• Touch controls optimized for portrait and landscape layouts`

`• Compatible game controller input with configurable routing`

`• MIDI routing, including supported Bluetooth MIDI devices`

`• Local project and sample storage through the Files app`

`• iPhone and iPad support`

`Your projects and settings stay on your device. NullPerator does not include advertising, analytics, or account tracking.`

### Keywords (100 characters maximum)

`tracker,music,sequencer,chiptune,MIDI,DAW,sampler,composer,gamepad,offline`

## URLs

- Support URL: `https://203.io/policies/contact-information`
- Marketing URL: `https://203.io/products/operator-deposit`
- Privacy Policy URL: `https://203.io/pages/nullperator-privacy-policy`

The app also contains an offline, in-app privacy policy under Settings > Privacy Policy. Keep the public policy consistent with that text.

## App Privacy answers (current implementation)

- Data collection: `Data Not Collected`
- Tracking: `No`
- Advertising identifier: Not used
- Analytics: Not used
- Accounts/sign-in: Not used
- Privacy manifest: Included in the app bundle

Re-audit these answers whenever analytics, crash reporting, cloud sync, accounts, or a third-party SDK is introduced.

## Review notes (English draft)

`NullPerator is a self-contained music tracker. Its tracker engine, audio, MIDI, persistence, and framebuffer renderer run as bundled native C++; its bundled Svelte presentation does not download or execute remote app code. Native iOS integrations provide audio playback, Files access, game-controller input, MIDI/Bluetooth MIDI routing, device battery display, and orientation-aware layouts.`

`No account or network connection is required. Projects can be created and loaded from the app's local Documents directory through the in-app browser or the iOS Files app.`

`The “Purchase NullPerator Hardware” item opens an external web page for an optional physical hardware product. It does not unlock app features or sell digital content.`

`Suggested review path: launch the app, press PLAY to start playback, open Settings with the square menu button, and inspect Controller, MIDI, Files, Privacy Policy, Software Version, and Reboot.`

## Assets and App Store Connect work still required

- Confirm the public privacy-policy URL remains accessible and matches the current app behavior.
- Provide final localized name, subtitle, description, keywords, and promotional text.
- Capture current iPhone screenshots and iPad screenshots because the binary supports both device families.
- Complete the current age-rating questionnaire.
- Choose price, availability regions, and release method.
- Complete App Privacy and accessibility declarations.
- Confirm content rights and third-party/open-source license compliance.
- Complete Digital Services Act trader status if distributing in the European Union.
- Add an Apple Distribution certificate and App Store provisioning/profile, then export or upload the archive.
- Upload to TestFlight and complete a real-device smoke test before App Review.

## Release smoke test

- Fresh install launches without a layout jump or clipped controls.
- Portrait and landscape layouts keep the tracker screen and all controls visible.
- Touch direction, Option, Enter, Shift, Play, and Settings controls respond and do not trigger text selection or the magnifier.
- Connected controller routes D-pad, A/B, Start, and Select correctly; auto-hide persists across relaunches.
- Audio plays while the Ring/Silent switch is silent, subject to the selected audio route and system media volume.
- Playback updates the live-note view.
- Save creates a visible project file; Load can enter the SD folder and navigate back with `..`.
- Files opens the app's Documents folder.
- MIDI route map lists available routes; Bluetooth MIDI opens its native picker.
- Battery icon reflects the real device level and charging state.
- Privacy Policy and hardware purchase links open successfully.
