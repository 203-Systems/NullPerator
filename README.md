<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/NullPerator_White.svg">
    <img src="docs/assets/NullPerator_Black.svg" width="420" alt="NullPerator">
  </picture>
</p>

<p align="center">
  An eight-track music tracker for NullPerator hardware, iPhone, iPad, and the web.
</p>

NullPerator combines a compact, controller-driven workflow with a shared C++
tracker engine. Compose songs from patterns, chains, phrases, and tables; work
with samples or synthesized instruments; and move compatible projects between
the supported targets.

NullPerator builds on the open-source picoTracker project and is developed by
203 Systems.

## Features

- Eight song tracks with 256 chains, 128 phrases, and 32 tables
- Sample, MIDI, SID, OPAL, and Macro instruments
- 44.1 kHz stereo audio engine
- Fast editing through physical controllers or on-screen controls
- Project autosave, sample import, and offline local storage
- Shared 240×240 UI2 tracker interface across hardware, iOS, and the web

## Supported targets

| Target | Runtime | Platform integration |
| --- | --- | --- |
| NullPerator for iOS | Native C++ core with a bundled Svelte presentation | Core Audio recording and playback, CoreMIDI, Bluetooth MIDI, GameController, background audio, and Files |
| NullPerator hardware | Native C++ firmware | ESP32-S3 hardware, display, controls, audio, MIDI, and storage |
| WASM workbench | C++ core compiled to WebAssembly | Browser storage, host-folder synchronization, Web MIDI, logs, and tracing |

All targets share the application model, project format, audio engine, and UI2
renderer. Platform adapters provide audio, MIDI, input, display, and filesystem
services without changing tracker behavior.

## Build NullPerator for iOS

Requirements: Xcode, an iOS signing team for physical devices, and pnpm.

```bash
cd web
pnpm install --frozen-lockfile
cd ..
ios/scripts/package-web.sh
open ios/NullPeratorIOS.xcodeproj
```

Select the `NullPeratorIOS` scheme, choose an iPhone, iPad, or simulator, and
run the app. The Xcode build phase compiles and links the native C++ core.

See the [iOS build and architecture guide](ios/README.md) for runtime details.

## Run the WASM workbench

Requirements: the pinned Emscripten toolchain, Node.js, and pnpm.

```bash
cd web
pnpm install --frozen-lockfile
cd ..
tools/build-wasm.sh Release
cd web
pnpm dev
```

The development server prints the local URL after startup. See the
[WASM build guide](docs/wasm/BUILD.md) for toolchain setup and production
packaging.

## Build the NullPerator hardware firmware

The hardware firmware targets ESP32-S3 and builds with ESP-IDF:

```bash
idf.py --project-dir sources -B sources/build/node -DNode=true build
```

See the [hardware build guide](docs/HARDWARE.md) for configuration, flashing,
monitoring, and troubleshooting.

## Run host tests

```bash
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

Shared changes should also be validated on every affected product target.

## Repository layout

| Path | Purpose |
| --- | --- |
| `sources/` | Shared C++ tracker engine, UI2, services, and platform adapters |
| `ios/` | Swift and Objective-C++ iOS host, native bridge, resources, and packaging scripts |
| `web/` | Shared Svelte presentation and the WASM workbench |
| `tests/` | Host-side C++ tests |
| `tools/` | Build, verification, asset, and firmware acceptance utilities |
| `docs/` | Development, release, App Store, privacy, and WASM guides |

## Documentation

- [Developer guide](docs/DEV.md)
- [Hardware build and flashing](docs/HARDWARE.md)
- [Release process](docs/RELEASES.md)
- [WASM storage](docs/wasm/STORAGE.md)
- [WASM testing](docs/wasm/TESTING.md)
- [WASM deployment](docs/wasm/DEPLOY.md)
- [Privacy policy](docs/NullPeratorPrivacyPolicy.md)
- [App Store submission checklist](docs/AppStoreSubmission.md)

## Hardware

Learn more about the physical NullPerator at
[203 Systems](https://203.io/products/operator-deposit).

## Contributing

Read the [developer guide](docs/DEV.md) and
[contribution guidelines](docs/CONTRIBUTING.md) before opening a pull request.

## License

This project is distributed under the BSD 3-Clause License. See
[LICENSE](LICENSE) for the picoTracker copyright history and the licenses of
bundled third-party components. Web distribution notices are maintained in
[`web/public/THIRD_PARTY_NOTICES.md`](web/public/THIRD_PARTY_NOTICES.md).
