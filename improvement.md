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
