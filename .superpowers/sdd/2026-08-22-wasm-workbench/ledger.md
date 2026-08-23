# SDD ledger — plan: docs/superpowers/plans/2026-08-22-wasm-workbench.md

## Plan preflight

| Item | Ruling |
| --- | --- |
| Task 1 | Self-consistent and complete at `23d696ac`; Vite 8/pnpm lock are the accepted current-toolchain equivalents of the original shell examples. |
| Task 2 | Self-consistent and complete at `795f603f`; use standard `-pthread`, wait for the C++ `Stopped` acknowledgement, then terminate workers. |
| Task 3 | Build browser primitives and full non-device link closure. Because Task 4 owns GUI, Task 3 installs non-GUI services and keeps the lifecycle shell; `Application::Init` boot is deferred to Task 4. |
| Task 4 | Owns the first complete `Application` boot and SDL2 canvas. Historical SDL code is reference material only and must be reconciled with current interfaces. |
| Task 5 | Self-consistent after Task 4; one action bridge owns all key/pointer/touch state and cleanup. |
| Task 6 | Host-testable and independent of browser playback; establishes the realtime primitives consumed by Task 7. |
| Task 7 | Replaces Task 3 silent audio, preserving the `System`/factory boundary and 44.1 kHz core rate. |
| Task 8 | Upgrades Task 3 MEMFS mounting to IDBFS without replacing the synchronous C++ filesystem API. |
| Task 9 | Builds on Task 8 storage coordination; package-manager references mean pnpm and `pnpm-lock.yaml`, not npm lockfiles. |
| Task 10 | Builds on Tasks 8–9 and treats host folders as an explicit mirror, never a direct POSIX mount. |
| Task 11 | Replaces Task 3 disconnected MIDI while preserving the MIDI factory boundary. |
| Task 12 | Adds bounded logs; process-stub diagnostics from Task 3 may initially use the existing trace sink and migrate here. |
| Task 13 | Adds bounded trace records and touches prior subsystems only at selected allocation-free scope sites. |
| Task 14 | Consolidates panels and recovery UI after their feature stores exist; it must retain all earlier behavior. |
| Task 15 | Final CI/static-distribution audit; generated WASM assets remain build artifacts rather than source commits. |
| Tasks 1 ↔ 2 | Shared `App.svelte` and build handoff: Task 2 preserves the isolated shell and makes runtime state authoritative. |
| Tasks 2 ↔ 3 | Shared WASM CMake/main: Task 3 extends the target and factory setup without changing lifecycle exports or restart acknowledgement. |
| Tasks 2 ↔ 4 | Shared main/CMake/runtime readiness: `Ready` becomes complete-app readiness only when Task 4 boots `Application`; earlier readiness remains the lifecycle smoke milestone. |
| Tasks 2 ↔ 8 | Runtime startup later waits for initial IDBFS population; shutdown/restart ordering remains C++ stop, storage flush, worker termination. |
| Tasks 2 ↔ 12 | Runtime errors feed the bounded log path once available, without changing lifecycle state semantics. |
| Tasks 2 ↔ 13 | Build identity and lifecycle timestamps use the same monotonic platform clock. |
| Tasks 3 ↔ 4 | Task 3 intentionally does not invent a headless GUI adapter; Task 4 supplies GUI factories and starts the application. |
| Tasks 3 ↔ 6 | Silent audio remains until the ring/resampler primitives exist; Task 6 does not switch factories. |
| Tasks 3 ↔ 7 | Task 7 deletes silent audio and switches factories/CMake atomically. |
| Tasks 3 ↔ 8 | Stable `/data` filesystem API; Task 8 changes mounting/sync orchestration only. |
| Tasks 3 ↔ 11 | Task 11 deletes disconnected MIDI and switches factories/CMake atomically. |
| Tasks 3 ↔ 12 | Unsupported process calls emit one diagnostic; Task 12 later routes it into structured bounded logs. |
| Tasks 4 ↔ 5 | GUI event manager is the sole C++ input consumer; Task 5 adds the bridge/action mapping without a second event path. |
| Tasks 4 ↔ 13 | Frame counters/durations are exposed through trace hooks with zero allocation in draw paths. |
| Tasks 4 ↔ 14 | Workbench layout may move the canvas but must preserve its id, logical size, focus, and screenshot behavior. |
| Tasks 5 ↔ 13 | Input latency tracing observes the unified action bridge and never changes input state. |
| Tasks 5 ↔ 14 | Input owns the fixed Node WASD/JK/XC map; Settings does not expose or persist remapping. |
| Tasks 6 ↔ 7 | Task 7 consumes the exact ring/resampler contracts and keeps the worklet callback allocation-, lock-, log-, and I/O-free. |
| Tasks 6 ↔ 13 | Audio metrics are atomic observations of the ring/worklet paths. |
| Tasks 7 ↔ 13 | Audio instrumentation uses fixed records/atomics only and cannot affect PCM output hashes. |
| Tasks 7 ↔ 14 | Top-bar/settings audio controls use Task 7 store states and preserve user-gesture unlock. |
| Tasks 8 ↔ 9 | Files operations use the serialized storage coordinator and request persistence after mutations. |
| Tasks 8 ↔ 10 | Host synchronization operates only at safe serialized sync points and never bypasses `/data`. |
| Tasks 8 ↔ 14 | Restart/recovery preserves dirty storage and exposes persistent failures. |
| Tasks 9 ↔ 10 | Files panel owns presentation; host-folder code owns permissions/manifests/conflicts. |
| Tasks 9 ↔ 14 | Layout polish must preserve ZIP safety and all file actions. |
| Tasks 10 ↔ 14 | Conflict dialog and host permission recovery remain explicit and actionable. |
| Tasks 11 ↔ 13 | MIDI tracing observes bounded queues/timestamps and cannot drop or reorder realtime clock bytes. |
| Tasks 11 ↔ 14 | MIDI panel/store own permission and reconnect UI; no separate MIDI logging product is added. |
| Tasks 12 ↔ 14 | Logs panel may be restyled but its bounded/dropped-record behavior remains authoritative. |
| Tasks 13 ↔ 14 | Trace panel may be restyled but fixed-buffer capture and deterministic export remain authoritative. |
| Tasks 14 ↔ 15 | Final UI state and recovery surfaces are the subject of the full Playwright/static-bundle acceptance audit. |

## Task ledger

### External acceptance fixture

- User-provided factory content: `/Users/nengzhuocai/Downloads/factory-content-main`.
- Size: about 194 MiB; 5,156 library sample files; projects include Advance
  `oneCycAc`, `bt9-midi`, and `dark-fog`; `default-current.txt` selects
  `oneCycAc`.
- Use a minimal licensed fixture subset for automated UI/audio/storage tests;
  do not copy the entire sample library into the repository or static bundle.
- Task 7 should exercise real playback with `oneCycAc` where format-compatible;
  Tasks 8–10 should import/mount the external directory and verify persistence.

| Task | Status | Base | Head | Review | Notes |
| --- | --- | --- | --- | --- | --- |
| 1 | complete | `d7b4e0ad` | `23d696ac` | clean | Imported prior completion; Vitest and production build passed. |
| 2 | complete | `23d696ac` | `795f603f` | clean after fixes | Frontend 11/11, host C++ 11 cases/39 assertions, real browser startup/restart smoke passed. |
| 3 | complete | `795f603f` | `694885bd` | clean after 2 fix rounds | Host 17/17 cases and 65/65 assertions; Debug + Release WASM and stable core-closure gates pass; no GUI/Application boot yet. |
| 4 | complete | `694885bd` | `ffc1215f` | clean after 1 fix round | Host 17/17, Debug WASM/core gate, Vitest 14/14, Playwright 2/2; real tracker frame, capture, restart, platform-independent golden. |
| 5 | complete | `ffc1215f` | `7919e185` | clean after 2 fix rounds | Host 20/82, Debug WASM/core gate, Vitest 19, Playwright 5; all actions, cleanup, concurrency, accessibility and focus semantics reviewed. |
| 6 | complete | `7919e185` | `4418c875` | clean after 1 fix round | Alias-safe SPSC transport; deterministic resampler; normal/ASan+UBSan/TSan/stress/WASM pass. |
| 7 | implemented; external acceptance pending | `4418c875` | `5a2b5e47` | clean after 2 fix rounds | Browser audio adapter, realtime-safe callback, shared diagnostics and deterministic oracle. User testing and the interactive in-app browser produce audible audio; the latter reached setup phase 8 with 806 callbacks and zero underruns in the measured interval. Automated Chrome remains RED because its `AudioWorklet.addModule()` never resolves past phase 3 with or without an early unlock, so 44.1/48 kHz hardware and 30-minute acceptance remain external gates rather than skipped passes. |
| 8 | complete | `5a2b5e47` | Task 8 commit | clean after 3 fix rounds | IDBFS startup, serialized persistence, recoverable shutdown flush, contained mutation notifications, and restart/reload byte acceptance. |
| 9 | complete | `25c16320` | Task 9 commit | clean after 2 fix rounds | Matrix-style Files panel, serialized virtual-disk operations, bounded local-record/CRC ZIP parsing, transactional rollback, and persistent browser acceptance. |
| 10 | complete; external picker acceptance pending | `068284c8` | `5d74a485` | reviewed and committed | Permission-aware host-folder mirror, bounded three-way manifests, transactional conflict policies, and recovery-safe lifecycle integration. Real Chrome/Edge picker acceptance remains external. |
| 11 | complete; physical-device acceptance pending | `9d8e6785` | `2885496a` | reviewed and committed | Bounded native/browser Web MIDI input/output bridge, reconnect handling, latency tracing, and panel. Physical MIDI hardware remains external. |
| 12 | complete | `9d8e6785` | `2885496a` | reviewed and committed | Fixed native log ABI, bounded 1,000-record browser store, filtering/export, runtime console capture, and fatal-diagnostic retention. |
| 13 | complete; cross-browser acceptance pending | `9d8e6785` | `2885496a` | reviewed and committed | Fixed 4,096-record native capture, real UI/audio/player/files/storage/MIDI scopes, honest synthetic DSP microbenchmark, summaries, and Chrome Trace export. |
| 14 | complete; visual acceptance pending | `2885496a` | `2885496a` | reviewed and committed | MatrixOS component topology with navigation rails, multi-panel tool stack, effective device scaling, full Operator controls, fixed Node WASD/JK/XC input, and helper tips. |
| 15 | implemented; external acceptance pending | `2885496a` | documentation/CI commit | local checks green | Pinned serial WASM/Pico CI, static distribution headers/notices, and five operator guides. Current local gates: host 74/74 (193748 assertions), Vitest 174/174, and Vite production build. Real factory-content browser persistence, strict 44.1/48 kHz and 30-minute audio, real Chrome/Edge folder and physical MIDI, Firefox/Safari, and physical Node regression remain external. |
