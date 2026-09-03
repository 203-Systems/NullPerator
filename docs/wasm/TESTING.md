# WASM workbench testing and acceptance

Automated tests are necessary evidence but do not replace real browser,
sound-device, folder, or MIDI checks. Run local commands serially:

```sh
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel 1
./build-host/picoTracker_tests
CMAKE_BUILD_PARALLEL_LEVEL=1 tools/build-wasm.sh Release
cd web
pnpm exec vitest run --maxWorkers=1
pnpm exec playwright test --workers=1
pnpm build
pnpm verify:dist
```

The normal Playwright audio test verifies the recoverable audio-disabled path.
On a Chrome/Edge runner with a real audio output, run the strict callback gate:

```sh
cd web
PICOTRACKER_AUDIO_E2E=1 pnpm exec playwright test e2e/audio.spec.js --workers=1
```

It must reach running state, observe real AudioWorklet callbacks, advance the
callback counter, expose the callback processing deadline and durations, add no
underruns, and add no callback processing-deadline misses during the measured
interval.

The `views.spec.js` acceptance case uses a test-query-only bridge to request
each of the 19 registered `ViewType` values and all four concrete modal UI
classes. Requests cross an atomic mailbox and are executed only by the normal
application pthread; a generation is published after the regular `ClockTick`
draw path returns. The test then sends pointer input through the unified action
bridge and closes every modal through its normal C++ lifecycle. This proves the
structural enter/draw/input paths, while the workflow-specific manual checks
below still prove real content and destructive confirmation behavior.

## Real factory-content gate

`factory-content.spec.js` is an opt-in acceptance test because the licensed
factory tree is not checked into this repository. Point it at a local checkout:

```sh
cd web
PICOTRACKER_FACTORY_CONTENT=/absolute/path/to/factory-content-main \
  pnpm exec playwright test e2e/factory-content.spec.js --workers=1
```

The test validates hashes before use and restores only the minimal valid
`oneCycAc` closure: `default-current.txt` as `/data/.current`, the project's
`lgptsav.dat`, and `AKWF_0906.wav`. It intentionally does not import the full
factory tree, which contains nested/duplicate project and sample directories.
The gate proves the C++ model loaded the named project, tempo, and sample; starts
and stops the real player through C; edits and saves tempo through the fixed
NullPerator `WASD / JK / XC` controls; then verifies the result after both
runtime restart and full page reload. The Save check first observes the real
C++ file mutation generation, waits for that exact IDBFS durability fence, and
does not invoke the test fixture's force-flush helper. If
`PICOTRACKER_FACTORY_CONTENT` is unset, this case is reported as skipped, never
as acceptance evidence.

On a machine where the strict AudioWorklet gate works with a real output, add
the audio flag to also require callbacks and non-silent master output:

```sh
cd web
PICOTRACKER_FACTORY_CONTENT=/absolute/path/to/factory-content-main \
PICOTRACKER_AUDIO_E2E=1 \
  pnpm exec playwright test e2e/factory-content.spec.js --workers=1
```

## Deterministic tracing gate

Run the trace gate against a freshly built Release module:

```sh
cd web
pnpm exec playwright test e2e/trace.spec.js --workers=1
```

It runs the 32-block PCM benchmark once with capture disabled and once with all
categories enabled; both must equal fixture-v1 golden `0xc45e4b1c`. It also
requires real UI/input/audio-health/MIDI-poll/benchmark records, a correlated
`input.accepted` → committed `input.presented` transition with a matching
`input.to_frame_latency_us` counter, paired 32-block Begin/End events, a
positive capture duration, build/time-range metadata, and complete numeric
`args` on every exported Begin, End, Instant, and Counter event. This short
deterministic gate does not replace the manual representative workload and
30-minute audio comparison below.

## Manual Chrome and Edge acceptance record

Record browser name/version, OS, commit, Emscripten version, audio device/rate,
folder path, MIDI device, start/end time, and tester. Do not mark an item from a
mocked/headless result.

- [ ] Enter, draw, and operate every current PicoTracker view and modal.
- [ ] Exercise every action from its default keyboard mapping and virtual button;
      verify fixed NullPerator WASD/JK/XC keys, chords, two simultaneous
      touches, and blur/cancel release. Verify C taps as PLAY before 500 ms,
      holds as NAV at or beyond 500 ms, and becomes NAV immediately when
      chorded. Verify input order: X then C holds ALT+PLAY for as long as C is
      down (a third key,
      including EDIT, cancels PLAY), while C then X
      remains NAV+ALT.
- [ ] At 44.1 kHz output, play known material continuously with correct pitch and
      duration; repeat at 48 kHz and inspect underrun and callback
      processing-deadline-miss counters.
- [ ] Save a real project and WAV sample, restart the runtime, reload the page,
      and reopen both with identical content.
- [ ] Import a WAV, preview it, edit it, save it, reload, and play the edited copy.
- [ ] Mount a real folder; test pull, push, bidirectional changes, deletion on
      each side, permission loss/reconnect, and all three conflict resolutions.
- [ ] With a physical MIDI device, test permission denial/retry, note/input bytes,
      output, clock/timestamp behavior, disconnect, and stable-id reconnect.
- [ ] Generate high-rate logs; confirm bounded/dropped behavior, filters, pause,
      clear, copy, and downloaded JSONL.
- [ ] Capture all required trace categories, validate downloaded Chrome JSON,
      run the synthetic DSP microbenchmark, and compare trace-disabled audio metrics.
- [ ] Trigger every documented recovery state: isolation, WASM/pthread load,
      audio setup, underrun, IDBFS/quota, host permission/conflict, MIDI denial/
      disconnect, and C++ fatal restart without clearing data.
- [ ] Repeat the complete suite in current Chrome and current Edge.
- [ ] Run uninterrupted playback for at least 30 minutes after warm-up; record
      callback duration/current/max/deadline, underrun, overrun, callback
      processing-deadline-miss, and memory values before/after.

## Reduced-mode compatibility

In current Firefox and Safari, verify the runtime and C++ UI start when their
pthread/SharedArrayBuffer capabilities and the required isolation headers are
available. Verify IDBFS save/reload and ZIP import/export. Host-folder mounting
and Web MIDI may be shown as unsupported; that must remain a local subsystem
state and must not prevent UI, files, or supported audio from running. Record
the exact unsupported capability and browser version.

## NullPerator hardware regression

Host adapter tests and the full browser acceptance suite run in the WASM
workflow. Before release, build and smoke-test the hardware using its documented
ESP-IDF environment, including UI, storage, buttons/headphone detection, and
audio.

## Completion evidence

The design's 14 completion criteria are satisfied only when automated results,
the static verifier, both Chrome/Edge records, Firefox/Safari reduced-mode
records, the physical MIDI/folder/audio checks, the 30-minute run, and existing
target regressions are all attached to the same commit. An unchecked or
environment-skipped row remains pending rather than passing by assumption.
