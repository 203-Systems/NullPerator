# PicoTracker

PicoTracker is an eight-track music tracker with a compact, controller-driven
workflow. This repository contains three supported products:

- **Node firmware** for the ESP32-S3 hardware target.
- **WASM workbench** for running the same C++ engine and UI2 in a browser.
- **NullPerator for iOS**, which runs the C++ engine natively behind the shared
  Svelte presentation on iPhone and iPad.

All three products use the 240×240 UI2 renderer. The retired hardware targets and
their legacy character UI are not part of this repository.

## Features

- 8 song tracks
- 256 chains, 128 phrases, and 32 tables
- Sample, MIDI, SID, OPAL, and Macro instruments
- 44.1 kHz stereo engine
- Project autosave, browser-based project storage, sample import, and render
- Fixed-capacity UI and application state suitable for embedded use

## Node firmware

Node uses ESP-IDF and targets ESP32-S3. See [README_NODE.MD](README_NODE.MD) for
build, flash, and monitor commands.

## WASM workbench

The static browser workbench includes persistent files, optional host-folder
synchronization, Web MIDI, logs, and performance tracing.

- [Build](docs/wasm/BUILD.md)
- [Deploy](docs/wasm/DEPLOY.md)
- [Storage](docs/wasm/STORAGE.md)
- [Tracing](docs/wasm/TRACING.md)
- [Testing](docs/wasm/TESTING.md)

Quick start:

```bash
tools/build-wasm.sh Release
cd web
pnpm install --frozen-lockfile
pnpm dev
```

## NullPerator for iOS

The iOS application uses native C++ audio, MIDI, recording, persistence, and
framebuffer rendering. It does not ship the WASM runtime or browser filesystem.

```bash
cd web
pnpm install --frozen-lockfile
cd ..
ios/scripts/package-web.sh
open ios/NullPeratorIOS.xcodeproj
```

See the [iOS build guide](ios/README.md) and
[native architecture](ios/NATIVE_ARCHITECTURE.md).

## Host tests

```bash
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

## License

PicoTracker is BSD-3-Clause. Bundled third-party components retain their own
licenses; see [LICENSE](LICENSE).
