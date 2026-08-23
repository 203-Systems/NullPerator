import { describe, expect, it, vi } from 'vitest'
import { readFile } from 'node:fs/promises'

import { audioMetricsContract, createAudioBridge } from '../src/handles/audio.js'
import { createAudioStore } from '../src/stores/audio.js'

describe('browser audio state', () => {
  it('does not report running before a user unlock gesture', async () => {
    const bridge = {
      getAudioState: () => 1,
      getAudioMetrics: () => ({ version: 5, size: 68 }),
      unlockAudio: vi.fn(),
    }
    const audio = createAudioStore(bridge)

    await audio.initialize()

    expect(audio.snapshot()).toMatchObject({ state: 'locked', error: null })
    expect(bridge.unlockAudio).not.toHaveBeenCalled()
  })

  it('makes unlock idempotent and maps a failed native state', async () => {
    let state = 1
    const bridge = {
      getAudioState: () => state,
      getAudioMetrics: () => ({ version: 5, size: 68 }),
      unlockAudio: vi.fn(() => {
        state = 2
        return 1
      }),
    }
    const audio = createAudioStore(bridge)
    await audio.initialize()

    await Promise.all([audio.unlock(), audio.unlock()])
    expect(bridge.unlockAudio).toHaveBeenCalledTimes(1)
    expect(audio.snapshot().state).toBe('starting')

    state = 5
    await audio.refresh()
    expect(audio.snapshot().state).toBe('failed')
  })

  it('rejects incompatible metrics copies rather than exposing stale data', async () => {
    const audio = createAudioStore({
      getAudioState: () => 1,
      getAudioMetrics: () => ({ version: 99, size: 0 }),
      unlockAudio: () => 1,
    })

    await audio.initialize()

    expect(audio.snapshot()).toMatchObject({
      state: 'locked',
      metrics: null,
      error: expect.stringMatching(/metrics/i),
    })
  })

  it('uses the complete v5 callback timing metrics ABI', () => {
    expect(audioMetricsContract).toEqual({ version: 5, size: 68 })
  })

  it('applies bounded buffer and Q16 volume settings through the native bridge', () => {
    const configure = vi.fn()
    const bridge = createAudioBridge({
      _PicoTracker_Wasm_ConfigureAudio: configure,
    })
    bridge.configureAudio({ audioBufferFrames: 2048, outputVolume: 25 })
    expect(configure).toHaveBeenCalledWith(2048, 16384)

    const storeBridge = { configureAudio: vi.fn() }
    const audio = createAudioStore(storeBridge, { poll: false })
    audio.configure({ audioBufferFrames: 8192, outputVolume: 80 })
    expect(storeBridge.configureAudio).toHaveBeenCalledWith({ audioBufferFrames: 8192, outputVolume: 80 })
  })

  it('retries a shared metrics copy when publication changes mid-read', () => {
    const words = new Uint32Array(new SharedArrayBuffer(19 * Uint32Array.BYTES_PER_ELEMENT))
    const metrics = [
      5, 68, 3, 32, 1024, 601, 2, 4, 44100, 48000, 9, 8, 1,
      733, 1101, 2667, 3,
    ]
    words.set([2, ...metrics], 1)
    let sequenceLoads = 0
    const originalLoad = Atomics.load
    vi.spyOn(Atomics, 'load').mockImplementation((array, index) => {
      const value = originalLoad(array, index)
      if (index === 1 && ++sequenceLoads === 2) {
        Atomics.store(words, 1, 3)
        Atomics.store(words, 4, 4)
        Atomics.store(words, 1, 4)
        return Atomics.load(words, 1)
      }
      return value
    })
    const bridge = createAudioBridge({
      HEAPU32: words,
      __picoTrackerAudioSnapshot: { metrics: 4, error: 0 },
    })

    expect(bridge.getAudioMetrics()).toEqual({
      version: 5, size: 68, state: 4, ringFillFrames: 32,
      ringCapacityFrames: 1024, renderMicros: 601,
      underrunFrames: 2, overrunFrames: 4, sourceRate: 44100,
      destinationRate: 48000, callbackCount: 9, setupPhase: 8,
      unlockOnBrowserMainThread: 1, callbackMicros: 733,
      callbackMaxMicros: 1101, callbackDeadlineMicros: 2667,
      callbackDeadlineMisses: 3,
    })
    expect(sequenceLoads).toBeGreaterThanOrEqual(4)
  })

  it('limits realtime callback instrumentation to a monotonic clock and lock-free atomics', async () => {
    const [source, driver] = await Promise.all([
      readFile(new URL('../../sources/Adapters/wasm/audio/AudioWorklet.cpp', import.meta.url), 'utf8'),
      readFile(new URL('../../sources/Adapters/wasm/audio/WasmAudioDriver.cpp', import.meta.url), 'utf8'),
    ])
    const callback = source.match(/extern "C" bool PicoTracker_Wasm_AudioWorkletProcess[\s\S]*?\n}\n#endif/)?.[0]
    const recorder = driver.match(/void WasmAudioDriver::RecordCallback[\s\S]*?\n}\n\nWasmAudioDriver \*WasmAudioDriver::Instance/)?.[0]

    expect(callback).toBeTruthy()
    expect(callback.match(/emscripten_get_now/g)).toHaveLength(2)
    expect(callback).not.toMatch(/WasmProfiler|WASM_TRACE|chrono|steady_clock/)
    expect(source).not.toMatch(/\b(?:malloc|calloc|realloc|free|open|close|read|write|fopen|fprintf|printf)\s*\(|\bnew\s/)
    expect(source).not.toMatch(/Trace::|mutex|lock_guard|condition_variable|sleep_for|sleep_until/)
    expect(recorder).toContain('compare_exchange_weak')
    expect(recorder).not.toMatch(/chrono|WasmProfiler|WASM_TRACE|Trace::|mutex|\bnew\s|\b(?:malloc|open|close|read|write|fopen|printf)\s*\(/)
  })

  it('publishes all fixed audio counters from the application snapshot boundary', async () => {
    const source = await readFile(
      new URL('../../sources/Adapters/wasm/audio/WasmAudio.cpp', import.meta.url),
      'utf8',
    )

    expect(source).toMatch(
      /void WasmAudio::PublishSnapshot\(\)[\s\S]*AudioSnapshot[\s\S]*AudioCallbackCount[\s\S]*AudioUnderrunFrames[\s\S]*AudioOverrunFrames[\s\S]*AudioRenderDurationUs[\s\S]*AudioCallbackDurationUs[\s\S]*AudioCallbackMaxDurationUs[\s\S]*AudioCallbackDeadlineUs[\s\S]*AudioCallbackProcessingDeadlineMisses[\s\S]*metricsSnapshot_/,
    )
  })

  it('has exactly one application-rAF publisher for a metrics generation', async () => {
    const [audioSource, eventManager] = await Promise.all([
      readFile(new URL('../../sources/Adapters/wasm/audio/WasmAudio.cpp', import.meta.url), 'utf8'),
      readFile(new URL('../../sources/Adapters/wasm/gui/WasmEventManager.cpp', import.meta.url), 'utf8'),
    ])

    expect(audioSource).not.toContain('PublishSnapshot();')
    expect(eventManager).toMatch(/void WasmEventManager::PumpFrame\(\)[\s\S]*WasmAudio::PublishSnapshot\(\)/)
  })

  it('pins every callback timing field to the v5 C++ ABI offsets', async () => {
    const source = await readFile(
      new URL('../../sources/Adapters/wasm/audio/WasmAudioState.h', import.meta.url),
      'utf8',
    )

    expect(source).toMatch(/Version = 5/)
    expect(source).toMatch(/sizeof\(WasmAudioMetrics\) == 68U/)
    expect(source).toMatch(/offsetof\(WasmAudioMetrics, callbackMicros\) == 52U/)
    expect(source).toMatch(/offsetof\(WasmAudioMetrics, callbackMaxMicros\) == 56U/)
    expect(source).toMatch(/offsetof\(WasmAudioMetrics, callbackDeadlineMicros\) == 60U/)
    expect(source).toMatch(/offsetof\(WasmAudioMetrics, callbackDeadlineMisses\) == 64U/)
  })
})
