import { describe, expect, it, vi } from 'vitest'

import { createRuntime } from '../src/handles/runtime.js'
import { createRuntimeManager } from '../src/stores/runtime.js'

describe('WASM runtime lifecycle', () => {
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

  it('exports an immutable 240x240 RGBA frame copy', async () => {
    const module = {
      _PicoTracker_Wasm_CaptureFrameRgba: () => 4,
      _PicoTracker_Wasm_SetAction() {},
      _PicoTracker_Wasm_ReleaseAllActions() {},
      _PicoTracker_Wasm_GetActionMask() {},
      _PicoTracker_Wasm_GetActionGeneration() {},
      _PicoTracker_Wasm_GetLastAction() {},
      HEAPU8: new Uint8Array([9, 8, 7, 6, 5, 4, 3, 2]),
    }
    const runtime = await createRuntime({ moduleFactory: async () => module })

    const frame = runtime.captureFrameRgba()

    expect(frame).toBeInstanceOf(Uint8Array)
    expect(frame).not.toBe(module.HEAPU8)
    expect(frame).toEqual(new Uint8Array([5, 4, 3, 2]))
    module.HEAPU8[4] = 0
    expect(frame[0]).toBe(5)
  })

  it('terminates the old module before creating a replacement', async () => {
    const calls = []
    let acknowledgeShutdown
    const shutdownAcknowledged = new Promise((resolve) => (acknowledgeShutdown = resolve))
    const createModule = vi.fn(async () => ({
      getState: () => 1,
      requestShutdown: async () => {
        calls.push('shutdown')
        await shutdownAcknowledged
        calls.push('stopped')
      },
      terminate: async () => {
        calls.push('terminate')
        await Promise.resolve()
        calls.push('terminated')
      },
    }))
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
        return { getState: () => 1, terminate() {} }
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
      .mockResolvedValueOnce({
        getState: () => 1,
        requestShutdown() {},
        terminate() {
          throw new Error('worker cleanup failed')
        },
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
    const runtime = createRuntimeManager({
      crossOriginIsolated: true,
      createModule: async () => ({
        getState: () => 1,
        input,
        requestShutdown() {},
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
