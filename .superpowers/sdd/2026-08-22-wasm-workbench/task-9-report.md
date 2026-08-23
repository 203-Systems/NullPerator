# Task 9 report — virtual disk Files panel and ZIP safety

## Delivered

- Added `fflate` **0.8.3** as a pinned local dependency (with the pnpm lock
  update); the workbench never uses a ZIP CDN or a machine-local tool.
- Added the only production browser filesystem boundary in
  `web/src/handles/files.js`. It normalizes every incoming path through the
  Task 8 `/data` containment guard and exposes browse, mkdir, rename, delete,
  multi-upload, download, ZIP export, preview, and transactional restore.
  Components and stores never receive a raw Emscripten `FS` object.
- Added ZIP central-directory plus local-record preflight in
  `web/src/storage/zip.js` before decompression or mutation. Every entry
  verifies the local signature, flags, method, raw filename, bounds, no-data-
  descriptor local fields or descriptor CRC/sizes, and non-overlap. Deflate is
  decoded with `fflate`'s chunked `Inflate`, with incremental size accounting
  and CRC32 verification; `unzipSync` is not used. It rejects malformed layouts, encrypted and
  unsupported entries, NUL/backslash/absolute/drive/UNC/traversal names,
  duplicates, and Unix symlink/special types when metadata identifies them.
  Limits are 4,096 entries, 64 MiB compressed input, 32 MiB per file, and
  128 MiB declared/actual inflated data.
- Restore has an explicit preview and only accepts `overwrite` or deterministic
  `keep-both` (`name (2).ext`) policies. The archive is fully parsed and
  planned first. Restore snapshots only overwrite targets (capped at 128 MiB),
  journals files/directories created by that attempt, and rolls back those
  affected paths only; unrelated disk content is never read or copied. A
  failed application makes no persistence flush request. If rollback itself
  fails, storage is failed closed and every later sync/flush rejects rather
  than presenting a falsely recoverable disk. This also suppresses a queued
  follow-up `syncfs(false)` when failure occurs during an already-active drain.
- Added `FilesPanel` plus a small files store: directories are listed first,
  with navigation/breadcrumbs, create/rename/delete, multi-file input and
  drag/drop, file downloads, ZIP export, ZIP preview/conflict choices, and
  actionable status/error text. The device canvas stays mounted but hidden
  while Files is visible, so a global Restart retains the canvas required by
  the existing pthread transfer topology.
- Files mutations use `storage.runMutation(reason, callback)`, an exclusive
  storage barrier rather than direct flushes. It waits for an active
  `syncfs(false)` drain, serializes all Files MEMFS mutations, and performs one
  release sync before allowing the next mutation. Requests arriving during a
  barrier only mark dirty and are included in that release sync; no concurrent
  IDBFS sync starts. A fail-closed rollback blocks the release sync as well.
  Upload preflights all `File.name`/`File.size` values before calling
  `arrayBuffer()`, then checks the actual byte count while sequentially
  applying a journal. Recursive deletion has a separately bounded subtree
  backup and rollback. The query-gated Task 8 test seam remains narrow and is
  used only to byte-verify browser acceptance output.
- Archive and disk directories merge when both sides are directories. Only
  file/file paths enter `overwrite` or deterministic `keep-both`
  (`name (2).ext`) planning; file/directory, directory/file, and file-ancestor
  conflicts remain hard failures. ZIP export now produces its Blob and clicks
  the download anchor synchronously in the original user event, so browser
  trusted-gesture download policy cannot suppress a valid export.

## TDD and browser acceptance

The first focused run was a real RED because both `files.js` and `zip.js` were
absent. The focused tests now cover directories-first listing, containment and
no overwrite, batch upload flushing/rollback, upload filename and duplicate
rejection, export preflight before file reads, archive path/size/entry, local
header/data-descriptor/CRC, and declared-vs-actual protections, explicit
conflict policy, deterministic
multi-suffix rename, matching archive/disk-directory merge, directory/file
collision checks, explicit archive directories, and affected-target rollback
after an injected second-file write failure. Storage tests additionally cover
waiting for an existing drain before mutation entry, strict mutation ordering,
request-during-mutation coalescing, fail-closed rollback with no later sync,
and direct-flush bypass prevention for every Files write operation.

The fresh Files Playwright test creates only synthetic bytes. It clears the
IDBFS database, uploads two in-memory files, makes/navigates a directory,
renames, downloads, drop-uploads, exports, previews a conflict, restores via
keep-both, rejects a traversal archive without mutation, restarts, and
byte-compares the persisted results. No factory content was copied or changed.

## Verification

- Focused ZIP/files/storage Vitest: **3 files, 34 tests passed**.
- Full Vitest: **8 files, 76 tests passed**.
- Fresh `CI=1` full Playwright: **9/9 passed**, including Files, persistence,
  device restart, input, default audio recovery, and browser oracle.
- Debug WASM with local Emscripten **6.0.5**: passed, including the
  `picotracker_wasm` core-link closure gate.
- Host Debug, ASan, and TSan: each **41/41 passed**, **192,894 assertions**;
  no sanitizer diagnostics in the test runs.

## Scope boundary

Task 10 host-folder mounting, permission persistence, manifest comparison, and
host/browser synchronization are deliberately not implemented. The supplied
factory-content tree was only treated as an optional read-only local acceptance
source and was not needed for automated verification.
