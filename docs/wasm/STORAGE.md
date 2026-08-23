# Browser storage and host-folder synchronization

## IDBFS virtual disk

`/data` is PicoTracker's filesystem root in the browser. Startup populates it
from IndexedDB before the C++ application starts. Mutations schedule serialized
flushes, and explicit flushes occur during imports, saves, restart, and normal
shutdown. Projects, configuration, themes, and samples therefore survive a
runtime restart and page reload on the same origin and browser profile.

Each observed mutation receives a monotonically increasing persistence
generation. A generation becomes durable only after the IDBFS sync that
started after observing it succeeds; mutations arriving while a sync is in
flight force another pass. The workbench keeps `dirty` true until every
observed generation is durable, and guards page unload while a mutation or
sync is active. A failed sync never advances the durable generation.

With the storage trace category enabled, every actual `syncfs` pass (rather
than every coalesced request) appears as a correlated `storage.sync`
browser-thread scope. Its end event records success or failure, and its duration
is the real callback interval. Trace failures are ignored by the coordinator,
so they cannot alter generation or fence guarantees.

Browser storage is origin-scoped. Clearing site data, using a temporary/private
profile, changing the deployment origin, or deleting IndexedDB removes or
isolates the virtual disk. Export a ZIP before those operations. Quota and sync
failures leave a visible dirty/error state; export data before retrying or
clearing anything.

The Files panel supports upload, drag-and-drop, download, directories, rename,
delete, ZIP export, and previewed ZIP restore. Archive paths are contained under
`/data`; absolute paths and parent traversal are rejected. Conflicts require an
explicit overwrite, skip, or keep-both policy.

## Optional host-folder mirror

Chrome and Edge can select a local folder using the File System Access API.
This is a synchronized mirror, not a direct POSIX mount: PicoTracker continues
to read and write IDBFS synchronously while the coordinator copies at safe sync
points. The selected directory is the hard root boundary.

The mirror stores a manifest of relative path, type, size, modification time,
and content hash. Pull, push, and bidirectional sync detect independent changes,
deletions, and conflicts. A conflict pauses that path until the user chooses
the browser version, host version, or both. Unmount waits for pending work.

Folder handles may persist while permission returns to `prompt` or `denied`.
Reconnect must be initiated by a user gesture. Losing permission detaches the
host side but does not delete the IDBFS copy.

Firefox and Safari use IDBFS plus ZIP import/export when host-folder access is
unavailable. The Files panel must say that mounting is unsupported without
preventing the tracker from starting.

## Factory content

To seed a large library, mount or import the desired factory-content directory
through the Files panel. Keep the original directory outside the repository;
the workbench copies only through its explicit import/sync flow. Verify the
result with a ZIP export before treating the browser copy as authoritative.
