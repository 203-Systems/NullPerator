# UI2 Approved Golden Frames

These PNGs are the approved 240x240 UI2 examples from the design review.

They are acceptance inputs, not output snapshots that implementation work may
update casually. `manifest.json` records the SHA-256 of the approved source and
every generated PNG.

To replace them, use `web/tools/capture-ui2-goldens.mjs` with the explicit
`--approve yes` flag and an approved reference. A golden update must be kept in
a separate commit and reviewed visually before renderer changes use it.

Pixel-perfect tests use zero differing pixels. A tolerance may be used only for
an explicitly approved platform conversion test, never for the shared indexed
framebuffer.
