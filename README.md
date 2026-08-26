# PicoTracker

PicoTracker is an eight-track music tracker with a compact, controller-driven
workflow. This repository contains two supported products:

- **Node firmware** for the ESP32-S3 hardware target.
- **WASM workbench** for running the same C++ engine and UI2 in a browser.

Both products use the 240×240 UI2 renderer. The retired hardware targets and
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
npm install
npm run dev
```

## Host tests

```bash
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel
./build-host/picoTracker_tests
```

## License

PicoTracker is BSD-3-Clause. Bundled third-party components retain their own
licenses; see [LICENSE](LICENSE).
