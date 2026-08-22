# Task 4: Complete SDL2 canvas UI

## Delivered

- Added the WASM `GUIFactory`, `WasmGUIWindowImp`, and `WasmEventManager`.
  The GUI implementation maintains a deterministic 240 x 240 RGBA framebuffer,
  draws the PicoTracker bitmap fonts and rectangles at the current 320 x 240
  source coordinate system, and exports
  `PicoTracker_Wasm_CaptureFrameRgba()`.
- Linked SDL2 for the platform window/event queue, while presenting the
  framebuffer through a WebGL context owned by the application pthread. The
  browser canvas is transferred once to that pthread as
  `#picotracker-canvas`.
- `Ready` is now published only after `Application::Init`, the first complete
  clock tick, and a successful framebuffer presentation.
- Replaced the blocking C++ event loop with an Emscripten animation-frame pump.
  This yields the worker event loop between frames, which is required for
  modern OffscreenCanvas implicit presentation.
- Added a hidden `#canvas` compatibility element for SDL2 2.32's hard-coded
  passive event hook. Rendering remains exclusively on
  `#picotracker-canvas`; this removes the SDL target-null registrations and
  leaves the visible canvas available for Task 5's action bridge.
- Added a heap-backed `WasmSamplePool` platform service. This fixes first boot:
  `AppWindow::LoadProject()` unconditionally resets `SamplePool`, but Task 3
  had not installed one. The pool owns decoded PCM buffers in the WASM heap,
  enforces a 32 MiB budget, and supports reset/load/unload.
- The host recreates the canvas between stop and restart, since an HTML canvas
  cannot be transferred to an OffscreenCanvas twice. The browser lifecycle is
  therefore stop, wait for the C++ stopped acknowledgement, recreate canvas,
  then start.

## Debugging evidence

The original transfer experiment failed because SDL2's software framebuffer
tries to create a 2D context from `Module.canvas`, and SDL2's GLES path proxies
EGL context creation to the main thread. Both paths attempt `getContext()` on
the already transferred HTML canvas. With software rendering and no transfer,
the application booted after the SamplePool fix but there was no usable worker
canvas.

The direct application-pthread WebGL presenter removed that context error but
initially produced a black browser image despite a non-uniform RGBA capture.
Local Emscripten source showed `emscripten_webgl_commit_frame()` is a no-op in
modern browsers and the implicit swap occurs when the worker returns to its
event loop. The previous infinite C++ loop prevented that. The rAF frame pump
is the minimal architecture that preserves the application pthread and makes
the actual tracker UI visible.

AddressSanitizer localized the earlier boot crash to
`AppWindow::LoadProject()` calling `SamplePool::GetInstance()->Reset()` at
`sources/Application/AppWindow.cpp:433`. Installing the concrete browser
SamplePool fixed this cause; all temporary ASAN, SAFE_HEAP, stack, and trace
diagnostics were removed.

## TDD and regression coverage

- Added a RED/green Vitest case for immutable RGBA capture copies. The runtime
  exposes `captureFrameRgba()` using the exported `HEAPU8` and returns a copied
  240 x 240 x 4 byte array.
- Added a RED/green runtime state test which reports whether the captured C++
  frame is non-uniform. The visible canvas publishes this as
  `data-frame-content="rendered"` for integration verification.
- Playwright verifies canvas dimensions, C++ readiness, RGBA-backed rendered
  content, a deterministic platform-independent tracker screenshot baseline
  (`device-boot.png`), and full stop/recreate/restart lifecycle behavior.
- Vitest excludes Playwright's `e2e/` suite so `pnpm vitest run` remains a
  valid unit-test command.

## Verification

```sh
cmake -S tests -B build-host && cmake --build build-host && ./build-host/picoTracker_tests
# 17 test cases, 65 assertions: PASS

tools/build-wasm.sh Debug
# PASS, including the Task 3 core-link closure gate

cd web && pnpm vitest run
# 3 files, 14 tests: PASS

cd web && npx playwright test e2e/device.spec.js
# 2 tests: PASS
```

The Playwright browser run has no browser exceptions, no SDL
`registerOrRemoveHandler` target-null errors, and no favicon 404. First-run
MEMFS intentionally logs that `/.config.xml` does not exist before creating
the default configuration; this is an application trace message, not a browser
runtime error.

## Scope

No HID, serial, or Task 5 input-action mapping was added. Task 4 provides the
live SDL event queue and visible-canvas foundation; Task 5 can attach its single
action bridge to `#picotracker-canvas`.
