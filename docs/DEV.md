# Developer guide

NullPerator supports three products: the ESP32-S3 hardware firmware, the
browser WASM workbench, and the native iOS application. All three use the same
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

## NullPerator hardware

Install and export ESP-IDF, then run:

```bash
idf.py --project-dir sources -B sources/build/node -DNode=true build
idf.py --project-dir sources -B sources/build/node -DNode=true flash monitor
```

The target is fixed to `esp32s3`. See the
[hardware build guide](HARDWARE.md) for configuration and troubleshooting.

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
- NullPerator hardware, WASM, and iOS preserve the same project format and
  editing semantics.
- Add focused host tests for model, workflow, persistence, and input changes.
- Run the native layout Playwright suite for shared mobile UI changes.

## Instrument memory and voice ownership

The bank has 64 logical slots, not fixed per-type preset pools. `NONE` slots
share an empty instrument. Creating or replacing a preset allocates only the
chosen type; transactional replacement publishes it after validation succeeds.
Handle allocation failure without overwriting the existing slot.

Drum, Stack, and Chiptune each use bank-owned, per-track voice storage instead
of an eight-voice array inside every preset. The three GB types share a single
per-track pool allocated when the first GB preset is created and freed after
the last one is removed. These allocations happen outside the audio callback.
Respect `TrackVoicePool` ownership when rendering, applying FX, or stopping a
voice; a replaced preset must not affect the new owner on that track.

This removes preset quotas, not engine constraints: SID still shares one
three-oscillator chip, OPAL remains monophonic per preset, MIDI has 16 protocol
channels, and the song still has eight tracks. Test clone/import/load failures
as well as creating many presets of the same type.
