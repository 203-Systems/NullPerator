# Performance tracing

The Trace panel records bounded, fixed-size native events for UI frames, input,
player/mixer/instruments, audio production and buffer health, filesystem work,
storage synchronization, and MIDI. Each application-thread audio snapshot emits
fixed counters for ring fill (`audio.snapshot`), callback count, cumulative
underrun/overrun frames, producer render duration, current/maximum callback
duration, callback deadline, and callback processing-deadline misses.

`audio.render_duration_us` is the latest application-thread producer request:
it starts immediately before the synchronous `ADET_BUFFERNEEDED` observers and
ends after mixer rendering, clipping, and the PCM ring write return. It is not
AudioWorklet time. `audio.callback_duration_us` measures one successful
AudioWorklet callback's output-render workload, while
`audio.callback_max_duration_us` is its lifetime peak. The processing deadline
is `ceil(quantumFrames * 1,000,000 / destinationSampleRate)` microseconds, using
the actual callback frame count (normally 128). A processing-deadline miss is
counted only when callback duration exceeds that budget. A zero/unknown sample
rate publishes a zero deadline and never increments the miss counter. Misses
use the original sub-microsecond monotonic duration; current/max/deadline values
are rounded up to integral microseconds only for display, so quantization cannot
hide a processing overrun.

The realtime callback reads the browser's monotonic `emscripten_get_now` clock
before and after successful processing and updates only fixed lock-free 32-bit
atomics. It never writes trace records, allocates, logs, locks, accesses files,
performs I/O, or blocks. The application rAF copies those atomics into metrics
ABI v5 and emits trace counters, keeping collection and export off the worklet.

Each actual browser `FS.syncfs` pass is recorded as a `storage.sync` begin/end
scope on the browser thread. A numeric `syncId` pairs the records even when a
new persistence request arrives during an active pass. End events and the scope
summary distinguish successful and failed IndexedDB operations, while the
`populate` argument identifies startup disk population. Trace emission is
observational: it cannot change storage generations, durability fences, retry
behavior, or serialization.

Input latency is not inferred from the `input.dispatch` scope. Every new press
accepted by `InputMap` emits an `input.accepted` instant on the browser thread
with a correlation ID and action. Once SDL dequeues that transition on the
application pthread, the first successful explicit WebGL frame commit emits a
matching `input.presented` instant and an `input.to_frame_latency_us` counter.
Each dispatched press represented by a shared frame receives its own counter.
The bridge uses 16 fixed atomic tickets and never allocates: a seventeenth
pending press emits `input.latency_dropped` with reason `overflow`, while a
ticket without a committed frame for two seconds is retired with reason
`no-presentation`. A press released or coalesced before SDL accepts its DOWN
transition is retired immediately with reason `coalesced`, so it cannot lend
an old timestamp to a later press of the same action. These records keep
instrumentation loss distinct from UI latency.

Web MIDI latency uses two queue boundaries and never changes the original MIDI
timestamps. Every accepted browser input batch emits `midi.input_accepted`
with a non-zero correlation ID. That ID and its monotonic acceptance time ride
with every queued byte, but the application emits exactly one
`midi.input_latency_us` counter after the batch's first byte has actually
entered `processMidiData`, including when a batch spans the 512-byte poll
budget. The original DOM event timestamp remains untouched for MIDI
diagnostics.

Each outgoing message similarly emits `midi.output_queued` when its bounded
native queue accepts it. Browser-main emits the matching
`midi.output_latency_us` immediately when `PicoTracker_Wasm_MidiDrainOutput`
hands it to JavaScript. The separate timestamp scheduled for
`MIDIOutput.send()` is not part of this latency, so a message deliberately
scheduled in the future does not look like a slow native/browser hand-off.
Both paths use fixed SPSC metadata with independent wrapping 16-bit correlation
spaces. Invalid, reversed, or overflowing clock samples produce a safely
saturated counter and do not alter MIDI queue behavior; disabled tracing does
not read the tracing clock.

## Capturing a trace

1. Choose default categories in Settings or use the Trace panel controls.
2. Start capture, reproduce one focused operation, then stop capture.
3. Inspect capture duration, event count, dropped/overwritten count, scope
   statistics, and hottest scopes.
4. Download Chrome JSON and open it in a compatible trace viewer.

The ring holds 4,096 records. A long capture intentionally overwrites old data
and reports the gap instead of growing memory. Records use numeric identifiers;
the JavaScript registry supplies names only after collection. Paths, project
contents, MIDI payload bytes, samples, and audio data are not captured.
Chrome JSON preserves every fixed record's numeric `value`, `sequence`,
`generation`, and `flags` fields in `args` for Begin, End, Instant, and Counter
events. Its metadata includes the WASM build identity and toolchain, selected
mask, capture duration, explicit time units, monotonic record time range, drop
count, generation, benchmark result, and the fixture golden.

## Synthetic DSP microbenchmark

This benchmark is a standalone deterministic triangle-wave workload; it does
not execute PicoTracker's Project, Player, Mixer, or instrument engine. It
renders a fixed 8-channel, 64-row fixture in 128-frame stereo blocks and reports
median, p95, p99, maximum, total work, deadline misses, and a fixture hash over
every generated PCM byte. The 32-block golden hash is
`0xc45e4b1c`; the native test, browser bridge, and browser acceptance gate all
reject a different value as a behavioral change, not merely a timing change.

Compare builds on the same browser, machine, power state, sample count, trace
mask, and audio mode. Browser results reveal algorithmic hotspots and relative
regressions, but do not reproduce ESP32/RP2040 cache, PSRAM, I2S, DMA, or RTOS
timing.

With capture disabled, native scope entry performs a category-mask condition
only: it does not read a clock, allocate, format strings, or enqueue a record.
For audio-impact validation, compare a warmed 30-minute playback run with trace
fully disabled against the same workload with only the required categories.
