# Developer guide

PicoTracker supports two products: ESP32-S3 Node firmware and the browser WASM
workbench. Both use the same application model and UI2 renderer.

## Host tests

```bash
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel
./build-host/picoTracker_tests
```

## WASM

```bash
tools/build-wasm.sh Release
cd web
npm install
npm test -- --run
npm run dev
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

## Architecture rules

- UI state and animation stay in UI2; project data remains in the application
  model and services.
- Platform adapters implement explicit fixed-capacity interfaces.
- The audio callback must not allocate, lock, log, or touch UI state.
- Node and WASM preserve the same project format and editing semantics.
- Add focused host tests for model, workflow, persistence, and input changes.
