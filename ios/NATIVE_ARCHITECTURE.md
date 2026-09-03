# Native mobile architecture

The mobile product keeps the existing Svelte interface but removes the
Emscripten runtime. Web content is a presentation layer only; tracker state,
rendering, audio, MIDI and storage are native C++ services.

## Shared boundary

```text
Svelte UI (iOS WKWebView / Android WebView)
  - responsive layout, controls, settings
  - sends semantic TrackerAction events
  - paints indexed framebuffer updates into its 240x240 canvas
                 |
        platform message bridge
                 |
C++ NullPerator core
  - Ui2TrackerApplication and UI2 renderer
  - player, project model and persistence
  - platform-neutral service contracts
                 |
      iOS adapters / Android adapters
```

The tracker screen crosses the platform bridge as compact, palette-indexed
dirty regions. The first update contains the complete 240x240 frame; later
updates normally contain only changed 8x8 tile runs. Svelte expands those
indices into its existing canvas, so screen scale, orientation and modal
stacking remain ordinary DOM layout and require no native overlay geometry.
Android can reuse the same versioned frame-packet contract through JNI.

## iOS adapters

- Objective-C++ owns the C++ runtime and its single mutation/render thread.
- A native serial timer advances the core and fills audio buffers independently
  of WKWebView animation frames, including during declared background audio.
- CoreAudio/RemoteIO consumes a lock-free buffer filled by the C++ mixer.
- RemoteIO captures the current system-selected iOS input directly into the
  native recording adapter. The recording screen therefore does not expose an
  input picker on iOS; WAV persistence runs off the realtime audio thread.
- CoreMIDI supplies selected input and drains selected output through the same
  bounded C++ MIDI graph used by the firmware.
- The C++ filesystem adapter uses POSIX paths rooted in the app's Documents
  directory. There is no FAT, IDBFS or browser mirror.
- UIKit/GameController and Svelte controls both publish the same semantic
  `TrackerAction` values.
- The bundled Svelte build is served from the app bundle by a private
  `nullperator://app` `WKURLSchemeHandler`. This gives ES modules a proper
  origin without a loopback HTTP server, WebAssembly, pthread bootstrap or
  AudioWorklet.

## Migration sequence

1. Prove that production UI2 sources compile and render inside the iOS target.
2. Load the existing Svelte shell without starting the WASM runtime.
3. Route Svelte controls to a minimal native runtime and feed native dirty
   framebuffer packets into the DOM canvas.
4. Install POSIX filesystem and lifecycle services, then boot the complete
   `Ui2TrackerApplication`.
5. Add native MIDI, controller and background/foreground recovery.
6. Reuse the same Svelte/native-core contract for Android through JNI.

Steps 1–5 and native playback are implemented. The host now boots the complete
`Ui2TrackerApplication`, restores projects directly from Documents through
POSIX, renders the production UI2 framebuffer, and feeds RemoteIO from the C++
mixer. CoreMIDI routes through the native firmware MIDI service, and recording
uses the active iOS input route through RemoteIO. The Svelte layout and native
bridge boundary remain shared with future mobile hosts.
