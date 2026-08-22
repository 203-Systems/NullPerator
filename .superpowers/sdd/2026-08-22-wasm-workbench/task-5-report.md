# Task 5 report — complete interactive controls

## RED / GREEN

- **RED:** Added `web/tests/input.test.js`, then ran
  `cd web && pnpm test -- --run tests/input.test.js`. The suite failed because
  `src/stores/input.js` did not exist.
- **GREEN:** Implemented the unified input store, WASM bridge, C++ action map,
  and virtual controls. The full Vitest suite passes: 4 files, 19 tests.

## Implementation

- Restored the historical SDL desktop defaults from
  `75e58070556f…:sources/Adapters/SDL/Input/SDLInput.cpp`: A, S, arrows,
  right/left Ctrl, and Space. The historical adapter predated Select and
  Power, which receive explicit browser defaults X and P.
- Exposed all 11 current `GUIEventPadButtonType` actions through a persisted,
  remappable `KeyMap`, including chords and multiple simultaneous sources.
- Added `PicoTracker_Wasm_SetAction` and
  `PicoTracker_Wasm_ReleaseAllActions`. `InputMap` owns the held-action mask
  and injects SDL user events; `WasmEventManager` is still the sole C++ UI
  dispatcher on the application pthread/rAF loop.
- A post-implementation review hardened the queue boundary: an `InputMap`
  mutex serializes each SDL insertion with its held-state transition, failed
  releases remain held for a later retry, and deterministic host tests inject
  queue failures and concurrent release/press producers. SDL keyboard events
  are disabled at initialization and are no longer dispatched, leaving the
  Svelte KeyMap as the only browser keyboard path.
- A final queue-failure pass separates desired browser state from
  SDL-accepted held state. A failed ordinary UP remains pending and the ready
  `WasmEventManager::PumpFrame` retries it before processing SDL events; every
  transition and queue insertion remains serialized by the same mutex.
- The bridge exposes dispatched-action mask, generation, and last-action
  diagnostics. They are recorded only after `WasmEventManager` dispatches into
  the real C++ UI, and support both browser debugging and action-specific e2e
  checks.
- Added keyboard focus ownership, browser-repeat suppression, pointer capture,
  multi-touch source tracking, virtual controls, and release-all cleanup on
  blur, hidden-page visibility, pointer cancellation, runtime stop/restart,
  component destruction, and WASM shutdown.
- Runtime snapshots clear their input bridge in every non-ready state; virtual
  controls are disabled outside ready state. Virtual buttons additionally use
  accessible click/Enter/Space activation with a dedicated source while
  suppressing only the synthetic pointer-up click duplicate. Keyboard clicks
  (`detail === 0`) always activate after cancellation/lost capture. Only the
  tracker canvas, not a focused child button, receives mapped tracker keyboard
  input.
- C++ action diagnostics are opt-in (`?inputDiagnostics=1`), so production
  does not poll three mutex-taking WASM getters every 16ms. The input e2e suite
  explicitly opts in before observing real C++ UI dispatch state.
- Kept the visible Task 4 canvas, app pthread, rAF pump, restart flow, and
  hidden SDL compatibility canvas intact. Virtual controls sit below the
  device panel so the existing canvas visual baseline remains unchanged.

## Verification

- `cmake --build build-host --parallel && ./build-host/picoTracker_tests`
  — 20 doctest cases / 82 assertions passed, including injected queue-failure,
  ordinary-release frame-retry, and concurrent producer ordering checks.
- `source /Users/nengzhuocai/.cache/emsdk/emsdk_env.sh && tools/build-wasm.sh Debug`
  — successful Debug WASM compile, link, and core-link closure verification.
- `cd web && pnpm test -- --run` — 19 tests passed.
- `cd web && pnpm exec playwright test --workers=1` — 5 tests passed,
  including the existing device/restart coverage. Input e2e holds each of all
  11 controls and asserts its exact post-dispatch C++ action bit, checks cleanup
  masks and a stable dispatch generation after blur/pointer-cancel, proves
  keyboard activation still works after pointer cancellation, and proves a
  focused virtual button does not invoke global tracker mappings.

## Self-review

- No scope outside Task 5: no HID, Serial, audio, storage, MIDI, Node, or Pico
  behavior changes.
- The default Select/Power bindings are browser choices because the historical
  SDL map only defined nine actions; users can remap and persist them.
- The pending transition retry intentionally runs only while the runtime is
  ready; shutdown still requests release-all before SDL teardown, while a
  failed release remains pending for the next ready frame rather than being
  silently forgotten.

## Changed files

- `sources/Adapters/wasm/input/InputMap.{h,cpp}`
- `sources/Adapters/wasm/{CMakeLists.txt,gui/WasmEventManager.cpp,platform/wasm_bridge.{h,cpp}}`
- `web/src/{components/DevicePanel.svelte,components/VirtualControls.svelte,handles/input.js,handles/runtime.js,stores/input.js,stores/runtime.js}`
- `web/{tests/input.test.js,tests/runtime.test.js,e2e/input.spec.js}`
- `tests/{CMakeLists.txt,wasm_platform_tests.cpp}`

## Commit

`feat(wasm): add complete interactive controls`
