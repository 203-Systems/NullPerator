# Task 8 brief — IDBFS-backed PicoTracker filesystem

## Goal

Replace the browser build's ephemeral `/data` MEMFS contents with IDBFS while
preserving PicoTracker's synchronous `FileSystem`/`I_File` API. Application
startup must not begin until the initial IndexedDB population has completed,
and runtime shutdown/restart must flush pending mutations before workers are
terminated.

Commit subject: `feat(wasm): persist projects and samples with IDBFS`

## Scope

- Mount IDBFS at the existing `/data` path. Do not change paths visible to the
  C++ core.
- Add a JS storage coordinator exposing:
  - `initializePersistentFs(module)`
  - `requestSync(reason)`
  - `flushNow(reason)`
  - an observable, serialized `StorageState`
- Use an Emscripten `preRun(module)` hook plus a run dependency for
  `FS.syncfs(true)`. The private Emscripten 6 `Module` passed to the pre-run
  callback is authoritative. The C `main`/`Application::Init` path must not run
  before initial population succeeds.
- Export only the Emscripten runtime methods needed by the coordinator (`FS`
  and run-dependency methods). Do not switch to `noInitialRun` or manually call
  `_main`.
- C++ reads remain synchronous. Successful mutations notify the coordinator
  without waiting for IndexedDB:
  - `WasmFile` coalesces writes and notifies after successful `Sync`/`Close`.
  - mkdir, delete, copy, and move notify once after success.
  - failed/no-op operations do not mark storage dirty.
- Serialize all `syncfs(false)` calls. Requests arriving during a sync must
  cause a later sync and must never run concurrently or be silently lost.
- Runtime stop ordering is: release input/audio, request and await C++ stop,
  flush persistent storage, then terminate pthreads. A flush failure is an
  actionable runtime/storage failure and must not be reported as clean idle.
- Restart waits for the same flush and then creates a fresh module which again
  performs initial `syncfs(true)`.

## Safety and containment

- Keep the existing canonical `/data` containment checks and extend host tests
  for absolute paths, `..`, symlink escape, copy/move destinations, and root
  deletion.
- JS filesystem helpers accept only normalized paths inside `/data`; Task 9
  will add user-facing file operations and ZIP handling.
- Do not implement the File System Access API or host-folder mirroring here;
  that is Task 10.
- Do not copy the complete factory content into the repository or static
  bundle.

## External acceptance fixture

Read-only source:
`/Users/nengzhuocai/Downloads/factory-content-main`

- About 194 MiB; never copy the full tree into git.
- `default-current.txt` selects `oneCycAc`.
- Minimal Pico project fixture:
  - `projects/pico/oneCycAc/lgptsav.dat`
  - `projects/pico/oneCycAc/samples/AKWF_0906.wav`
- The project is CC-BY-SA-4.0 and the sample has its own referenced factory
  licensing. For automated tests, import bytes from the user's external tree
  at test time when available; otherwise generate a tiny synthetic WAV and a
  test project through the runtime APIs. Do not add third-party sample bytes to
  the repository in this task.

## TDD and verification

1. Record RED tests before implementation:
   - overlapping sync requests are strictly serialized;
   - a dirty request during an active sync schedules a follow-up sync;
   - initialization waits for `syncfs(true)` and propagates its error;
   - shutdown waits for `syncfs(false)` before termination;
   - path traversal/symlink escape and failed mutations do not notify;
   - reload and stop/restart retain a created project and WAV.
2. Run host filesystem tests and focused Vitest.
3. Build Debug WASM and run fresh Playwright persistence tests with a unique
   IndexedDB namespace or explicit cleanup so tests are deterministic.
4. Run the full host, Vitest, and default Playwright regressions.
5. Write `task-8-report.md`, request independent review, fix every
   Critical/Important issue, and only then create the single task commit.

## Explicit non-goals

- Files panel, upload/download, ZIP export/restore (Task 9).
- Host-directory handles, manifests, conflict resolution (Task 10).
- Persistent settings UI polish (Task 14).
- Bundling the full factory library.
