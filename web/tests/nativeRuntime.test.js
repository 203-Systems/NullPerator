import { describe, expect, it, vi } from 'vitest'

import { createNativeRuntimeManager } from '../src/stores/nativeRuntime.js'

describe('native C++ runtime adapter', () => {
  it('boots without WASM and forwards held input edges and repeats', async () => {
    const postNative = vi.fn(async (command) => command === 'nativeReady'
      ? {
          runtime: 'native-cpp', platform: 'ios', version: 1,
          iosVersion: '1.0', iosBuild: '1', nullPeratorVersion: '2.3-Beta3',
          buildHash: '0123456789abcdef', buildTime: '2026-09-01T01:30:00Z',
        }
      : { ok: true })
    const manager = createNativeRuntimeManager({ postNative })

    await manager.start()
    const runtime = manager.getSnapshot()
    expect(runtime.state).toBe('ready')
    expect(runtime.frameContent).toBe('native')
    expect(runtime.buildMetadata.runtime).toBe('native-cpp')
    expect(runtime.buildMetadata).toMatchObject({
      iosVersion: '1.0', iosBuild: '1', nullPeratorVersion: '2.3-Beta3',
      buildHash: '0123456789abcdef', buildTime: '2026-09-01T01:30:00Z',
    })

    runtime.input.pressAction(3)
    runtime.input.repeatAction(3)
    runtime.input.releaseAction(3)
    runtime.input.releaseAllActions()
    await Promise.resolve()

    expect(postNative.mock.calls).toEqual([
      ['nativeReady'],
      ['nativeAction', { action: 3, pressed: true, repeat: false }],
      ['nativeAction', { action: 3, pressed: true, repeat: true }],
      ['nativeAction', { action: 3, pressed: false, repeat: false }],
      ['nativeReleaseAll'],
    ])
    expect(runtime.input.getActionMask()).toBe(0)
  })

  it('publishes a native audio facade without creating WebAudio', async () => {
    const manager = createNativeRuntimeManager({
      postNative: vi.fn(async () => ({ runtime: 'native-cpp' })),
    })
    await manager.start()
    const audio = manager.getSnapshot().audio

    expect(audio.snapshot().state).toBe('native')
    await expect(audio.unlockAudio()).resolves.toMatchObject({ state: 'native' })
    await expect(audio.stop()).resolves.toBeUndefined()
  })

  it('publishes CoreMIDI routes and forwards selected input to the native core', async () => {
    const input = {
      id: 'native-input-1', name: 'Test Input', manufacturer: 'Test',
      state: 'connected', connection: 'closed', onmidimessage: null,
      async open() { this.connection = 'open'; return this },
      async close() { this.connection = 'closed'; return this },
    }
    const access = { inputs: new Map([[input.id, input]]), outputs: new Map(), onstatechange: null }
    const postNative = vi.fn(async (command) => command === 'nativeReady'
      ? { runtime: 'native-cpp' }
      : command === 'nativeMidiDrain'
        ? { packets: [], droppedNormal: 0, droppedRealtime: 0 }
        : { ok: true })
    const manager = createNativeRuntimeManager({
      postNative,
      midiOptions: {
        navigator: { requestMIDIAccess: async () => access },
        storage: { getItem: () => null, setItem: vi.fn(), removeItem: vi.fn() },
        requestAnimationFrame: vi.fn(() => 1),
        cancelAnimationFrame: vi.fn(),
      },
    })

    await manager.start()
    const midi = manager.getSnapshot().midi
    expect(midi.snapshot().state).toBe('idle')
    await midi.requestMidiAccess()
    await midi.selectMidiInput(input.id)
    input.onmidimessage({ data: new Uint8Array([0x90, 60, 100]), timeStamp: 12.5 })
    await Promise.resolve()

    expect(midi.snapshot()).toMatchObject({ state: 'ready', inputConnected: true })
    expect(postNative).toHaveBeenCalledWith('nativeMidiInput', {
      bytes: [0x90, 60, 100], timestamp: 12.5,
    })
  })
})
