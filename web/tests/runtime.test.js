import { describe, expect, it, vi } from 'vitest'
import { readFile } from 'node:fs/promises'

import { createRuntime } from '../src/handles/runtime.js'
import { createRuntimeManager } from '../src/stores/runtime.js'

describe('WASM runtime lifecycle', () => {
  it('flushes persistent storage after C++ stop and before terminating workers', async () => {
    const order = []
    let state = 1
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => ({
        getState: () => state,
        getBuildMetadataJson: () => '{}',
        input: { releaseAllActions() {} },
        storage: {
          flushNow: async (reason) => { order.push(`flush-${reason}`) },
        },
        requestShutdown: async () => { state = 4; order.push('cpp-stopped') },
        terminate: async () => { order.push('terminated') },
      }),
    })

    await runtime.start()
    await runtime.stop()

    expect(order).toEqual(['cpp-stopped', 'flush-shutdown', 'terminated'])
    expect(runtime.getSnapshot().state).toBe('idle')
  })

  it('surfaces a failed shutdown flush instead of reporting an idle runtime', async () => {
    let flushes = 0
    let state = 1
    const terminate = vi.fn()
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => ({
        getState: () => state,
        getBuildMetadataJson: () => '{}',
        storage: { flushNow: async () => {
          flushes += 1
          if (flushes === 1) throw new Error('quota exceeded')
        } },
        requestShutdown: async () => { state = 4 },
        terminate,
      }),
    })

    await runtime.start()
    await expect(runtime.stop()).rejects.toThrow('quota exceeded')
    expect(runtime.getSnapshot()).toMatchObject({ state: 'failed', error: 'quota exceeded' })
    expect(terminate).not.toHaveBeenCalled()
    await expect(runtime.start()).rejects.toThrow(/retry Stop/i)
    await runtime.stop()
    expect(terminate).toHaveBeenCalledOnce()
    expect(runtime.getSnapshot().state).toBe('idle')
  })

  it('does not construct a replacement runtime when restart cannot flush stopped storage', async () => {
    const createModule = vi.fn(async () => {
      let state = 1
      return {
        getState: () => state,
        getBuildMetadataJson: () => '{}',
        storage: { flushNow: async () => { throw new Error('quota exceeded') } },
        requestShutdown: async () => { state = 4 },
        terminate: vi.fn(),
      }
    })
    const runtime = createRuntimeManager({ crossOriginIsolated: true, createModule })

    await runtime.start()
    await expect(runtime.restart()).rejects.toThrow('quota exceeded')

    expect(createModule).toHaveBeenCalledTimes(1)
    expect(runtime.getSnapshot()).toMatchObject({ state: 'failed', error: 'quota exceeded' })
  })

  it('does not flush or terminate until a timed-out C++ stopped acknowledgement is later confirmed', async () => {
    const events = []
    let cppState = 1
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      shutdownTimeoutMs: 0,
      createModule: async () => ({
        getState: () => cppState,
        getBuildMetadataJson: () => '{}',
        storage: { flushNow: async () => events.push('flush') },
        requestShutdown: async () => events.push('shutdown'),
        terminate: async () => events.push('terminate'),
      }),
    })

    await runtime.start()
    cppState = 2
    await expect(runtime.stop()).rejects.toThrow(/timed out waiting for C\+\+ shutdown/i)
    expect(events).toEqual(['shutdown'])
    await expect(runtime.restart()).rejects.toThrow(/timed out waiting for C\+\+ shutdown/i)
    expect(events).toEqual(['shutdown'])

    cppState = 4
    await runtime.stop()

    expect(events).toEqual(['shutdown', 'flush', 'terminate'])
    expect(runtime.getSnapshot().state).toBe('idle')
  })

  it('reissues a shutdown request only when the prior call threw before C++ left Ready', async () => {
    const events = []
    let state = 1
    let requests = 0
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => ({
        getState: () => state,
        getBuildMetadataJson: () => '{}',
        storage: { flushNow: async () => events.push('flush') },
        requestShutdown: async () => {
          requests += 1
          events.push(`request-${requests}`)
          if (requests === 1) throw new Error('dispatch failed')
          state = 4
        },
        terminate: async () => events.push('terminate'),
      }),
    })

    await runtime.start()
    await expect(runtime.stop()).rejects.toThrow('dispatch failed')
    expect(events).toEqual(['request-1'])

    await runtime.stop()
    expect(events).toEqual(['request-1', 'request-2', 'flush', 'terminate'])
    expect(runtime.getSnapshot().state).toBe('idle')
  })

  it('requires browser-main teardown acknowledgement before an application shutdown can stop', async () => {
    const source = await readFile(
      new URL('../../sources/Adapters/wasm/audio/WasmAudio.cpp', import.meta.url),
      'utf8',
    )
    const eventManager = await readFile(
      new URL('../../sources/Adapters/wasm/gui/WasmEventManager.cpp', import.meta.url),
      'utf8',
    )

    expect(source).toContain('emscripten_async_run_in_main_runtime_thread')
    expect(source).toContain('teardownComplete_')
    expect(eventManager).toContain('BrowserTeardownComplete')
    expect(eventManager).toContain('PicoTracker_Wasm_RequestShutdown()')
  })

  it('does not restart until a delayed C++ teardown acknowledgement precedes thread termination', async () => {
    const events = []
    let generation = 0
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => {
        const id = ++generation
        let state = 1
        return {
          getState: () => state,
          getBuildMetadataJson: () => '{}',
          input: { releaseAllActions() {} },
          requestShutdown() {
            events.push(`request-${id}`)
            state = 2
            setTimeout(() => {
              state = 4
              events.push(`ack-${id}`)
            }, 0)
          },
          terminate() {
            expect(state).toBe(4)
            events.push(`terminate-${id}`)
          },
        }
      },
    })

    await runtime.start()
    await runtime.restart()

    expect(events).toEqual(['request-1', 'ack-1', 'terminate-1'])
    expect(runtime.getSnapshot().state).toBe('ready')
  })

  it('boots browser-owned audio exactly once from the private Module only when explicitly enabled', async () => {
    let moduleArgument
    const calls = []
    const privateModule = {
      _PicoTracker_Wasm_BootstrapAudio: () => calls.push('bootstrap'),
      _PicoTracker_Wasm_GetState: () => 1,
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      PThread: { terminateAllThreads() {} },
    }

    const runtime = await createRuntime({
      audioWorkletEnabled: true,
      moduleFactory: async (options) => {
        moduleArgument = options
        expect(options).not.toBe(privateModule)
        options.onRuntimeInitialized.call(privateModule)
        calls.push('resolved')
        return privateModule
      },
    })

    expect(calls).toEqual(['bootstrap', 'resolved'])
    expect(moduleArgument).not.toBe(privateModule)
    expect(runtime.audio.capability).toEqual({ available: true, mode: 'worklet', reason: null })
  })

  it('marks audio unavailable instead of entering Emscripten worklet bootstrap by default', async () => {
    const calls = []
    const privateModule = {
      _PicoTracker_Wasm_BootstrapAudio: () => calls.push('bootstrap'),
      _PicoTracker_Wasm_MarkAudioUnavailable: () => calls.push('unavailable'),
      _PicoTracker_Wasm_GetState: () => 1,
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      PThread: { terminateAllThreads() {} },
    }

    const runtime = await createRuntime({
      audioWorkletEnabled: false,
      moduleFactory: async (options) => {
        options.onRuntimeInitialized.call(privateModule)
        calls.push('resolved')
        return privateModule
      },
    })

    expect(calls).toEqual(['unavailable', 'resolved'])
    expect(runtime.audio.capability).toEqual({
      available: false,
      mode: 'disabled',
      reason: 'Audio disabled; enable low-latency audio and reload.',
    })
  })

  it('uses the single pre-main browser snapshot descriptor and never re-enters an oracle export', async () => {
    const calls = []
    const memory = new SharedArrayBuffer(128)
    const words = new Uint32Array(memory)
    const privateModule = {
      HEAPU32: words,
      _PicoTracker_Wasm_MarkAudioUnavailable: () => calls.push('unavailable'),
      _PicoTracker_Wasm_GetBrowserSnapshots: () => {
        calls.push('snapshot-descriptor')
        words.set([1, 28, 0, 0, 0, 0, 0], 2)
        return 8
      },
      _PicoTracker_Wasm_GetState: () => 1,
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      PThread: { terminateAllThreads() {} },
    }
    const runtime = await createRuntime({
      audioWorkletEnabled: false,
      moduleFactory: async (options) => {
        options.onRuntimeInitialized.call(privateModule)
        return privateModule
      },
    })

    expect(calls).toEqual(['snapshot-descriptor', 'unavailable'])
    expect(calls).toEqual(['snapshot-descriptor', 'unavailable'])
  })

  it('passes the tracker canvas to Emscripten', async () => {
    const canvas = {}
    let moduleOptions
    const module = {
      _PicoTracker_Wasm_GetState: () => 1,
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      PThread: { terminateAllThreads() {} },
    }

    await createRuntime({
      canvas,
      moduleFactory: async (options) => {
        moduleOptions = options
        return module
      },
    })

    expect(moduleOptions.canvas).toBe(canvas)
  })

  it('exposes only path-contained storage test operations for the explicit persistence acceptance URL', async () => {
    vi.stubGlobal('location', { search: '?storage-test=1' })
    const writes = []
    const module = {
      FS: {
        writeFile: (path, bytes) => writes.push([path, Array.from(bytes)]),
        readFile: () => new Uint8Array([8, 7]),
        stat: () => ({}),
      },
      _PicoTracker_Wasm_GetState: () => 1,
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      PThread: { terminateAllThreads() {} },
    }

    const runtime = await createRuntime({ moduleFactory: async () => module })

    expect(globalThis.__picoTrackerStorageTest.read('/data/sample')).toEqual([8, 7])
    globalThis.__picoTrackerStorageTest.write('/data/sample', [1, 2])
    expect(writes).toEqual([['/data/sample', [1, 2]]])
    expect(() => globalThis.__picoTrackerStorageTest.read('/data/../escape')).toThrow(/outside \/data/)
    expect(globalThis.__picoTrackerStorageTest.module).toBeUndefined()
    await runtime.terminate()
    expect(globalThis.__picoTrackerStorageTest).toBeUndefined()
    vi.unstubAllGlobals()
  })

  it('does not expose the persistence acceptance handle on normal URLs', async () => {
    vi.stubGlobal('location', { search: '' })
    globalThis.__picoTrackerStorageTest = { stale: true }
    const module = {
      _PicoTracker_Wasm_GetState: () => 1,
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      PThread: { terminateAllThreads() {} },
    }

    await createRuntime({ moduleFactory: async () => module })

    expect(globalThis.__picoTrackerStorageTest).toBeUndefined()
    vi.unstubAllGlobals()
  })

  it('rejects initial IDBFS population before bootstrap or C++ main can run', async () => {
    const calls = []
    const storage = {
      initializationError: () => new Error('IDB unavailable'),
    }
    await expect(createRuntime({
      storage,
      moduleFactory: async (options) => {
        const module = {
          _PicoTracker_Wasm_BootstrapAudio: () => calls.push('bootstrap'),
          _PicoTracker_Wasm_MarkAudioUnavailable: () => calls.push('unavailable'),
        }
        options.onRuntimeInitialized.call(module)
        calls.push('main')
        return module
      },
    })).rejects.toThrow('IDB unavailable')
    expect(calls).toEqual([])
  })

  it('exports an immutable 240x240 RGBA frame copy', async () => {
    const frameBytes = 240 * 240 * 4
    const descriptorPointer = 8 + frameBytes
    const memory = new SharedArrayBuffer(frameBytes + 64)
    const heap = new Uint8Array(memory)
    const words = new Uint32Array(memory)
    const module = {
      _PicoTracker_Wasm_CaptureFrameRgba: () => 8,
      _PicoTracker_Wasm_GetFrameSnapshotSequence: () => 4,
      _PicoTracker_Wasm_GetBrowserSnapshots: () => descriptorPointer,
      _PicoTracker_Wasm_MarkAudioUnavailable() {},
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      HEAPU8: heap,
      HEAPU32: words,
    }
    heap.set([9, 8, 7, 6, 5, 4, 3, 2], 4)
    words[1] = 2
    words.set([1, 28, 8, 4, 0, 0, 0], descriptorPointer >>> 2)
    const runtime = await createRuntime({
      moduleFactory: async (options) => {
        options.onRuntimeInitialized.call(module)
        return module
      },
    })

    const frame = runtime.captureFrameRgba()

    expect(frame).toBeInstanceOf(Uint8Array)
    expect(frame).not.toBe(module.HEAPU8)
    expect(frame.slice(0, 4)).toEqual(new Uint8Array([5, 4, 3, 2]))
    module.HEAPU8[8] = 0
    expect(frame[0]).toBe(5)
  })

  it('retries a frame read when the producer publishes during the copy', async () => {
    const frameBytes = 240 * 240 * 4
    const descriptorPointer = 8 + frameBytes
    const memory = new SharedArrayBuffer(frameBytes + 64)
    const heap = new Uint8Array(memory)
    const words = new Uint32Array(memory)
    const module = {
      _PicoTracker_Wasm_CaptureFrameRgba: () => 8,
      _PicoTracker_Wasm_GetFrameSnapshotSequence: () => 4,
      _PicoTracker_Wasm_GetBrowserSnapshots: () => descriptorPointer,
      _PicoTracker_Wasm_MarkAudioUnavailable() {},
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      HEAPU8: heap,
      HEAPU32: words,
    }
    heap.fill(1, 8, 8 + 240 * 240 * 4)
    words[1] = 2
    words.set([1, 28, 8, 4, 0, 0, 0], descriptorPointer >>> 2)
    const slice = heap.slice.bind(heap)
    let copies = 0
    heap.slice = (...args) => {
      copies += 1
      if (copies === 1) {
        heap.fill(2, 8, 8 + 240 * 240 * 4)
        Atomics.store(words, 1, 4)
      }
      return slice(...args)
    }
    const runtime = await createRuntime({
      moduleFactory: async (options) => {
        options.onRuntimeInitialized.call(module)
        return module
      },
    })

    const frame = runtime.captureFrameRgba()

    expect(copies).toBe(2)
    expect(frame[0]).toBe(2)
  })

  it('terminates the old module before creating a replacement', async () => {
    const calls = []
    let acknowledgeShutdown
    const shutdownAcknowledged = new Promise((resolve) => (acknowledgeShutdown = resolve))
    const createModule = vi.fn(async () => {
      let state = 1
      return {
        getState: () => state,
        requestShutdown: async () => {
          calls.push('shutdown')
          await shutdownAcknowledged
          state = 4
          calls.push('stopped')
        },
        terminate: async () => {
          calls.push('terminate')
          await Promise.resolve()
          calls.push('terminated')
        },
      }
    })
    const runtime = createRuntimeManager({
      createModule: async () => {
        calls.push('start')
        return createModule()
      },
      crossOriginIsolated: true,
    })

    await runtime.start()
    calls.length = 0
    const restarting = runtime.restart()
    await Promise.resolve()

    expect(calls).toEqual(['shutdown'])
    acknowledgeShutdown()
    await restarting

    expect(calls).toEqual(['shutdown', 'stopped', 'terminate', 'terminated', 'start'])
    expect(runtime.getSnapshot().state).toBe('ready')
  })

  it('reports an actionable error when cross-origin isolation is absent', async () => {
    const runtime = createRuntimeManager({
      createModule: vi.fn(),
      crossOriginIsolated: false,
    })

    await expect(runtime.start()).rejects.toThrow(/cross-origin isolation/i)
    expect(runtime.getSnapshot()).toMatchObject({
      state: 'failed',
      error: expect.stringMatching(/COOP.*COEP/i),
    })
  })

  it('serializes overlapping lifecycle operations', async () => {
    let active = 0
    let peak = 0
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => {
        active += 1
        peak = Math.max(peak, active)
        await Promise.resolve()
        active -= 1
        let state = 1
        return {
          getState: () => state,
          requestShutdown() { state = 4 },
          terminate() {},
        }
      },
    })

    await Promise.all([runtime.restart(), runtime.restart()])

    expect(peak).toBe(1)
    expect(runtime.getSnapshot().state).toBe('ready')
  })

  it('does not report ready when C++ initialization failed', async () => {
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => ({
        getState: () => 3,
        getLastError: () => 'platform initialization failed',
        terminate() {},
      }),
    })

    await expect(runtime.start()).rejects.toThrow('platform initialization failed')
    expect(runtime.getSnapshot()).toMatchObject({
      state: 'failed',
      error: 'platform initialization failed',
    })
  })

  it('stays booting until C++ publishes ready', async () => {
    let cppState = 0
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => ({ getState: () => cppState, terminate() {} }),
    })

    const starting = runtime.start()
    await new Promise((resolve) => setTimeout(resolve, 0))
    expect(runtime.getSnapshot().state).toBe('booting')

    cppState = 1
    await starting
    expect(runtime.getSnapshot().state).toBe('ready')
  })

  it('publishes whether the exported C++ frame contains visible content', async () => {
    const frame = new Uint8Array(240 * 240 * 4)
    frame.set([255, 255, 255, 255], 4)
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => ({
        getState: () => 1,
        captureFrameRgba: () => frame,
        terminate() {},
      }),
    })

    await runtime.start()

    expect(runtime.getSnapshot().frameContent).toBe('rendered')
  })

  it('clears a stale module and reports cleanup failures', async () => {
    const createModule = vi
      .fn()
      .mockImplementationOnce(async () => {
        let state = 1
        return {
          getState: () => state,
          requestShutdown() { state = 4 },
          terminate() { throw new Error('worker cleanup failed') },
        }
      })
      .mockResolvedValueOnce({ getState: () => 1, terminate() {} })
    const runtime = createRuntimeManager({ crossOriginIsolated: true, createModule })

    await runtime.start()
    await expect(runtime.stop()).rejects.toThrow('worker cleanup failed')
    expect(runtime.getSnapshot()).toMatchObject({
      state: 'failed',
      error: 'worker cleanup failed',
    })

    await runtime.start()
    expect(createModule).toHaveBeenCalledTimes(2)
    expect(runtime.getSnapshot().state).toBe('ready')
  })

  it('never publishes an input bridge outside the ready state', async () => {
    const input = { releaseAllActions: vi.fn() }
    let state = 1
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => ({
        getState: () => state,
        input,
        requestShutdown() { state = 4 },
        terminate() {
          throw new Error('terminate failed')
        },
      }),
    })
    const snapshots = []
    runtime.subscribe((snapshot) => snapshots.push(snapshot))

    await runtime.start()
    await expect(runtime.stop()).rejects.toThrow('terminate failed')

    expect(input.releaseAllActions).toHaveBeenCalledOnce()
    expect(snapshots.filter(({ state }) => state !== 'ready').every(({ input }) => input === null)).toBe(true)
  })

  it('rejects module handles that cannot report C++ readiness', async () => {
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => ({ terminate() {} }),
    })

    await expect(runtime.start()).rejects.toThrow(/getState/)
    expect(runtime.getSnapshot().state).toBe('failed')
  })

  it('waits for the C++ stopped acknowledgement before resolving shutdown', async () => {
    let cppState = 1
    const module = {
      _PicoTracker_Wasm_GetState: () => cppState,
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      _PicoTracker_Wasm_RequestShutdown: () => {
        cppState = 2
        setTimeout(() => (cppState = 4), 0)
      },
      PThread: { terminateAllThreads: vi.fn() },
    }
    const handle = await createRuntime({ moduleFactory: async () => module })

    await handle.requestShutdown()

    expect(cppState).toBe(4)
  })
})
