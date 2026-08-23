# Building the PicoTracker WASM workbench

The workbench is a static Svelte application containing the complete
PicoTracker C++ application compiled to WebAssembly. It does not use a server
backend or a MatrixOS runtime dependency.

## Reproducible toolchain

CI currently pins these versions:

- Emscripten SDK 6.0.5;
- Node.js 24.19.0;
- pnpm 11.19.0;
- CMake 3.13 or newer.

Initialize all git submodules before building. Activate Emscripten so that
`emcmake`, `em++`, and `emcc` are on `PATH`, then run:

```sh
git submodule update --init --recursive
corepack enable
corepack prepare pnpm@11.19.0 --activate
cd web && pnpm install --frozen-lockfile && cd ..
CMAKE_BUILD_PARALLEL_LEVEL=1 tools/build-wasm.sh Release
cd web && pnpm build && pnpm verify:dist
```

The C++ build writes `picotracker.js`, `picotracker.wasm`, and their deterministic
audio oracle counterparts to `web/public/wasm`. Vite then emits the deployable
bundle in `web/dist`. `verify:dist` rejects missing WASM, broken local asset
references, remote runtime dependencies, invalid WASM magic bytes, missing
isolation/MIME headers, or missing third-party notices.

Use `tools/build-wasm.sh Debug` for assertion-enabled development. Builds and
tests default to one worker because concurrent PicoTracker test executables and
Emscripten linkers consume substantial memory.

## Local development

After a Debug WASM build:

```sh
cd web
pnpm dev
```

For an exact production-layout check use `pnpm build && pnpm preview`. Both Vite
servers provide the required cross-origin isolation headers. Opening
`dist/index.html` directly with a `file:` URL cannot provide those headers and
will not run pthread-enabled WASM.

Build identity, Emscripten version, and dirty state are visible in the About
panel. Generated `web/public/wasm` and `web/dist` files must be rebuilt whenever
native exports or Emscripten link settings change.
