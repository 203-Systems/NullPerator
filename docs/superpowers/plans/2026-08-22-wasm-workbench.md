# PicoTracker WebAssembly Workbench Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a self-contained static PicoTracker WebAssembly workbench with the complete C++ UI, low-latency audio, persistent projects and samples, host-folder synchronization, Web MIDI, bounded logs, and exportable performance tracing.

**Architecture:** Compile the existing PicoTracker application against a new SDL2/Emscripten adapter and run it on a pthread. Host it inside an adapted MatrixOS-style Svelte/Vite workbench; use a WASM AudioWorklet and shared lock-free buffers for sound, IDBFS plus an optional File System Access mirror for storage, and bounded shared queues for MIDI, logs, and trace events.

**Tech Stack:** C++23, CMake, Emscripten, SDL2, pthreads, Wasm AudioWorklets, Svelte 5, Vite 8, Vitest, Playwright, doctest, IndexedDB/IDBFS, File System Access API, Web MIDI API.

**Spec:** `docs/superpowers/specs/2026-08-22-wasm-workbench-design.md`

## Global Constraints

- Keep the existing PicoTracker `Application`, `UIFramework`, player, mixer, and instruments as the only tracker implementation.
- Do not link MatrixOS as a submodule, package, CDN asset, or runtime dependency; copied MIT-licensed portions retain their notice.
- Do not add HID or Serial panels, stores, handles, or WASM exports.
- Chrome and Edge are full-feature targets; Firefox and Safari use the documented IDBFS/ZIP fallback.
- The WASM runtime requires `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`.
- Keep PicoTracker audio generation at 44.1 kHz stereo; resample only at the WASM platform boundary.
- The AudioWorklet callback must not allocate, log, lock, access files, or block.
- Host-folder access cannot escape the selected directory and cannot silently overwrite conflicts.
- Node and Pico build behavior must remain unchanged.
- Use test-first development and one independently reviewable commit per task.

---

## File map

### Build and application target

- `sources/CMakeLists.txt`: select Pico, Node, or WASM without affecting existing paths.
- `sources/Adapters/wasm/CMakeLists.txt`: compose the WASM platform libraries and executable.
- `sources/Adapters/wasm/main/main.cpp`: initialize adapters and enter the application lifecycle.
- `cmake/wasm/toolchain-check.cmake`: validate Emscripten and required capabilities.
- `tools/build-wasm.sh`: reproducible configure/build/copy command.

### WASM platform adapter

- `sources/Adapters/wasm/platform/wasm_bridge.h`: stable C exports shared with JavaScript.
- `sources/Adapters/wasm/platform/wasm_bridge.cpp`: lifecycle and capability bridge only.
- `sources/Adapters/wasm/gui/`: SDL2 canvas, GUI factory, event manager, and framebuffer capture.
- `sources/Adapters/wasm/input/`: action map and input-state cleanup.
- `sources/Adapters/wasm/audio/`: audio driver, PCM ring buffer, resampler, and AudioWorklet entry.
- `sources/Adapters/wasm/filesystem/`: synchronous PicoTracker filesystem implementation and JS sync notifications.
- `sources/Adapters/wasm/midi/`: Web MIDI input/output queues and MIDI service adapter.
- `sources/Adapters/wasm/logging/`: bounded structured log records.
- `sources/Adapters/wasm/tracing/`: fixed-size trace ring, scopes, metrics, and benchmark control.
- `sources/Adapters/wasm/system/`, `timer/`, `mutex/`, `process/`: browser implementations of required platform services.

### Workbench

- `web/src/App.svelte`: workbench composition only.
- `web/src/components/TopBar.svelte`: runtime/audio/storage/MIDI status and restart.
- `web/src/components/DevicePanel.svelte`: canvas host and virtual controls.
- `web/src/components/FilesPanel.svelte`: virtual disk, host mount, sync, and ZIP operations.
- `web/src/components/MidiPanel.svelte`: Web MIDI permission and routing.
- `web/src/components/LogsPanel.svelte`: bounded runtime log viewer.
- `web/src/components/TracePanel.svelte`: trace capture, summary, and export.
- `web/src/components/SettingsPanel.svelte`: display, keymap, audio, and trace settings.
- `web/src/components/AboutPanel.svelte`: build identity and license notices.
- `web/src/stores/runtime.js`: WASM module lifecycle state machine.
- `web/src/stores/input.js`: unified keyboard/pointer/touch state.
- `web/src/stores/audio.js`: AudioContext state and metrics.
- `web/src/stores/storage.js`: IDBFS state and serialized sync requests.
- `web/src/stores/midi.js`: MIDI permission, ports, selections, and reconnect.
- `web/src/stores/logs.js`: bounded filtered log state.
- `web/src/stores/trace.js`: trace capture and export state.
- `web/src/handles/`: narrow WASM API wrappers with no UI state.
- `web/src/storage/hostFolder.js`: File System Access handle and permission management.
- `web/src/storage/syncManifest.js`: deterministic folder diff and conflict resolution.
- `web/tests/`: Vitest unit tests.
- `web/e2e/`: Playwright end-to-end and screenshot tests.

---

### Task 1: Reproducible WASM toolchain and static shell

**Files:**
- Create: `cmake/wasm/toolchain-check.cmake`
- Create: `tools/build-wasm.sh`
- Create: `web/package.json`
- Create: `web/vite.config.js`
- Create: `web/index.html`
- Create: `web/src/main.js`
- Create: `web/src/App.svelte`
- Create: `web/src/app.css`
- Create: `web/src/buildMetadata.js`
- Create: `web/tests/buildMetadata.test.js`
- Create: `web/THIRD_PARTY_NOTICES.md`
- Modify: `.gitignore`

**Interfaces:**
- Produces: `tools/build-wasm.sh [Debug|Release]`, `pnpm build`, and `parseBuildMetadata(raw: unknown): BuildMetadata`.
- Produces: Vite development and preview servers with both required isolation headers.

- [x] **Step 1: Write the failing metadata and config tests**

```js
import { describe, expect, it } from 'vitest'
import { parseBuildMetadata } from '../src/buildMetadata.js'

describe('parseBuildMetadata', () => {
  it('normalizes a complete WASM build identity', () => {
    expect(parseBuildMetadata({ commit: 'abc12345', dirty: false, builtAt: '2026-08-22T00:00:00Z' }))
      .toEqual({ commit: 'abc12345', dirty: false, builtAt: '2026-08-22T00:00:00Z' })
  })
})
```

- [x] **Step 2: Run the tests and verify RED**

Run: `cd web && pnpm install && pnpm exec vitest run`

Expected: FAIL because `package.json` and `buildMetadata.js` do not exist.

- [x] **Step 3: Add the minimal isolated static shell and toolchain check**

```cmake
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "The WASM target requires emcmake/em++")
endif()
```

```js
export function parseBuildMetadata(raw) {
  return {
    commit: String(raw?.commit ?? 'unknown'),
    dirty: Boolean(raw?.dirty),
    builtAt: String(raw?.builtAt ?? 'unknown'),
  }
}
```

The copied MatrixOS-derived shell must include its MIT notice and contain no
HID or Serial packages, routes, stores, or components.

- [x] **Step 4: Verify GREEN and production build**

Run: `cd web && pnpm exec vitest run && pnpm build`

Expected: Vitest passes and `web/dist/index.html` exists.

- [x] **Step 5: Commit**

```bash
git add cmake/wasm tools/build-wasm.sh web .gitignore
git commit -m "build(wasm): add isolated static workbench shell"
```

### Task 2: Minimal Emscripten executable and runtime lifecycle

**Files:**
- Create: `sources/Adapters/wasm/CMakeLists.txt`
- Create: `sources/Adapters/wasm/main/main.cpp`
- Create: `sources/Adapters/wasm/platform/wasm_bridge.h`
- Create: `sources/Adapters/wasm/platform/wasm_bridge.cpp`
- Create: `web/src/stores/runtime.js`
- Create: `web/src/handles/runtime.js`
- Create: `web/tests/runtime.test.js`
- Modify: `sources/CMakeLists.txt`
- Modify: `web/src/App.svelte`

**Interfaces:**
- Produces C exports: `PicoTracker_Wasm_GetBuildMetadataJson()`, `PicoTracker_Wasm_GetState()`, `PicoTracker_Wasm_RequestShutdown()`, and `PicoTracker_Wasm_GetLastError()`.
- Produces JS: `createRuntime(options): Promise<RuntimeHandle>` and `restartRuntime(): Promise<void>`.

- [x] **Step 1: Write lifecycle state-machine tests**

```js
it('terminates the old module before creating a replacement', async () => {
  const calls = []
  const runtime = makeRuntimeForTest({ terminate: () => calls.push('stop'), create: async () => calls.push('start') })
  await runtime.restart()
  expect(calls).toEqual(['stop', 'start'])
})
```

- [x] **Step 2: Verify RED**

Run: `cd web && pnpm exec vitest run tests/runtime.test.js`

Expected: FAIL because the runtime store is absent.

- [x] **Step 3: Implement the minimal module and explicit lifecycle**

```cpp
enum class WasmRuntimeState : uint32_t { Booting, Ready, Stopping, Failed, Stopped };
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t PicoTracker_Wasm_GetState();
extern "C" EMSCRIPTEN_KEEPALIVE const char *PicoTracker_Wasm_GetBuildMetadataJson();
```

Link with standard `-pthread`, `-sPROXY_TO_PTHREAD=1`, a fixed pthread pool,
modularized output, and explicit exported functions. Restart waits for the
C++ application pthread to publish `Stopped` before terminating idle workers.

- [x] **Step 4: Verify lifecycle tests and WASM smoke boot**

Run: `cd web && pnpm exec vitest run tests/runtime.test.js`

Run: `tools/build-wasm.sh Debug`

Expected: the generated module reaches `Ready` under the local isolated preview.

- [x] **Step 5: Commit**

```bash
git add sources/CMakeLists.txt sources/Adapters/wasm web/src web/tests
git commit -m "feat(wasm): add runtime lifecycle"
```

### Task 3: Compile the current PicoTracker core with browser platform services

**Files:**
- Create: `sources/Adapters/wasm/system/WasmSystem.{h,cpp}`
- Create: `sources/Adapters/wasm/timer/WasmTimer.{h,cpp}`
- Create: `sources/Adapters/wasm/mutex/WasmMutex.{h,cpp}`
- Create: `sources/Adapters/wasm/process/WasmProcess.{h,cpp}`
- Create: `sources/Adapters/wasm/filesystem/WasmFile.{h,cpp}`
- Create: `sources/Adapters/wasm/filesystem/WasmFileSystem.{h,cpp}`
- Create: `sources/Adapters/wasm/audio/WasmSilentAudio.{h,cpp}`
- Create: `sources/Adapters/wasm/midi/WasmDisconnectedMidi.{h,cpp}`
- Create: `sources/Adapters/wasm/platform/etl_profile.h`
- Create: `tests/wasm_platform_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `sources/Adapters/wasm/CMakeLists.txt`
- Modify: `sources/Adapters/wasm/main/main.cpp`

**Interfaces:**
- Produces monotonic `Millis()` and `Micros()`, pthread-backed mutexes, timer callbacks, process stubs with explicit unsupported errors, a MEMFS-backed implementation of the final filesystem interfaces, and installation of all required factories.
- Produces explicit silent-audio and disconnected-MIDI adapters so the complete application can boot before Tasks 7 and 11 replace those services.

- [ ] **Step 1: Add failing monotonic-clock and timer-cancellation tests**

```cpp
TEST_CASE("WASM monotonic conversion does not move backwards") {
  WasmClock clock([] { return 12.5; });
  CHECK(clock.Micros() == 12500);
  CHECK(clock.Millis() == 12);
}
```

- [ ] **Step 2: Verify RED**

Run: `cmake -S tests -B build-host && cmake --build build-host && ./build-host/picoTracker_tests`

Expected: FAIL because `WasmClock` is undefined.

- [ ] **Step 3: Implement platform primitives and link the full non-device core**

Use `emscripten_get_now()` for browser monotonic time and `std::mutex` or
`pthread_mutex_t` for the WASM mutex implementation. Back the filesystem with
Emscripten's synchronous MEMFS at `/data`; Task 8 adds persistent mounting and
sync coordination without replacing the C++ file API. Unsupported process
operations return failure and one structured log; they do not pretend success.
Silent audio and disconnected MIDI report their unavailable state while safely
accepting ordinary application initialization and shutdown.

- [ ] **Step 4: Verify host tests and full link closure**

Run: `cmake --build build-host && ./build-host/picoTracker_tests`

Run: `tools/build-wasm.sh Debug`

Expected: host tests pass and the linker reports no missing platform symbols.

- [ ] **Step 5: Commit**

```bash
git add sources/Adapters/wasm tests
git commit -m "feat(wasm): add browser platform services"
```

### Task 4: Complete SDL2 canvas UI

**Files:**
- Create: `sources/Adapters/wasm/gui/WasmGUIWindowImp.{h,cpp}`
- Create: `sources/Adapters/wasm/gui/GUIFactory.{h,cpp}`
- Create: `sources/Adapters/wasm/gui/WasmEventManager.{h,cpp}`
- Create: `sources/Adapters/wasm/gui/font.h`
- Create: `web/src/components/DevicePanel.svelte`
- Create: `web/e2e/device.spec.js`
- Create: `web/playwright.config.js`
- Modify: `sources/Adapters/wasm/CMakeLists.txt`
- Modify: `sources/Adapters/wasm/main/main.cpp`
- Modify: `web/src/App.svelte`

**Interfaces:**
- Produces: complete current `I_GUIWindowImp` behavior, canvas id `#picotracker-canvas`, and `PicoTracker_Wasm_CaptureFrameRgba()` for tests.

- [ ] **Step 1: Write a failing Playwright boot/screenshot test**

```js
test('boots the C++ UI at 240x240 logical pixels', async ({ page }) => {
  await page.goto('/')
  await expect(page.locator('[data-runtime-state="ready"]')).toBeVisible()
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('width', '240')
  await expect(page.locator('#picotracker-canvas')).toHaveAttribute('height', '240')
})
```

- [ ] **Step 2: Verify RED**

Run: `cd web && npx playwright test e2e/device.spec.js`

Expected: FAIL because the device panel and complete GUI adapter are absent.

- [ ] **Step 3: Port current GUI interfaces to SDL2**

Use the historical SDL adapter only to recover semantics. Implement all current
drawing, text, clipping, dirty-region, modal, and event APIs against SDL2. Add
`--use-port=sdl2` to compile and link options. Canvas CSS uses
`image-rendering: pixelated` and preserves the 1:1 logical coordinate system.

- [ ] **Step 4: Verify full UI boot and representative golden screenshots**

Run: `tools/build-wasm.sh Debug && cd web && npx playwright test e2e/device.spec.js`

Expected: boot test passes and song, project, instrument, sample, mixer, device,
theme, and modal golden images match approved baselines.

- [ ] **Step 5: Commit**

```bash
git add sources/Adapters/wasm/gui web/src web/e2e web/playwright.config.js
git commit -m "feat(wasm): render complete tracker UI with SDL2"
```

### Task 5: Keyboard, pointer, touch, and virtual controls

**Files:**
- Create: `sources/Adapters/wasm/input/InputMap.{h,cpp}`
- Create: `web/src/stores/input.js`
- Create: `web/src/handles/input.js`
- Create: `web/src/components/VirtualControls.svelte`
- Create: `web/tests/input.test.js`
- Create: `web/e2e/input.spec.js`
- Modify: `sources/Adapters/wasm/platform/wasm_bridge.h`
- Modify: `sources/Adapters/wasm/platform/wasm_bridge.cpp`
- Modify: `web/src/components/DevicePanel.svelte`

**Interfaces:**
- Produces C exports: `PicoTracker_Wasm_SetAction(uint16_t action, bool pressed)` and `PicoTracker_Wasm_ReleaseAllActions()`.
- Produces JS: `pressAction(action)`, `releaseAction(action)`, `releaseAllActions()`, and persisted `KeyMap`.

- [ ] **Step 1: Write failing input-state tests**

```js
it('releases every held action when focus is lost', () => {
  const input = createInputStore(fakeBridge)
  input.press('enter')
  input.press('up')
  input.releaseAll()
  expect(fakeBridge.calls).toEqual([['enter', true], ['up', true], ['all', false]])
})
```

- [ ] **Step 2: Verify RED**

Run: `cd web && npm test -- --run tests/input.test.js`

Expected: FAIL because `createInputStore` does not exist.

- [ ] **Step 3: Implement unified action state and controls**

Use the fixed Node WASD/JK/XC layout, including C's 500 ms START tap/hold
Play/Nav behavior; support chords, multi-touch, and
pointer capture. Ignore DOM `KeyboardEvent.repeat`. Release all actions on
blur, visibility change, pointer cancel, restart, and component destruction.

- [ ] **Step 4: Verify unit and browser navigation tests**

Run: `cd web && npm test -- --run tests/input.test.js && npx playwright test e2e/input.spec.js`

Expected: every action reaches the C++ UI and no key remains held after cleanup.

- [ ] **Step 5: Commit**

```bash
git add sources/Adapters/wasm/input sources/Adapters/wasm/platform web/src web/tests web/e2e
git commit -m "feat(wasm): add complete interactive controls"
```

### Task 6: Lock-free PCM queue and deterministic resampler

**Files:**
- Create: `sources/Adapters/wasm/audio/PcmRingBuffer.{h,cpp}`
- Create: `sources/Adapters/wasm/audio/OutputResampler.{h,cpp}`
- Create: `tests/wasm_audio_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `PcmRingBuffer::Write(std::span<const StereoI16>)`, `Read(std::span<StereoF32>)`, `FillFrames()`, `Underruns()`, and `Overruns()`.
- Produces: `OutputResampler(uint32_t sourceRate, uint32_t destinationRate)` and allocation-free `Process()`.

- [ ] **Step 1: Write failing wrap, underrun, and rate-conversion tests**

```cpp
TEST_CASE("PCM ring preserves frames across wrap") {
  PcmRingBuffer<8> ring;
  StereoI16 input[10] = {};
  CHECK(ring.Write({input, 6}) == 6);
  StereoF32 first[4] = {};
  CHECK(ring.Read(first) == 4);
  CHECK(ring.Write({input + 6, 4}) == 4);
  CHECK(ring.FillFrames() == 6);
}
```

Add a one-second 44.1-to-48 kHz sine test that checks frame count, channel
identity, bounded amplitude error, and output frequency.

- [ ] **Step 2: Verify RED**

Run: `cmake --build build-host && ./build-host/picoTracker_tests`

Expected: FAIL because the audio primitives are undefined.

- [ ] **Step 3: Implement atomic single-producer/single-consumer storage**

Use power-of-two indices and acquire/release atomics. Underrun fills missing
frames with zero; overrun rejects excess producer frames. The resampler owns
all scratch memory at construction and performs no callback-time allocation.

- [ ] **Step 4: Verify GREEN under normal and sanitizer builds**

Run: `cmake --build build-host && ./build-host/picoTracker_tests`

Run: `cmake -S tests -B build-host-asan -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined' && cmake --build build-host-asan && ./build-host-asan/picoTracker_tests`

Expected: all audio tests pass with no sanitizer reports.

- [ ] **Step 5: Commit**

```bash
git add sources/Adapters/wasm/audio tests
git commit -m "feat(wasm): add realtime PCM transport"
```

### Task 7: WASM AudioDriver and AudioWorklet playback

**Files:**
- Create: `sources/Adapters/wasm/audio/WasmAudio.{h,cpp}`
- Create: `sources/Adapters/wasm/audio/WasmAudioDriver.{h,cpp}`
- Create: `sources/Adapters/wasm/audio/AudioWorklet.cpp`
- Create: `web/src/stores/audio.js`
- Create: `web/src/handles/audio.js`
- Create: `web/tests/audio.test.js`
- Create: `web/e2e/audio.spec.js`
- Delete: `sources/Adapters/wasm/audio/WasmSilentAudio.{h,cpp}`
- Modify: `sources/Adapters/wasm/CMakeLists.txt`
- Modify: `sources/Adapters/wasm/system/WasmSystem.cpp`
- Modify: `web/src/components/TopBar.svelte`

**Interfaces:**
- Implements the existing `AudioDriver` contract.
- Produces C exports: `PicoTracker_Wasm_UnlockAudio()`, `PicoTracker_Wasm_GetAudioState()`, `PicoTracker_Wasm_GetAudioMetrics()`.

- [ ] **Step 1: Write failing audio state and offline-render tests**

```js
it('does not report running before a user unlock gesture', async () => {
  const audio = createAudioStore(fakeBridge)
  await audio.initialize()
  expect(audio.snapshot().state).toBe('locked')
})
```

The browser test renders a fixed tracker sequence offline and compares its PCM
hash and duration at 44.1 and 48 kHz contexts.

- [ ] **Step 2: Verify RED**

Run: `cd web && npm test -- --run tests/audio.test.js && npx playwright test e2e/audio.spec.js`

Expected: FAIL because the audio store and worklet do not exist.

- [ ] **Step 3: Implement pull-mode worklet and user-gesture unlock**

Start the worklet through Emscripten's Wasm Audio Worklets API. Its callback
reads the shared PCM ring, resamples when required, emits silence on underflow,
and instruments processing with only a monotonic clock and fixed lock-free
atomics. Metrics expose producer render duration, callback current/max duration,
the actual quantum deadline, and processing-deadline misses; trace records are
published later from the application snapshot boundary.

- [ ] **Step 4: Verify audio unit, offline, and timed playback tests**

Run: `tools/build-wasm.sh Debug && cd web && npm test -- --run tests/audio.test.js && npx playwright test e2e/audio.spec.js`

Expected: correct hash/duration, successful unlock, and no underruns after the
documented warm-up in the timed smoke test.

- [ ] **Step 5: Commit**

```bash
git add sources/Adapters/wasm/audio sources/Adapters/wasm/system web/src web/tests web/e2e
git commit -m "feat(wasm): add low-latency browser audio"
```

### Task 8: IDBFS-backed PicoTracker filesystem

**Files:**
- Modify: `sources/Adapters/wasm/filesystem/WasmFile.{h,cpp}`
- Modify: `sources/Adapters/wasm/filesystem/WasmFileSystem.{h,cpp}`
- Create: `web/src/stores/storage.js`
- Create: `web/src/handles/filesystem.js`
- Create: `web/tests/storage.test.js`
- Create: `web/e2e/persistence.spec.js`
- Modify: `sources/Adapters/wasm/CMakeLists.txt`
- Modify: `web/src/stores/runtime.js`

**Interfaces:**
- Implements existing `FileSystem` and `I_File` interfaces rooted at `/data`.
- Produces JS: `initializePersistentFs()`, `requestSync(reason)`, `flushNow(reason)`, and serialized `StorageState`.

- [ ] **Step 1: Write failing sync-serialization and path-containment tests**

```js
it('serializes overlapping persistence requests', async () => {
  const order = []
  const storage = createStorageForTest(() => deferredSync(order))
  await Promise.all([storage.requestSync('save'), storage.requestSync('import')])
  expect(order).toEqual(['start', 'finish', 'start', 'finish'])
})
```

- [ ] **Step 2: Verify RED**

Run: `cd web && npm test -- --run tests/storage.test.js`

Expected: FAIL because the storage coordinator is absent.

- [ ] **Step 3: Implement IDBFS startup and synchronous C++ file access**

Mount `/data`, call `FS.syncfs(true)` before application startup, and serialize
all `syncfs(false)` requests. Reject traversal outside `/data`. Notify the JS
coordinator after mutating C++ operations without blocking ordinary reads.

- [ ] **Step 4: Verify reload persistence**

Run: `tools/build-wasm.sh Debug && cd web && npm test -- --run tests/storage.test.js && npx playwright test e2e/persistence.spec.js`

Expected: a created project and imported WAV survive page reload and runtime restart.

- [ ] **Step 5: Commit**

```bash
git add sources/Adapters/wasm/filesystem sources/Adapters/wasm/CMakeLists.txt web/src web/tests web/e2e
git commit -m "feat(wasm): persist projects and samples with IDBFS"
```

### Task 9: Virtual disk Files panel and ZIP safety

**Files:**
- Create: `web/src/components/FilesPanel.svelte`
- Create: `web/src/handles/files.js`
- Create: `web/src/storage/zip.js`
- Create: `web/tests/files.test.js`
- Create: `web/tests/zip.test.js`
- Create: `web/e2e/files.spec.js`
- Modify: `web/package.json`
- Modify: `web/src/App.svelte`

**Interfaces:**
- Produces: `listDirectory(path)`, `uploadFiles(files, destination)`, `downloadFile(path)`, `deletePath(path)`, `exportDiskZip()`, and `previewZipRestore(bytes)`.

- [ ] **Step 1: Write failing ZIP traversal and restore-preview tests**

```js
it('rejects ZIP entries that escape the virtual disk', async () => {
  await expect(previewZipRestore(makeZip({ '../outside': 'bad' })))
    .rejects.toThrow('outside the virtual disk')
})
```

- [ ] **Step 2: Verify RED**

Run: `cd web && npm test -- --run tests/files.test.js tests/zip.test.js`

Expected: FAIL because file and ZIP handles are absent.

- [ ] **Step 3: Implement file tools with explicit conflict preview**

Support browse, mkdir, rename, delete, upload, drag/drop, download, ZIP export,
and ZIP restore. Normalize separators, reject absolute/traversal paths, bound
uncompressed size, and require an overwrite/keep-both policy before restore.

- [ ] **Step 4: Verify unit and end-to-end file operations**

Run: `cd web && npm test -- --run tests/files.test.js tests/zip.test.js && npx playwright test e2e/files.spec.js`

Expected: all operations persist and unsafe archives are rejected.

- [ ] **Step 5: Commit**

```bash
git add web/package.json web/package-lock.json web/src web/tests web/e2e
git commit -m "feat(web): add virtual disk file tools"
```

### Task 10: Host-folder mount and conflict-safe synchronization

**Files:**
- Create: `web/src/storage/hostFolder.js`
- Create: `web/src/storage/syncManifest.js`
- Create: `web/src/storage/syncCoordinator.js`
- Create: `web/src/components/ConflictDialog.svelte`
- Create: `web/tests/syncManifest.test.js`
- Create: `web/tests/syncCoordinator.test.js`
- Modify: `web/src/components/FilesPanel.svelte`
- Modify: `web/src/stores/storage.js`

**Interfaces:**
- Produces: `mountHostFolder()`, `restoreHostFolderHandle()`, `syncHostFolder(direction)`, `resolveConflict(path, policy)`, and `unmountHostFolder()`.
- `policy` is exactly `'keep-browser' | 'keep-host' | 'keep-both'`.

- [ ] **Step 1: Write failing manifest-diff and conflict tests**

```js
it('reports a conflict when browser and host changed from the same base', () => {
  const diff = compareManifests(base, changedBrowser, changedHost)
  expect(diff.conflicts.map(x => x.path)).toEqual(['/samples/kick.wav'])
})
```

- [ ] **Step 2: Verify RED**

Run: `cd web && npm test -- --run tests/syncManifest.test.js tests/syncCoordinator.test.js`

Expected: FAIL because manifest comparison is absent.

- [ ] **Step 3: Implement permission-aware bidirectional synchronization**

Store the directory handle in IndexedDB, re-check `readwrite` permission after
reload, hash files incrementally, serialize sync runs, track deletions, stop on
conflicts, and never walk above the chosen root. Unmount waits for pending writes.

- [ ] **Step 4: Verify all diff directions and permission loss**

Run: `cd web && npm test -- --run tests/syncManifest.test.js tests/syncCoordinator.test.js`

Expected: import, export, deletion, keep-browser, keep-host, keep-both, denied
permission, and interrupted-write tests pass.

- [ ] **Step 5: Commit**

```bash
git add web/src/storage web/src/components web/src/stores web/tests
git commit -m "feat(web): mount and synchronize host folders"
```

### Task 11: Web MIDI input and output

**Files:**
- Create: `sources/Adapters/wasm/midi/WasmMidiInDevice.{h,cpp}`
- Create: `sources/Adapters/wasm/midi/WasmMidiOutDevice.{h,cpp}`
- Create: `sources/Adapters/wasm/midi/WasmMidiService.{h,cpp}`
- Create: `sources/Adapters/wasm/midi/MidiByteQueue.{h,cpp}`
- Create: `web/src/stores/midi.js`
- Create: `web/src/handles/midi.js`
- Create: `web/src/components/MidiPanel.svelte`
- Create: `tests/wasm_midi_tests.cpp`
- Create: `web/tests/midi.test.js`
- Delete: `sources/Adapters/wasm/midi/WasmDisconnectedMidi.{h,cpp}`
- Modify: `tests/CMakeLists.txt`
- Modify: `sources/Adapters/wasm/CMakeLists.txt`

**Interfaces:**
- Produces C exports: `PicoTracker_Wasm_MidiInput(const uint8_t*, size_t, double)`, `PicoTracker_Wasm_MidiDrainOutput()`, and `PicoTracker_Wasm_MidiDisconnect(uint32_t)`.
- Produces JS: `requestMidiAccess()`, `selectMidiInput(id)`, and `selectMidiOutput(id)`.

- [ ] **Step 1: Write failing queue, clock, and reconnect tests**

```cpp
TEST_CASE("MIDI queue preserves realtime bytes interleaved with channel data") {
  MidiByteQueue<16> queue;
  const uint8_t bytes[] = {0x90, 60, 0xF8, 100};
  CHECK(queue.Push(bytes));
  CHECK(queue.PopAll() == std::vector<uint8_t>{0x90, 60, 0xF8, 100});
}
```

- [ ] **Step 2: Verify RED**

Run: `cmake --build build-host && ./build-host/picoTracker_tests`

Run: `cd web && npm test -- --run tests/midi.test.js`

Expected: FAIL because WASM MIDI adapters and stores are absent.

- [ ] **Step 3: Implement bounded queues and Web MIDI routing**

Request permission only from a user action. Route selected input bytes and
timestamps to the existing parser. Batch outgoing messages without dropping
clock bytes, retain output timestamps, and recover selected devices by stable id.

- [ ] **Step 4: Verify parser, permission, disconnect, and fake-port tests**

Run: `cmake --build build-host && ./build-host/picoTracker_tests`

Run: `cd web && npm test -- --run tests/midi.test.js`

Expected: input, output, clock, denial, disconnect, and reconnect tests pass.

- [ ] **Step 5: Commit**

```bash
git add sources/Adapters/wasm/midi sources/Adapters/wasm/CMakeLists.txt tests web/src web/tests
git commit -m "feat(wasm): connect Web MIDI input and output"
```

### Task 12: Bounded structured logs

**Files:**
- Create: `sources/Adapters/wasm/logging/WasmTrace.{h,cpp}`
- Create: `web/src/stores/logs.js`
- Create: `web/src/handles/logs.js`
- Create: `web/src/components/LogsPanel.svelte`
- Create: `web/tests/logs.test.js`
- Modify: `sources/Adapters/wasm/CMakeLists.txt`

**Interfaces:**
- Produces records `{ monotonicUs, wallTime, severity, category, thread, message }`.
- Produces JS: `appendLog(record)`, `setLogFilter(filter)`, `clearLogs()`, and `downloadLogs()`.

- [ ] **Step 1: Write failing bounded-store and rate-limit tests**

```js
it('bounds retained logs and reports dropped records', () => {
  const logs = createLogStore({ capacity: 3 })
  for (let i = 0; i < 5; i += 1) logs.append(makeLog(i))
  expect(logs.snapshot().records).toHaveLength(3)
  expect(logs.snapshot().dropped).toBe(2)
})
```

- [ ] **Step 2: Verify RED**

Run: `cd web && npm test -- --run tests/logs.test.js`

Expected: FAIL because the log store is absent.

- [ ] **Step 3: Implement the C++ sink and workbench viewer**

Bridge PicoTracker `Trace` and Emscripten console output into structured records.
Bound memory, coalesce repeated high-rate messages, and support severity,
category, text, pause, clear, copy, and download.

- [ ] **Step 4: Verify formatting, bounds, filters, and download**

Run: `cd web && npm test -- --run tests/logs.test.js`

Expected: tests pass with deterministic dropped-count reporting.

- [ ] **Step 5: Commit**

```bash
git add sources/Adapters/wasm/logging sources/Adapters/wasm/CMakeLists.txt web/src web/tests
git commit -m "feat(wasm): add bounded runtime logs"
```

### Task 13: Low-overhead tracing and deterministic benchmark

**Files:**
- Create: `sources/Adapters/wasm/tracing/TraceRecord.h`
- Create: `sources/Adapters/wasm/tracing/TraceRingBuffer.{h,cpp}`
- Create: `sources/Adapters/wasm/tracing/WasmProfiler.{h,cpp}`
- Create: `sources/Adapters/wasm/tracing/Benchmark.{h,cpp}`
- Create: `web/src/stores/trace.js`
- Create: `web/src/handles/trace.js`
- Create: `web/src/components/TracePanel.svelte`
- Create: `web/src/trace/chromeTrace.js`
- Create: `tests/wasm_trace_tests.cpp`
- Create: `web/tests/trace.test.js`
- Modify: `tests/CMakeLists.txt`
- Modify: selected mixer, player, UI, filesystem, input, and MIDI scope sites.

**Interfaces:**
- Produces: `WASM_TRACE_SCOPE(category, name)`, atomic metrics, `PicoTracker_Wasm_TraceStart(mask)`, `PicoTracker_Wasm_TraceStop()`, `PicoTracker_Wasm_TraceDrain(...)`, and `PicoTracker_Wasm_RunBenchmark(config)`.
- Produces valid Chrome Trace Event JSON.

- [ ] **Step 1: Write failing ring-wrap and trace-export tests**

```cpp
TEST_CASE("trace ring reports overwritten events") {
  TraceRingBuffer<2> ring;
  ring.Push(beginEvent(1)); ring.Push(endEvent(1)); ring.Push(beginEvent(2));
  CHECK(ring.Dropped() == 1);
}
```

```js
it('exports complete Chrome trace events', () => {
  expect(toChromeTrace([begin, end]).traceEvents.map(x => x.ph)).toEqual(['B', 'E'])
})
```

- [ ] **Step 2: Verify RED**

Run: `cmake --build build-host && ./build-host/picoTracker_tests`

Run: `cd web && npm test -- --run tests/trace.test.js`

Expected: FAIL because trace storage and conversion are absent.

- [ ] **Step 3: Implement fixed records, selected scopes, and benchmark**

Pre-register stable names, write allocation-free events, drain from JS, and
instrument UI, input, mixer, instruments, audio, files, and MIDI. The benchmark
uses a fixed project/event sequence/frame count and returns median, p95, p99,
maximum, misses, total work, and rendered-audio hash.

- [ ] **Step 4: Verify trace validity and disabled overhead**

Run: `cmake --build build-host && ./build-host/picoTracker_tests`

Run: `cd web && npm test -- --run tests/trace.test.js`

Run the benchmark with tracing enabled and disabled; expected audio hashes are
identical and the disabled path introduces no allocations or audio misses.

- [ ] **Step 5: Commit**

```bash
git add sources/Adapters/wasm/tracing sources/Application sources/Services tests web/src web/tests
git commit -m "feat(wasm): add exportable performance tracing"
```

### Task 14: Complete MatrixOS-style workbench and recovery states

**Files:**
- Create: `web/src/components/TopBar.svelte`
- Create: `web/src/components/LeftNav.svelte`
- Create: `web/src/components/SettingsPanel.svelte`
- Create: `web/src/components/AboutPanel.svelte`
- Create: `web/src/components/ErrorBoundary.svelte`
- Create: `web/src/stores/settings.js`
- Create: `web/tests/settings.test.js`
- Create: `web/e2e/recovery.spec.js`
- Modify: all workbench panels for shared layout and status.
- Modify: `web/src/App.svelte`
- Modify: `web/src/app.css`
- Modify: `web/THIRD_PARTY_NOTICES.md`

**Interfaces:**
- Produces persisted settings schema version 1.
- Produces recoverable states for isolation, WASM, audio, storage, host-folder, MIDI, and fatal C++ errors.

- [ ] **Step 1: Write failing settings-migration and recovery tests**

```js
it('migrates an unversioned key map without discarding audio settings', () => {
  expect(migrateSettings({ keyMap: legacyMap, audioBufferFrames: 1024 }))
    .toMatchObject({ version: 1, keyMap: legacyMap, audioBufferFrames: 1024 })
})
```

- [ ] **Step 2: Verify RED**

Run: `cd web && npm test -- --run tests/settings.test.js && npx playwright test e2e/recovery.spec.js`

Expected: FAIL because settings migrations and recovery surfaces are absent.

- [ ] **Step 3: Finish navigation, responsive layout, settings, and errors**

Implement Device, Files, MIDI, Logs, Trace, Settings, and About navigation.
Remove every inherited HID/Serial reference. Add actionable recovery buttons
for each documented error and preserve dirty storage through runtime restart.

- [ ] **Step 4: Verify all workbench and recovery flows**

Run: `cd web && npm test -- --run && npx playwright test`

Expected: unit and browser suites pass at desktop and tablet viewports.

- [ ] **Step 5: Commit**

```bash
git add web
git commit -m "feat(web): complete PicoTracker WASM workbench"
```

### Task 15: CI, deployment, compatibility, and full acceptance

**Files:**
- Create: `.github/workflows/wasm.yml`
- Create: `docs/wasm/BUILD.md`
- Create: `docs/wasm/DEPLOY.md`
- Create: `docs/wasm/STORAGE.md`
- Create: `docs/wasm/TRACING.md`
- Create: `docs/wasm/TESTING.md`
- Create: `web/public/_headers`
- Create: `web/tools/verify-dist.mjs`
- Create: `web/e2e/full-acceptance.spec.js`
- Modify: `README.md`

**Interfaces:**
- Produces CI artifacts: `picotracker-wasm-dist` and Playwright results.
- Produces `npm run verify:dist` and a complete manual hardware/browser checklist.

- [ ] **Step 1: Write the failing static-distribution verifier**

```js
for (const required of ['index.html', 'picotracker.js', 'picotracker.wasm']) {
  if (!existsSync(resolve(dist, required))) throw new Error(`missing ${required}`)
}
if (scanForRemoteRuntimeDependencies(dist).length) throw new Error('runtime CDN dependency found')
```

- [ ] **Step 2: Verify RED**

Run: `cd web && npm run verify:dist`

Expected: FAIL until all required artifacts, notices, and header declarations are present.

- [ ] **Step 3: Add CI, deployment metadata, compatibility fallbacks, and docs**

CI installs pinned Emscripten and Node versions, initializes submodules, runs
host tests, builds WASM, runs Vitest and Playwright, verifies the static bundle,
and also runs existing Node/Pico checks that are available in CI. Document
Chrome/Edge full support and Firefox/Safari fallback behavior.

- [ ] **Step 4: Run the complete completion audit**

Run:

```bash
cmake -S tests -B build-host
cmake --build build-host
./build-host/picoTracker_tests
tools/build-wasm.sh Release
cd web
npm test -- --run
npx playwright test
npm run build
npm run verify:dist
```

Then manually verify all 14 completion criteria in the design document against
Chrome and Edge, including real audio, a real mounted folder, a physical Web
MIDI device, trace export, reload persistence, and a 30-minute playback run.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/wasm.yml docs/wasm web/public web/tools web/e2e README.md
git commit -m "ci(wasm): verify and document static workbench"
```
