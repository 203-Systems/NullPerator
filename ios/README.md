# NullPerator iOS

The iOS target runs the tracker as native C++. Svelte remains the shared
presentation layer and paints native framebuffer packets into its canvas; the
app bundle contains no WebAssembly runtime, FAT filesystem image, IDBFS mirror,
loopback HTTP server, or AudioWorklet.

## Build

1. Install WebUI dependencies in `web/`.
2. Package the shared presentation assets with `ios/scripts/package-web.sh`.
3. Open `ios/NullPeratorIOS.xcodeproj`, select an Apple Developer Team, choose
   the `NullPeratorIOS` scheme, and run.

The Xcode build phase compiles the native C++ library for the active SDK and
architecture before linking the application. The bundle identifier is
`io.203systems.nullperator`.

The release version is owned by `sources/ProductVersion.h`. The native-core
build phase derives the iOS `CFBundleShortVersionString` from that value, so the
C++ UI, native settings bridge and signed app bundle cannot drift apart.

## Runtime architecture

```text
Svelte UI in WKWebView
  - responsive layout, controls and settings
  - sends semantic TrackerAction events
  - paints the 240×240 indexed framebuffer
                 |
        native message bridge
                 |
C++ NullPerator core
  - Ui2TrackerApplication and UI2 renderer
  - player, project model and persistence
  - platform-neutral service contracts
                 |
             iOS adapters
```

The tracker screen crosses the bridge as palette-indexed dirty regions. The
first update contains the complete frame; later updates normally contain only
changed 8×8 tile runs. Svelte expands those indices into its canvas, leaving
screen layout and modal presentation in the shared WebUI.

- SwiftUI owns application lifecycle and hosts the bundled Svelte presentation
  through a private `nullperator://app` URL scheme.
- The host injects a native-core capability before the page loads. The shared
  WebUI selects the native runtime from that capability; it has no iOS URL mode.
- Objective-C++ owns the C++ runtime, framebuffer bridge, semantic input and
  native audio lifecycle.
- C++ accesses the app's Documents directory through POSIX APIs. There is no
  browser filesystem synchronization layer.
- CoreMIDI, GameController, the device battery and Files app integration are
  supplied by native platform adapters.
- Recording uses the current system-selected iOS input route through RemoteIO;
  iOS does not show the firmware input-source selector.
- iPhone and iPad portrait and landscape orientations are enabled.
