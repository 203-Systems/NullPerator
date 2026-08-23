# Task 8 report — IDBFS-backed PicoTracker filesystem

## Delivered

- The existing `/data` C++ filesystem now mounts Emscripten IDBFS through a
  private-Module `preRun` hook. A named run dependency holds startup through
  `FS.syncfs(true)`; it is balanced exactly once on either outcome. A failed
  population is saved by the coordinator and thrown by
  `onRuntimeInitialized`, before Emscripten can invoke proxied C `main`.
- `createStorageCoordinator` provides observable initializing/ready/syncing/
  failed state, serialized `requestSync`/`flushNow`, coalesced dirty work, and
  a re-entrant listener-safe final drain. Concurrent `syncfs(false)` calls are
  never issued.
- Successful C++ writes notify only after `Sync`/`Close`; create/truncate-only
  files are included. mkdir, copy, move, and deletion notify once on success.
  The notification bridge schedules browser-main work and never waits on
  IndexedDB from the synchronous C++ API.
- Runtime shutdown is now C++ stop acknowledgement, storage flush, then
  pthread termination. Every stage is retryable: an acknowledgement timeout
  retains the running module and never flushes/terminates; a later Stop only
  confirms the original shutdown request. A failed flush retains stopped MEMFS
  for a later retry; restart cannot construct a replacement module until all
  prior stages have succeeded.
- `/data` containment tests cover absolute paths, traversal, symlink escape,
  copy/move destinations, root deletion, and failed-operation parent rollback.
  `r+` never creates parents; copy/move validate their source/target before
  creating directories and roll back any newly-created empty parents on later
  failure. The JS acceptance seam is gated
  to `?storage-test=1`, exposes only contained read/write/exists/flush
  operations, and is removed on termination. It is not a Task 9 file API.

## Automated persistence acceptance

The browser test deletes the `/data` IDBFS database, writes a generated
44-byte synthetic WAV plus project marker, flushes, restarts, reloads, and
byte-compares both artifacts at each step. It does not add factory content to the
repository. The supplied factory tree was not copied or modified.

## Verification

- Host Debug: **41/41 passed**, **192,894 assertions**.
- ASan host: **41/41 passed**, no sanitizer diagnostics.
- TSan host: **41/41 passed**, no sanitizer diagnostics.
- Debug WASM, local Emscripten 6.0.5: `picotracker_wasm` and the core-link
  closure gate passed with `-lidbfs.js`.
- Vitest: **6 files, 49 tests passed**, including populate failure, overlapping/follow-up/reentrant
  syncs, staged shutdown acknowledgement/flush recovery (including a failed
  request before C++ leaves Ready), restart block, path containment, and the
  narrow acceptance seam.
- Fresh `CI=1` Playwright: **8/8 passed** (48.4s), including the dedicated
  persistence test (1/1, 41.1s in the final focused run), application boot/restart, input,
  default audio recovery, and the browser numeric oracle.

## Deliberate scope boundary

No files panel, upload/download, ZIP handling, File System Access API, or
host-folder mirroring was implemented. Those remain Tasks 9 and 10.
