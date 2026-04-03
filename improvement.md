## Audio Cleanup Summary

This note summarizes the retained audio-path changes after removing temporary
profiling and tracing code.

### Retained Changes

- `sources/Application/Instruments/SampleInstrument.cpp`
  - Added mono/nearest fast paths for:
    - no filter
    - plain lowpass
    - plain mixed filter
    - scream lowpass
    - scream mixed filter
  - Extended mono fast paths to support clean downsampling.
  - Added cached clean-downsampling fetch logic to avoid recomputing the same
    source address repeatedly.
  - Skipped no-op crush/drive work when settings are neutral.
  - Added integer-step shortcut for mono nearest playback.
  - Precomputed mono fast-path selection once and refreshed it only when needed.
  - Reduced mixed-filter multiply count by computing wet once and deriving dry.
  - Reduced scream-filter cost by precomputing combined filter parameters.
  - Reduced repeated filter state loads/stores by using local temporaries in the
    fast filter paths.
  - Reduced updater overhead:
    - single-updater fast path
    - refresh cached gain/filter/speed values only when they actually change
    - do not invalidate filter caches for `fbMix/fbTun`, which are not consumed
      by the current sample filter path

- `sources/Adapters/node/gui/EventManager.cpp`
  - Pinned UI/input/USB tasks to core 0 to reduce contention with audio work on
    core 1.

- `sources/Adapters/node/audio/AudioDriver.cpp`
  - Kept the static counting semaphore for audio fill synchronization.
  - Kept the larger `AudioThread` stack to avoid overflow.

### Removed During Cleanup

- Temporary timing/profiling logs from:
  - `SampleInstrument`
  - `AudioMixer`
  - `AudioOutDriver`
  - `MixerService`
  - `NodeAudioDriver`
- Temporary slow-frame aggregation/reporting helpers.
- Temporary log-rate limiting added only to support profiling sessions.

### Main Review Targets

- `sources/Application/Instruments/SampleInstrument.cpp`
- `sources/Adapters/node/audio/AudioDriver.cpp`
- `sources/Adapters/node/gui/EventManager.cpp`
- `sources/Adapters/node/system/SamplePool.cpp`

### Main Remaining Constraint

The sample render path is materially faster than before, but heavy passages can
still exceed the available real-time budget when many filtered sample voices are
active at the same time.

### Current Node Audio Pipeline Notes

- Core placement:
  - `sources/Adapters/node/audio/AudioDriver.cpp`
    - `AudioThread` runs on core 1
    - `I2SThread` runs on core 1
  - `sources/Adapters/node/gui/EventManager.cpp`
    - UI/input/USB tasks run on core 0
  - `sources/Application/AppWindow.cpp`
    - `PlayerEvent` view updates are now deferred and applied from
      `AnimationUpdate()` on the UI thread instead of directly from the audio
      render thread

- Current render / playback chain:
  - `NodeAudioDriver::AudioThread`
  - `NodeAudioDriver::BufferNeeded`
  - `AudioDriver::OnNewBufferNeeded`
  - `AudioOutDriver::Update`
  - `MixerService::Update`
    - first `NotifyObservers()` to advance player state
    - then `out_->Trigger()` to render/mix/clip
  - `AudioOutDriver::Trigger`
  - `AudioMixer::Render`
  - `AudioDriver::AddBuffer`
  - `NodeAudioDriver::I2SThread`
  - `audio_codec_write` / `i2s_channel_write`

- Important implication:
  - `render_max_us` does not only measure mixer cost.
  - It currently includes:
    - player state advancement
    - phrase / table command processing
    - mixer render
    - clip-to-int16
    - software buffer enqueue

### Current Performance Tracking

- `sources/Adapters/node/audio/AudioDriver.cpp`
  - `NodeAudioPerf` reports every 5s:
    - `req`: number of `BufferNeeded()` calls actually serviced
    - `played`: real audio buffers completed
    - `blank`: fallback blank buffers sent
    - `queued_now`, `queued_peak`
    - `render_max_us`
    - `write_max_us`
    - `req_drop`: failed `core1_audio` semaphore gives
    - `tx_ovf`: I2S TX message-queue overflow callbacks
    - `write_err`, `short`

- `sources/Services/Audio/AudioDriver.cpp`
  - overrun logs now include:
    - overrun count
    - queue/play indices
    - size
    - `hasData`

### Findings From Current Logs

- The first-start hardfault was not caused by audio overrun itself.
  - A separate `TablePlayback` race became visible once more work moved to core
    1.
  - `TablePlayback::ProcessStep()` was hardened by taking local snapshots of
    `table_` and `instrument_`.

- `blank` growth indicates true underrun periods, especially during playback
  startup or heavy passages.

- `tx_ovf` is significant and should not be ignored.
  - In ESP-IDF this means the I2S TX internal `msg_queue` overflowed.
  - It does **not** mean codec write failure (`write_err` can still be zero).
  - This suggests the current node pacing is still mismatched to DMA-chunk
    completion.

- If `played + blank > req`, request signaling is being lost.
  - This points to the current `core1_audio` synchronization as another real
    bottleneck.

### Next Optimization Direction

- Keep render-heavy work on core 1:
  - player step advancement
  - mixer render
  - sample generation
  - audio buffer production

- Keep non-render UI work on core 0:
  - input
  - redraw / animation
  - view `OnPlayerUpdate()` reactions

- Most likely next structural fixes:
  - prevent dropped buffer-request signals
  - feed I2S in DMA-chunk-sized pieces instead of whole logical audio buffers
  - then split `render_max_us` into:
    - player/update cost
    - mixer render cost
    - clip/enqueue cost

### SampleInstrument Render Review

Current profiling points to `SampleInstrument::Render()` as the main remaining
hot path under heavy passages, especially:

- mono sample
- nearest-neighbor path
- filtered path
- often with updaters active

Key observations from the current code in
`sources/Application/Instruments/SampleInstrument.cpp`:

- The current hotspot is **not** the generic interpolation path.
  - Current logs show `generic=0` while `mono_fast` dominates.
- `Player::Update()` and phrase/table command handling are not the main issue.
  - They are materially smaller than the render path.
- `clipToMix()` and software enqueue are also not the main issue.

#### Best Immediate Candidates

1. Avoid unconditional filter reconfiguration at render start.
   - `set_filter(channel, ...)` is called at the top of every render call.
   - Later in the same function, filter parameters are already refreshed only on
     change during k-rate updates.
   - Likely improvement:
     - cache `filterMix` / filter-mode mapping / cutoff / reso state in
       `renderParams`
     - only call `set_filter()` when those inputs actually change
   - This is especially attractive because `set_filter()` in
     `sources/Application/Instruments/Filters.cpp` still does non-trivial work
     every call.

2. Stop zeroing the whole output buffer on every sample render when the fast
   path fully overwrites it anyway.
   - Current code does:
     - `memset(buffer, 0, size * 2 * sizeof(fixed));`
   - For many active sample voices this becomes repeated full-buffer clearing.
   - Better approach:
     - only clear the tail when playback finishes early
     - or split the fast path so fully-covered buffers skip the initial clear

3. Batch work between state boundaries instead of checking every sample.
   - The current loop re-checks per sample:
     - loop end / ping-pong transitions
     - k-rate countdown
     - integer-step movement
   - For the dominant mono-nearest fast path, it should be possible to process
     a run of samples up to the next boundary:
     - next loop boundary
     - next k-rate update
     - next retrigger-sensitive point
   - This is more invasive than the first two items, but likely the biggest
     upside.

#### Good Secondary Candidates

4. Specialize the dominant mono-fast filtered path further.
   - The code already has dedicated fast paths:
     - `MFP_PLAIN_LOWPASS`
     - `MFP_PLAIN_MIXED`
     - `MFP_BOOST_LOWPASS`
     - `MFP_BOOST_MIXED`
   - Even there, the loop is still one-sample-at-a-time.
   - If the common case is:
     - mono
     - nearest
     - no downsampling change inside the run
     - no loop edge inside the run
     then a short unrolled block path may help.

5. Add tiny special-cases for panning/volume combinations.
   - Current fast paths already special-case equal pan.
   - Extra low-risk wins may exist for:
     - hard-left / hard-right
     - unity-volume-like cases
   - This is probably smaller than the items above.

6. Replace small per-render constants with cached/static fixed values.
   - Examples:
     - `volscale`
     - `zerofive`
   - This is safe but low-impact.

#### Lower Priority / Probably Not Worth It First

- Generic interpolation tuning
  - current logs say this path is not the one in use
- `Player::Update()` micro-optimizations
  - measurable, but not the dominant budget consumer
- `clipToMix()` changes
  - not currently the hotspot

#### Suggested Order

1. Make filter setup change-driven instead of unconditional.
2. Remove the always-on full-buffer clear from the dominant fast path.
3. Then investigate boundary batching / chunked mono-fast processing.

### Detailed Findings From The Latest Profiling Pass

The most useful conclusion from the latest profiling pass is that the real
problem is still inside `SampleInstrument::Render()`, not in the surrounding
audio pipeline.

#### What The Logs Actually Showed

- `NodeAudioPerf`
  - underruns (`blank`) and dropped requests (`req_drop`) grow only after
    render spikes become too large
  - they are symptoms of the render budget being exceeded, not the first cause

- `NodeMixerPerf`
  - `single_module_max_us` regularly reached roughly `40ms-70ms`
  - this does **not** mean one sample voice costs `40ms-70ms`
  - it means one top-level mixer module/bus accumulated enough work to cost
    that much

- `NodeSamplePerf`
  - `loop_max_us ~= render_max_us`
  - `mono_path_max_us` is the dominant sub-cost
  - `generic_path_max_us = 0` in the heavy test case
  - `filter_cfg_max_us`, `tick_max_us`, `krate_max_us`, and
    `boundary_max_us` were all much smaller than the main loop

#### Strongest Current Interpretation

- The main hot path is:
  - mono sample
  - nearest-neighbor playback
  - filtered fast path
  - often with updater activity

- The dominant cost is the one-sample-at-a-time inner loop itself.

- The previously suspected smaller items are real but not primary:
  - unconditional `set_filter(...)`
  - whole-buffer `memset(...)`
  - boundary checks
  - k-rate bookkeeping

- `NodeMixerPerf.single_module_max_us` being much larger than
  `NodeSamplePerf.render_max_us` strongly suggests:
  - several expensive sample voices are stacking on the same bus/module
  - not that one single voice alone is always taking the full module time

### PSRAM Findings

The latest PSRAM-vs-internal instrumentation showed:

- all hot sample renders observed in the heavy test were coming from PSRAM
- `internal_calls` stayed at `0` in the tested scenario

This means:

- the current hot sample path is definitely reading from PSRAM
- PSRAM is a credible amplifier of the problem
- but this alone does **not** prove PSRAM is the only root cause, because there
  was no meaningful internal-RAM comparison in the same workload yet

Board context:

- PSRAM is OSPI at `80MHz`

Most useful next experiment if restarting from scratch:

1. force a small, controlled subset of samples into internal RAM
2. keep the exact same musical test case
3. compare:
   - `NodeSamplePerf.psram_*`
   - `NodeSamplePerf.internal_*`
   - `NodeMixerPerf.single_module_max_us`
   - audible underrun behavior

If internal-RAM samples are much faster, PSRAM bandwidth/latency is a major
factor. If not, the dominant cost is mostly arithmetic/branching in the inner
kernel.

### Recommended Optimization Order If Re-Doing The Work

If starting clean, the highest-value order is:

1. Re-add only minimal coarse logs:
   - `NodeAudioPerf`
   - `NodeMixerPerf`
   - `NodeSamplePerf`

2. Re-confirm the same hotspot:
   - `SampleInstrument::Render()`
   - mono nearest filtered path

3. Optimize the render loop itself before touching the rest of the pipeline:
   - batch work between boundaries instead of checking every sample
   - focus on the mono-fast filtered branches
   - reduce repeated fixed-point work inside those branches

4. Only after that, revisit secondary contributors:
   - filter setup
   - buffer clearing
   - DMA pacing / request signaling

### Using ESP-IDF PerfMon

Espressif's Xtensa perfmon tools appear usable for this investigation, but they
fit best as a **targeted microbenchmark tool**, not as a whole-system live
profiler.

Relevant references:

- Official docs:
  - https://docs.espressif.com/projects/esp-idf/en/v5.3.5/esp32s3/api-reference/system/perfmon.html
- Local IDF headers:
  - `C:/espressif/v5.3.4/components/perfmon/include/xtensa_perfmon_apis.h`
  - `C:/espressif/v5.3.4/components/perfmon/include/xtensa_perfmon_access.h`
- Local example:
  - `C:/espressif/v5.3.4/examples/system/perfmon/main/perfmon_example_main.c`

Most relevant API:

- `xtensa_perfmon_exec(...)`

Most relevant usage pattern:

- wrap one narrow function/kernel
- run it many times with fixed inputs
- collect hardware counters

Why it is useful here:

- it can help answer whether the hot path is mostly:
  - cycle-bound
  - uncached/bypass-load bound
  - stall/bubble bound

Most useful counters to try first:

- cycles
- instructions
- uncached-load or bypass-load related counters
- local-memory load counters
- bubble/stall counters

Best way to use it in this project:

- build a temporary node-only microbenchmark around the hottest
  `SampleInstrument` kernel
- compare the same kernel with:
  - PSRAM-backed sample data
  - internal-RAM-backed sample data

PerfMon should be treated as a complement to the current timing logs, not a
replacement for them.
