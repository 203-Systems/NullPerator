# Deploying the static workbench

Deploy the complete contents of `web/dist` at one origin. No application
backend, CDN package, MatrixOS checkout, or external runtime asset is required.
Run `pnpm verify:dist` immediately before publishing.

## GitHub Actions deployment gate

The `WASM workbench` workflow runs host CTest targets before compiling the
Release WASM. It then runs browser unit tests, non-visual Playwright acceptance,
and the static distribution verifier. A failed early host build means the run
has not compiled or deployed the Web app, even if iOS and ESP32 checks pass.

Only a successful **push to main** reaches the Vercel production steps.
Pull-request, branch, and manually dispatched runs can validate the build
without publishing it. The Vercel build packages the WASM produced in that
same job; do not substitute a stale local loader/WASM pair.

If a check fails, open the first failing step rather than inferring a WASM
compiler error from the workflow name. Host tests must include their own
standard-library headers; a transitive include available on macOS may be
absent on Linux. Use clang-format 17 for the formatting gate. Protect embedded
JavaScript inside Emscripten macros with `clang-format off/on` where needed:
the C++ formatter can split JavaScript `=>` tokens and break WASM linking.

## Required response behavior

Every response in the workbench origin must include:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Serve `.wasm` as `application/wasm` and JavaScript as JavaScript. The included
`web/public/_headers` file is copied to `dist` for hosts that support that
format. Other static hosts must express the same rules in their native
configuration.

Recommended caching keeps content-hashed Vite assets immutable while forcing
the Emscripten loader and WASM pair to revalidate together:

```text
/index.html, /oracle.html       Cache-Control: no-cache
/assets/*                       Cache-Control: public, max-age=31536000, immutable
/wasm/*.js, /wasm/*.wasm       Cache-Control: public, max-age=0, must-revalidate
```

Do not cache `picotracker.js` independently from `picotracker.wasm`. A mismatched
pair can fail during instantiation or call the wrong native ABI.

## Post-deployment check

In Chrome and Edge:

1. Confirm `crossOriginIsolated` is true in the page console.
2. Confirm the Network panel reports `application/wasm` for both WASM files.
3. Boot until the top bar shows Runtime ready and Storage ready.
4. Hard reload and confirm the same build identity appears in About.
5. Exercise audio unlock, IDBFS persistence, a real host folder, physical MIDI,
   trace download, and the long-play checklist in `TESTING.md`.

If a service worker or reverse proxy is added later, it must preserve these
headers on every controlled response. Cross-origin subresources must also be
compatible with COEP; the current verified bundle avoids them entirely.
