# Task 9 brief — virtual disk Files panel and ZIP safety

## Goal

Turn the existing MatrixOS-style Files placeholder into a complete browser
virtual-disk panel backed by Task 8's `/data` IDBFS coordinator. Users can
browse, create, rename, delete, upload, drag/drop, download, export a ZIP, and
restore a ZIP only after an explicit conflict preview.

Commit subject: `feat(web): add virtual disk file tools`

## Architecture and dependency

- Add `fflate@0.8.3` through pnpm and update `pnpm-lock.yaml`. Use its official
  ZIP APIs locally; there is no CDN/runtime link.
- `web/src/handles/files.js` is the only production wrapper around Emscripten
  `FS`; every input path passes the Task 8 `/data` normalizer.
- `web/src/storage/zip.js` owns archive parsing, validation, preview, export,
  and restore planning. UI components never parse ZIP records themselves.
- `FilesPanel.svelte` owns presentation only. It consumes a file store/handle
  from the active runtime and never reaches raw `Module.FS`.
- Every successful mutation requests Task 8 persistence. Multi-file upload and
  restore issue one final serialized flush rather than one IndexedDB sync per
  entry.

## Required file operations

- `listDirectory(path)` returns deterministic, directories-first entries with
  name, absolute path, kind, and byte size.
- `mkdir`, `rename`, `deletePath`, `uploadFiles`, drag/drop upload,
  `downloadFile`, `exportDiskZip`, `previewZipRestore`, and `restoreZip`.
- Breadcrumb navigation cannot escape `/data`.
- Rename/move must not overwrite implicitly. Delete is explicit and recursive
  only after confirmation in the panel.
- Object URLs used for downloads are revoked after the click/task completes.
- The Files panel has accessible labels, keyboard-operable buttons, loading,
  empty, dirty/syncing, success, and actionable error states.

## ZIP safety contract

- Reject archive entry names containing NUL, backslashes, absolute paths,
  Windows drive/UNC forms, empty normalized names, or any `..` traversal.
- Treat directory entries separately and reject entries whose type cannot be
  represented as an ordinary file/directory in MEMFS (including symlinks when
  metadata is available).
- Hard limits (constants and tests):
  - at most 4,096 entries;
  - at most 32 MiB per file;
  - at most 128 MiB total uncompressed bytes;
  - at most 64 MiB compressed upload bytes.
- Inspect declared uncompressed sizes before inflating when the library API
  exposes them, and enforce the actual inflated totals again afterward.
- `previewZipRestore(bytes)` is side-effect free and reports creates,
  overwrites, directories, and conflicts.
- Restore requires one explicit policy: `overwrite` or `keep-both`.
  `keep-both` chooses deterministic `name (2).ext`, `name (3).ext`, etc.
- Stage and validate the entire bounded archive before mutating MEMFS. If an
  apply step fails, restore the prior in-memory files/directories so a later
  IDBFS flush cannot persist a partial restore.
- ZIP export uses relative forward-slash names and excludes internal test
  seams; importing an exported disk round-trips byte-for-byte.

## External factory acceptance

Read-only source:
`/Users/nengzhuocai/Downloads/factory-content-main`

- Do not copy the 194 MiB tree into git or the production bundle.
- A local acceptance test may upload:
  - `projects/pico/oneCycAc/lgptsav.dat`
  - `projects/pico/oneCycAc/samples/AKWF_0906.wav`
- The normal CI test uses generated marker/project bytes and a tiny synthetic
  WAV so it has no external licensing or machine-path dependency.

## TDD and acceptance

1. Record real RED tests for traversal, absolute/drive/backslash paths, entry
   and byte limits, overwrite/keep-both preview, rollback on write failure,
   deterministic listing, no implicit overwrite, and one-flush batching.
2. Implement handles/store/ZIP logic, then the Svelte panel.
3. Playwright acceptance must upload/drop files, navigate, rename, download,
   export, preview restore, choose a conflict policy, reload/restart, and prove
   bytes persist. Unsafe archives must cause no filesystem mutation.
4. Run full Vitest, Debug WASM, host regressions, and fresh Playwright.
5. Write `task-9-report.md`, request independent review, fix every
   Critical/Important finding, then create the single task commit.

## Non-goals

- Native host directory handles, permissions, manifests, or bidirectional
  synchronization (Task 10).
- MIDI/log/trace panels (Tasks 11–13).
- Final shared panel polish (Task 14).
