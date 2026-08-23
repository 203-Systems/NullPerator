import { describe, expect, it, vi } from 'vitest'
import { readFile } from 'node:fs/promises'

import { audioMetricsContract, createAudioBridge } from '../src/handles/audio.js'
import { createAudioStore } from '../src/stores/audio.js'

describe('browser audio state', () => {
  it('does not report running before a user unlock gesture', async () => {
    const bridge = {
      getAudioState: () => 1,
      getAudioMetrics: () => ({ version: 4, size: 52 }),
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
      getAudioMetrics: () => ({ version: 4, size: 52 }),
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

  it('uses the v4 non-realtime metrics ABI without callback timing fields', () => {
    expect(audioMetricsContract).toEqual({ version: 4, size: 52 })
  })

  it('retries a shared metrics copy when publication changes mid-read', () => {
    const words = new Uint32Array(new SharedArrayBuffer(17 * Uint32Array.BYTES_PER_ELEMENT))
    const metrics = [4, 52, 3, 32, 1024, 0, 0, 0, 44100, 48000, 9, 8, 1]
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

    expect(bridge.getAudioMetrics()).toMatchObject({ version: 4, state: 4, callbackCount: 9 })
    expect(sequenceLoads).toBeGreaterThanOrEqual(4)
  })

  it('keeps timing APIs out of the realtime AudioWorklet callback source', async () => {
    const source = await readFile(
      new URL('../../sources/Adapters/wasm/audio/AudioWorklet.cpp', import.meta.url),
      'utf8',
    )

    expect(source).not.toMatch(/chrono|steady_clock|RecordCallbackDuration/)
  })

  it('has exactly one application-rAF publisher for a metrics generation', async () => {
    const [audioSource, eventManager] = await Promise.all([
      readFile(new URL('../../sources/Adapters/wasm/audio/WasmAudio.cpp', import.meta.url), 'utf8'),
      readFile(new URL('../../sources/Adapters/wasm/gui/WasmEventManager.cpp', import.meta.url), 'utf8'),
    ])

    expect(audioSource).not.toContain('PublishSnapshot();')
    expect(eventManager).toMatch(/void WasmEventManager::PumpFrame\(\)[\s\S]*WasmAudio::PublishSnapshot\(\)/)
  })

  it('does not retain unsupported callback timing fields in the C++ metrics ABI', async () => {
    const source = await readFile(
      new URL('../../sources/Adapters/wasm/audio/WasmAudioState.h', import.meta.url),
      'utf8',
    )

    expect(source).not.toMatch(/callbackMicros|deadlineMisses/)
  })
})
