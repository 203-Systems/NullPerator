# Task 6 report — realtime PCM transport

## Delivered

- `PcmRingBuffer<CapacityFrames>` is a fixed-storage, power-of-two SPSC queue
  with signed-16-bit stereo input, an alias-safe `short` interleaved producer
  path, and normalized float stereo output.
- `OutputResampler` is a deterministic, scalar-state linear streaming
  resampler. `Process` reports independently how much input it consumed and
  output it produced; `Flush` emits the finite-stream tail without allocating.
- The host test executable now includes the audio primitives and coverage.

## Ring-buffer contract

Positions are monotonically increasing `uint64_t` values and are masked only
when indexing storage. This makes empty (`write == read`) distinct from full
(`write - read == capacity`) without reserving a slot. The producer reads the
consumer position with acquire ordering and publishes completed frame writes
with a release store; the consumer mirrors that ordering. `Write` returns
accepted frames and `Overruns` counts rejected frames. `Read` returns real
frames, zero-fills the rest, and `Underruns` counts zero-filled frames.

`WriteInterleaved(std::span<const short>)` is the AudioDriver-facing producer
API. It reads left/right scalar pairs directly, so it neither assumes that a
`short[]` contains `StereoI16` objects nor uses `reinterpret_cast`. It has
compile-time checks that the engine's `short` is a signed 16-bit sample. An odd
scalar count is malformed: the whole write is rejected and queue/counters stay
unchanged.

The 64-bit positions make wrapping unreachable in normal realtime use; modular
subtraction remains correct as long as the outstanding distance is at most the
fixed capacity. `Reset` is explicitly startup/test-only and requires quiescent
endpoints.

## Resampler contract

`Process(input, output)` returns `{ inputFramesConsumed,
outputFramesProduced }`, so callers retain only unconsumed input when an output
span fills. State holds the previous/current interpolation frames and phase;
there is no callback-time heap storage. A final `Flush(output)` extends the
last slope by one source frame (or duplicates a one-frame input), preserving
DC exactly and avoiding a discontinuous tail. Equal source/destination rates
copy frames directly.

## TDD and verification

- RED: a fresh host CMake configure failed because the new audio source files
  did not exist.
- Review follow-up RED: focused host tests failed to compile until
  `WriteInterleaved` and the HOST_TEST-only modular-position seam existed.
- Normal host: `cmake --build build-host && ./build-host/picoTracker_tests`
  passed.
- ASan + UBSan: fresh sanitizer configuration and final rerun passed. macOS
  ASan does not support leak detection, so the final run used
  `ASAN_OPTIONS=detect_leaks=0`.
- TSan: `build-host-tsan` configured, built, and passed with
  `TSAN_OPTIONS=halt_on_error=1`.
- The deterministic in-suite two-thread SPSC stress moved 65,536 ordered
  frames with no underruns or overruns.
- Additional edge coverage exercises alias-safe short conversion (including
  `INT16_MIN`), malformed odd interleaved input, an actual position wrap near
  `UINT64_MAX`, invalid rates, empty and one-frame streams, downsampling,
  non-identity partial-output continuation, multi-call flush, and reset/reuse.
- The adversarial-chunk one-second 44.1 kHz to 48 kHz sine test produced
  exactly 48,000 frames, maintained stereo identity and [-1, 1] bounds, and
  enforces maximum absolute sample error below `0.0006`.
- Debug WASM: after activating the local emsdk,
  `tools/build-wasm.sh Debug` passed and compiled both primitives into
  `platform_audio`.

The existing ETL and core link-closure warnings are pre-existing and no new
warnings or sanitizer diagnostics were reported for this task.
