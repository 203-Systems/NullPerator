# Developer guide

PicoTracker supports three products: ESP32-S3 Node firmware, the browser WASM
workbench, and the native NullPerator iOS application. All three use the same
application model and UI2 renderer.

## Host tests

```bash
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

## WASM

```bash
tools/build-wasm.sh Release
cd web
pnpm install --frozen-lockfile
pnpm test --run
pnpm dev
```

See [the WASM build guide](wasm/BUILD.md) and
[test guide](wasm/TESTING.md) for the complete browser workflow.

## Node firmware

Install and export ESP-IDF, then run:

```bash
idf.py --project-dir sources -B sources/build/node -DNode=true build
idf.py --project-dir sources -B sources/build/node -DNode=true flash monitor
```

The target is fixed to `esp32s3`. See
[README_NODE.MD](../README_NODE.MD) for configuration and troubleshooting.

## NullPerator for iOS

Install and package the shared Svelte presentation before opening the Xcode
project:

```bash
cd web
pnpm install --frozen-lockfile
cd ..
ios/scripts/package-web.sh
open ios/NullPeratorIOS.xcodeproj
```

Select the `NullPeratorIOS` scheme and an Apple Developer Team. Xcode builds the
native C++ library for the selected device or simulator before linking the app.
See the [iOS build guide](../ios/README.md) for runtime details.

## Architecture rules

- UI state and animation stay in UI2; project data remains in the application
  model and services.
- Platform adapters implement explicit fixed-capacity interfaces.
- The audio callback must not allocate, lock, log, or touch UI state.
- Node, WASM, and iOS preserve the same project format and editing semantics.
- Add focused host tests for model, workflow, persistence, and input changes.
- Run the native layout Playwright suite for shared mobile UI changes.
