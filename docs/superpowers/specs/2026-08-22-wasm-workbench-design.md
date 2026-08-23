# PicoTracker WebAssembly Workbench Design

Status: approved in chat on 2026-08-22; awaiting review of this written form.

## Purpose

Build a self-contained static web version of PicoTracker that runs the complete
existing C++ application and UI in WebAssembly. The site must provide normal
audio playback, persistent projects and samples, Web MIDI, runtime logs, and
performance tracing. Chrome and Edge are the full-feature targets. Firefox and
Safari receive a documented reduced mode where unsupported browser APIs are
unavailable without preventing PicoTracker from running.

The result is a development and performance-analysis environment as well as a
usable browser build. It is not an ESP32-S3 emulator: browser measurements are
valid for algorithmic hotspots, allocations, call paths, and workload
comparisons, but do not reproduce ESP32 cache, PSRAM, I2S, or FreeRTOS timing.

## Scope

### Required

- Run every existing PicoTracker view through the current C++ `UIFramework`.
- Render the 240 by 240 display into a browser canvas with nearest-neighbor
  integer scaling.
- Provide complete keyboard, pointer, and multi-touch input.
- Play 44.1 kHz stereo audio continuously through Web Audio.
- Persist projects, configuration, themes, and WAV samples across reloads.
- Optionally use a user-selected host folder as the virtual disk's synchronized
  backing directory.
- Import and export individual files and complete ZIP backups.
- Connect browser MIDI inputs and outputs to the existing MIDI service.
- Display application and platform logs.
- Record low-overhead performance events and export Chrome Trace Event JSON.
- Produce a static deployment bundle with no application backend.
- Provide automated tests and documentation for build, deployment, storage,
  MIDI permissions, audio startup, and tracing.

### Explicitly excluded

- HID integration and HID workbench panels.
- Serial integration and serial workbench panels.
- Emulation of RP2040, ESP32-S3, Pico SDK, ESP-IDF, I2S, PSRAM, or physical SD
  hardware.
- Exact comparison of browser timing with embedded hardware timing.
- A server-side project store or user account system.

## Source and licensing strategy

The workbench will copy and adapt relevant source from the MatrixOS
`Devices/MystrixSim/WebUI` implementation rather than link it as a submodule,
package, runtime dependency, or CDN asset. The copied portions are MIT licensed;
their copyright and license notice must be retained in the repository and in
the web distribution's third-party notices. PicoTracker remains BSD-3-Clause.

The reusable parts are the Svelte/Vite workbench layout, WASM lifecycle store,
status handling, file tools, log presentation, and test patterns. Mystrix
device rendering, firmware packaging, Python, HID, Serial, and device-specific
RPC code will not be copied.

## Architecture

```text
Browser main thread
  Svelte/Vite workbench
    Top bar and status
    PicoTracker canvas and virtual controls
    Files, MIDI, Logs, Trace, Settings, About
             |
             | exported functions, events, SharedArrayBuffer
             v
Emscripten runtime
  PicoTracker application pthread
    Application / UIFramework / Player / Mixer / Instruments
    WASM display, input, system, timer, filesystem, MIDI and audio adapters
             |
             | lock-free stereo PCM ring buffer
             v
  WASM AudioWorklet
    real-time pull callback -> Web Audio destination
```

The application target will use Emscripten pthreads and
`PROXY_TO_PTHREAD`. The browser main thread stays responsive for DOM and
workbench operations. The static host must return:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

The build must fail clearly if required Emscripten capabilities are missing.
At runtime, lack of cross-origin isolation must produce an actionable workbench
error rather than an opaque WASM startup failure.

## Repository layout

The intended layout is:

```text
sources/Adapters/wasm/
  audio/
  filesystem/
  gui/
  main/
  midi/
  mutex/
  platform/
  system/
  timer/
  CMakeLists.txt

web/
  package.json
  vite.config.js
  public/
  src/
    components/
    handles/
    stores/
  tests/
  tools/

cmake/wasm/
docs/wasm/
```

The root CMake project will gain an explicit WASM build path. Pico and Node
build behavior must remain unchanged. Generated Emscripten assets are copied
into Vite's build input and emitted as a self-contained `web/dist` bundle.

## Runtime lifecycle

1. The Svelte shell loads and checks browser capabilities.
2. It establishes cross-origin isolation, storage, audio, and MIDI status.
3. IDBFS is mounted and populated from IndexedDB.
4. If a host-folder handle exists, its permission and synchronization state are
   checked without overwriting either side.
5. The WASM module and pthread pool start only after the initial filesystem
   population finishes.
6. The PicoTracker application initializes its platform services and renders
   the first frame.
7. Audio starts only after a user gesture unlocks the AudioContext.
8. Restart first flushes storage, stops audio and MIDI, terminates workers, and
   then creates a fresh module instance.
9. Page hide and unload request a best-effort final storage flush and release
   all active inputs.

Every stage has an explicit state and error. The shell must never display
"ready" before the C++ application, persistent filesystem, and canvas are
usable.

## Complete C++ UI and display

The existing C++ application remains the only implementation of tracker views
and UI behavior. The Svelte application provides a host workbench, not a second
tracker UI.

The WASM GUI adapter implements the existing GUI interfaces using SDL2. The
historical SDL adapter is behavioral reference material, but the restored
adapter must use current interfaces and SDL2 APIs. Emscripten's SDL2 port owns
the canvas surface. Rendering uses the application's native 240 by 240 logical
resolution and nearest-neighbor scaling. Canvas resize changes presentation
only and cannot change tracker coordinates.

Frame counters and display durations are exposed to tracing. Screenshots can be
captured deterministically for regression tests.

## Input

A single Svelte input store owns keyboard, pointer, touch, and virtual-button
state. Events cross one WASM bridge into the platform event manager.

- Use the Node device layout: WASD directions, J/K Enter/Edit, X for Alt, and
  C as START (tap under 500 ms emits Play; hold or chord behaves as Nav).
- Expose every PicoTracker action through keyboard and virtual controls.
- Keep the Node key map fixed; do not expose or persist user remapping.
- Support chords and multi-touch presses.
- Do not forward browser key-repeat events; use PicoTracker's own repeat logic.
- Release all inputs on blur, page hide, pointer cancel, runtime restart, and
  input-device teardown.
- Keep workbench shortcuts separate from tracker input while the canvas is
  focused.
- Prevent browser scrolling or navigation only for keys consumed by the
  tracker.

## Audio

PicoTracker's engine stays fixed at 44.1 kHz stereo. A WASM `AudioDriver`
implementation receives interlaced signed 16-bit buffers from the existing
`AudioOutDriver` and writes them to a power-of-two lock-free ring buffer in
shared WASM memory.

A dedicated WASM AudioWorklet pulls 128-frame blocks and writes float output to
Web Audio. The real-time callback cannot allocate, log, lock, access the
filesystem, perform I/O, or wait on the application thread. Callback timing
uses only a monotonic clock and fixed lock-free atomics; trace publication stays
on the application snapshot boundary.

The workbench requests a 44.1 kHz AudioContext. If the actual context uses a
different rate, the adapter resamples only at the platform boundary so core
playback timing and instrument behavior remain 44.1 kHz. The resampler must
preserve pitch and playback duration and be covered by deterministic tests.

Audio startup requires a user gesture. The UI exposes locked, starting,
running, suspended, and failed states. It also exposes buffer fill, callback
current/maximum duration, processing deadline, producer render duration,
underrun, overrun, and processing-deadline-miss counters. The producer duration
covers synchronous mixer render through the PCM ring write. A callback miss
means its unquantized processing duration exceeded the actual quantum-frame /
destination-rate budget; it is not callback arrival jitter. Buffer size is
configurable within safe bounds and persisted in settings.

## Persistent virtual disk

IDBFS is the always-available backing store. The adapter mounts a dedicated
directory such as `/data` and maps PicoTracker's filesystem root to it. Startup
must call `syncfs(true)` before C++ code accesses configuration or projects.

Writes remain synchronous from C++'s perspective. JavaScript schedules a
debounced `syncfs(false)` after mutating operations and performs explicit syncs
after project saves, sample edits, imports, deletes, runtime restart, and user
requests. Sync operations are serialized; overlapping saves cannot reorder or
silently discard changes.

The Files panel supports directory browsing, creation, rename, deletion,
upload, download, drag and drop, and ZIP import/export. ZIP restore shows a
preview and conflict policy before changing the disk. Storage quota and sync
errors remain visible until acknowledged.

## Host-folder backing

Chrome and Edge can use the File System Access API after the user selects a
folder. The chosen directory handle is stored in IndexedDB. Because browser
permissions can return to `prompt`, restoration never assumes access; the
workbench requests it through a user gesture when necessary.

This is a synchronized backing directory rather than a falsely advertised
POSIX mount. C++ continues to use the synchronous WASM filesystem while a JS
coordinator mirrors changes at safe synchronization points.

Synchronization maintains a manifest containing relative path, entry type,
size, modification time, and content hash. It supports:

- initial host-to-virtual import;
- virtual-to-host flush after PicoTracker saves;
- manual bidirectional sync;
- deletion tracking;
- conflict detection when both sides changed;
- explicit keep-browser, keep-host, and keep-both resolutions;
- safe unmount after pending writes finish.

The selected directory is the root boundary. Parent traversal, symlink escape,
and access outside that directory are rejected. Firefox and Safari use IDBFS
plus ZIP import/export and show host-folder mounting as unsupported.

## Web MIDI

The MIDI panel is functional, not a placeholder. It requests Web MIDI access
only after a user gesture and lists current inputs and outputs. Selections are
remembered by stable device identity where available and recovered after a
device reconnects.

Incoming bytes enter a bounded thread-safe queue and are consumed by a WASM
`MidiInDevice`. Outgoing `MidiMessage` objects are passed to JavaScript and sent
through the selected `MIDIOutput`, preserving timestamps for clock-sensitive
messages. Disconnects stop the affected path, release queued state, and show a
recoverable status without stopping PicoTracker.

Tracing correlates each accepted browser input batch with the first byte
actually processed by `MidiInDevice`, and each accepted output packet with the
browser-main drain that precedes `MIDIOutput.send`. The latter measures queue
handoff only: a future scheduled send timestamp remains semantically separate
and is never counted as output latency.

The general Log panel records MIDI initialization and errors. There is no
separate MIDI logging product and no HID or Serial bridge.

## Logs

The WASM console and PicoTracker `Trace` backend feed a bounded log store. Each
entry contains monotonic time, wall time when available, severity, category,
thread, and message. The panel supports severity/category filtering, text
search, pause, clear, copy, and download. A high-rate source is rate-limited
with an explicit dropped-count record instead of growing memory without bound.

## Performance tracing

Tracing uses fixed-size records in a shared lock-free ring buffer. Records
contain event type, timestamp, thread, category, stable name identifier, and
optional numeric arguments. Instrumentation cannot allocate in measured audio
or rendering paths.

Initial categories are:

- application/UI frame;
- input-to-frame latency;
- player and mixer render;
- instrument render;
- audio producer and AudioWorklet callback;
- ring-buffer fill, underrun, overrun, callback duration/deadline, and
  processing-deadline miss;
- filesystem reads, writes, imports, and synchronization;
- MIDI input/output latency.

The Trace panel provides start/stop, duration, category selection, summary
statistics, hottest scopes, and export to Chrome Trace Event JSON. A fully
disabled build/runtime path adds only a predictable condition check.

A deterministic benchmark runs a fixed project, event sequence, and frame
count. It reports median, p95, p99, maximum, total work, audio misses, and a
hash of rendered audio so optimization comparisons catch behavioral changes.

## Workbench interface

The copied MatrixOS visual shell is adapted to PicoTracker branding and these
sections:

- **Device**: canvas, virtual controls, focus and audio-unlock status;
- **Files**: IDBFS browser, host-folder state, synchronization and ZIP tools;
- **MIDI**: permission, device selectors, connection state and test action;
- **Logs**: runtime log viewer;
- **Trace**: capture, metrics, hotspots and export;
- **Settings**: display scale, audio buffering, volume and trace level;
- **About**: commit, build time, Emscripten version and licenses.

The top bar shows runtime, audio, storage and MIDI status plus restart. HID and
Serial sections and all related code are absent. The layout remains usable on
desktop and tablet; phone layout may stack panels but is not required to mimic
the physical device dimensions.

## Failure handling and recovery

- Missing cross-origin isolation: show required headers and do not start WASM.
- WASM load or pthread failure: preserve storage, capture logs, allow restart.
- Audio permission or initialization failure: keep UI usable and offer retry.
- Audio underrun: output silence for the missing frames, increment metrics, and
  never block the real-time thread.
- IDBFS failure or quota exhaustion: keep dirty state visible and offer export.
- Host permission loss: detach safely from host sync while retaining IDBFS.
- Host sync conflict: pause affected writes until the user resolves it.
- MIDI permission denial or disconnect: disable MIDI only and allow retry.
- C++ fatal error: flush available diagnostics and offer runtime restart without
  clearing data.

## Verification strategy

### C++ tests

- WASM adapter units with host-testable boundaries.
- Input mapping, chord, repeat, and release-all behavior.
- Audio ring buffer wrap, underflow, overflow, and ordering.
- 44.1 kHz to browser-rate resampling duration, pitch, channel and bounds.
- Filesystem path containment and synchronization manifest logic.
- Trace ring-buffer wrap, dropped events and disabled overhead.
- MIDI byte parsing, queues, timestamps and disconnect cleanup.

### JavaScript tests

- WASM lifecycle state machine and restart cleanup.
- Input store and focus handling.
- IDBFS sync serialization and error reporting.
- Host-folder diff, conflict and resolution policies.
- MIDI permission, selection and reconnect behavior.
- Log bounding/filtering and trace JSON generation.

### Browser end-to-end tests

- Static bundle boots in headless Chromium with isolation headers.
- Every virtual button reaches the expected C++ action.
- Golden screenshots cover representative views and modal dialogs.
- A project survives save, reload and runtime restart.
- WAV import, preview, edit, save and reload completes.
- Offline audio rendering matches an expected content hash.
- A timed playback smoke test has no underruns after warm-up.
- MIDI input/output is tested with a deterministic browser-side fake; physical
  device smoke testing is documented separately.
- Host-folder permission UI and manual real-folder smoke testing are documented
  because automated browser permission prompts are restricted.
- Trace capture exports valid JSON containing required categories.

### Existing targets

Node and Pico builds and their relevant tests must remain green. WASM-only code
stays under its adapter and compile definitions. Shared changes require tests on
at least one embedded target plus WASM.

## Deployment

`web/dist` is a static artifact containing HTML, CSS, JavaScript, WASM, worker,
AudioWorklet, fonts, icons, and license notices. No runtime CDN or MatrixOS
repository connection is allowed. Deployment documentation includes MIME types,
COOP/COEP headers, cache rules that keep JS and WASM versions consistent, and a
local preview command with the same headers.

## Completion criteria

The project is complete only when all of the following are demonstrated:

1. Every current PicoTracker view can be entered, drawn, and operated.
2. All tracker actions have keyboard and virtual-control input paths.
3. Audio plays continuously with correct pitch and duration at both 44.1 and
   48 kHz browser output rates.
4. Projects and WAV samples survive reload and runtime restart.
5. WAV import, preview, edit, save, and reload work end to end.
6. Host-folder synchronization handles changes, deletions and conflicts without
   silent data loss.
7. Web MIDI input, output, clock, permission and reconnect behavior work.
8. Logs remain bounded and downloadable.
9. Tracing captures required categories, exports valid trace JSON, and can be
   disabled without material audio impact.
10. Startup and all documented failure modes have recoverable UI states.
11. Chrome and Edge pass the complete automated and manual acceptance suite.
12. Firefox and Safari successfully run the documented IDBFS/ZIP fallback when
    their available WASM capabilities permit it.
13. The static deployment works with documented headers and contains all assets
    and license notices.
14. Existing Node and Pico behavior is not regressed.

## Implementation sequencing constraints

The implementation plan must be incremental and keep each milestone runnable:

1. Toolchain and minimal WASM executable.
2. Current C++ core plus complete canvas UI.
3. Input and virtual controls.
4. Basic audio, followed by AudioWorklet hardening and resampling.
5. IDBFS persistence and file tools.
6. Host-folder synchronization.
7. Web MIDI.
8. Logs and tracing.
9. Workbench polish, compatibility fallbacks and failure recovery.
10. Full regression suite, documentation and static deployment verification.

Each independently reviewable fix or feature receives its own commit. A later
milestone cannot hide a failing earlier acceptance test.
