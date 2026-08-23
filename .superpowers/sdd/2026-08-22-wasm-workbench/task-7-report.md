# Task 7 report — low-latency browser audio

## Delivered

- Replaced the silent WASM backend with `WasmAudio` and `WasmAudioDriver`.
  The engine remains 44.1 kHz signed-16-bit interleaved stereo. A fixed
  storage SPSC PCM ring and `OutputResampler` adapt only at the browser output
  rate boundary.
- Added a real Emscripten Wasm AudioWorklet graph. Browser-main bootstrap
  creates a suspended `AudioContext` and worklet scope; a trusted user click
  resumes it, creates processor/node, and the process callback advances state
  to `Running` and increments its atomic callback counter.
- The AudioWorklet process callback uses fixed stack/storage and atomics only:
  no allocation, logging, locks, files, waits, `chrono`, or other time API.
  Callback-duration and deadline metrics are not exposed: timing or browser
  imports in this realtime boundary are deferred to a future tracing design.
- Audio diagnostics use the v4 shared seqlock snapshot: the application rAF
  is its only writer, publishing odd generation, all atomic words, then
  even-release generation. Browser readers retry unless before/copy/after
  observe the same even generation. Runtime polling never calls a proxied C++
  metrics/state export. The ABI retains callback count, render time,
  underrun/overrun counters, and source/destination rates.
- Application-pthread shutdown queues graph teardown on Emscripten's official
  `emscripten_async_run_in_main_runtime_thread` API. The rAF lifecycle keeps
  running until browser-main has destroyed node/context and acknowledged
  completion, then publishes `Stopped`; JS only terminates the old pthread
  after that acknowledgement. This covers app-internal quit and restart/late
  callback ordering.
- Added the visible unlock/recovery controls. Default mode deliberately marks
  audio unavailable and keeps the UI usable; `?audio=worklet` is an explicit
  low-latency opt-in. Timed-out/failed worklet mode offers reload-without-audio.

## Deterministic oracle and regression fixed

`AudioRenderOracle.cpp` contains the shared C++ render helper. A tiny
single-thread standalone WASM target links that helper with no imports and is
instantiated directly by Playwright with `WebAssembly.instantiateStreaming`.
Exact results are:

| Destination rate | result `[version, size, rate, frames, hash, peakQ15]` |
| --- | --- |
| 44,100 Hz | `[1, 24, 44100, 128, 799941061, 16384]` |
| 48,000 Hz | `[1, 24, 48000, 140, 2233655419, 16384]` |

The fixture exposed a RED failure in `OutputResampler::Flush`: it could remake
a completed tail after its phase had passed the end, preventing 48 kHz output
progress. Flush now stops once that tail is finished. The SPSC stress test was
also corrected to count every intentionally rejected full-ring retry as an
overrun, while still checking ordering/no-underflow.

## Verification

- Host Debug: **38/38 passed**, **192,855 assertions**.
- ASan host full suite: **38/38 passed**, no sanitizer diagnostics. macOS
  ASan does not support `detect_leaks=1`, so the successful command uses
  `ASAN_OPTIONS=halt_on_error=1`.
- TSan SPSC contention: **1/1 passed**, no diagnostics.
- Debug WASM with local Emscripten 6.0.5: `picotracker_wasm` and
  `picotracker_wasm_oracle` built, including the core-link-closure gate.
- Vitest: **5 files, 33 tests passed**, including source-level realtime and
  single-rAF-publisher guards, delayed teardown acknowledgement, and forced
  in-copy metrics publication.
- Fresh `CI=1` Playwright: **7/7 passed** (47.8s), covering UI boot,
  input/restart, default recovery, and raw-WASM 44.1/48 kHz oracle.

## Strict audio acceptance — pending outside this environment

The independent mandatory command is:

```sh
cd web
pnpm test:e2e:audio-worklet
```

It sets `PICOTRACKER_AUDIO_E2E=1`; it does not runtime-skip. It requires the
`?audio=worklet` URL, trusted unlock, `Running`, continuously growing callback
count, and no new underruns after warmup. Existing GitHub workflows only build
Pico firmware/formatting and provide no known audio-capable Chrome runner, so
this is a documented release-acceptance command rather than a falsely-green
web CI job.

This Mac’s bounded strict run is **RED**, not accepted: after trusted unlock,
the app remains out of `Running` for 12 seconds with `audioSetupPhase=4`,
`audioUnlockMainThread=1`, `audioWorkletCallbacks=0`, and
`audioUnderruns=0`. Phase 4 is after context resume and before processor
creation; it matches the local Chrome/Emscripten 6.0.5 AudioWorklet bootstrap
hang seen in native controls. No audible playback is claimed. Task 15 must
repeat this strict command on an audio-capable Chrome/Edge runner before final
audio acceptance.

## Manual content

The supplied factory content was not copied into the repository. If manually
checking playback later, use `projects/pico/oneCycAc/lgptsav.dat` with only its
referenced minimal samples. Pre-start IDBFS import remains Task 8.
