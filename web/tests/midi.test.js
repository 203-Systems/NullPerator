import { describe, expect, it, vi } from 'vitest'

import { createMidiBridge } from '../src/handles/midi.js'
import { createMidiStore } from '../src/stores/midi.js'

function moduleFixture() {
  const heap = new Uint8Array(new ArrayBuffer(4096))
  const view = new DataView(heap.buffer)
  view.setUint32(1024, 1, true)
  view.setUint32(1028, 32, true)
  const calls = { input: [], disconnect: [], connected: [] }
  const module = {
    HEAPU8: heap,
    _PicoTracker_Wasm_MidiInputBuffer: () => 128,
    _PicoTracker_Wasm_MidiInputCapacity: () => 16,
    _PicoTracker_Wasm_MidiInput: (pointer, size, timestamp) => {
      calls.input.push({ bytes: Array.from(heap.slice(pointer, pointer + size)), timestamp })
      return 1
    },
    _PicoTracker_Wasm_MidiDrainOutput: () => 512,
    _PicoTracker_Wasm_MidiDisconnect: (directions) => calls.disconnect.push(directions),
    _PicoTracker_Wasm_MidiSetOutputConnected: (connected) => calls.connected.push(connected),
    _PicoTracker_Wasm_MidiDiagnosticSnapshot: () => 1024,
  }
  return { module, heap, calls }
}

function writeDrain(heap, packets, droppedNormal = 0, droppedRealtime = 0) {
  const view = new DataView(heap.buffer)
  view.setUint32(512, 1, true)
  view.setUint32(516, 24, true)
  view.setUint32(520, 24, true)
  view.setUint32(524, packets.length, true)
  view.setUint32(528, droppedNormal, true)
  view.setUint32(532, droppedRealtime, true)
  packets.forEach((packet, index) => {
    const offset = 536 + index * 24
    view.setUint32(offset, packet.sequence ?? index, true)
    view.setUint32(offset + 4, 0, true)
    view.setFloat64(offset + 8, packet.timestamp, true)
    heap[offset + 16] = packet.bytes.length
    heap.set(packet.bytes, offset + 17)
  })
}

function port(type, id, options = {}) {
  return {
    type, id, name: options.name ?? id, manufacturer: options.manufacturer ?? 'Test',
    state: options.state ?? 'connected', connection: 'closed', onmidimessage: null,
    open: vi.fn(async function open() { this.connection = 'open'; return this }),
    close: vi.fn(async function close() { this.connection = 'closed'; return this }),
    send: vi.fn(),
  }
}

function memoryStorage(initial = {}) {
  const values = new Map(Object.entries(initial))
  return {
    getItem: (key) => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, String(value)),
    removeItem: (key) => values.delete(key),
  }
}

describe('Web MIDI bridge and store', () => {
  it('copies input through fixed WASM memory and decodes the bounded output ABI', () => {
    const { module, heap, calls } = moduleFixture()
    const bridge = createMidiBridge(module, { timeOrigin: 1_000 })
    writeDrain(heap, [
      { sequence: 9, timestamp: 1_012.5, bytes: [0x90, 60, 100] },
      { sequence: 10, timestamp: 1_013.0, bytes: [0xF8] },
    ], 2, 0)

    expect(bridge.submitInput(new Uint8Array([0x90, 64, 127]), 7.25)).toBe(true)
    expect(calls.input).toEqual([{ bytes: [0x90, 64, 127], timestamp: 7.25 }])
    expect(bridge.drainOutput()).toEqual({
      packets: [
        { sequence: 9, timestamp: 12.5, bytes: new Uint8Array([0x90, 60, 100]) },
        { sequence: 10, timestamp: 13, bytes: new Uint8Array([0xF8]) },
      ],
      droppedNormal: 2,
      droppedRealtime: 0,
    })
    expect(() => bridge.submitInput(new Uint8Array(17), 0)).toThrow(/capacity/i)
  })

  it('requests permission only explicitly, restores stable ids, and routes input timestamps', async () => {
    const input = port('input', 'in-1')
    const output = port('output', 'out-1')
    const access = { inputs: new Map([[input.id, input]]), outputs: new Map([[output.id, output]]), onstatechange: null }
    const requestMIDIAccess = vi.fn(async () => access)
    const bridge = { submitInput: vi.fn(() => true), drainOutput: vi.fn(() => ({ packets: [], droppedNormal: 0, droppedRealtime: 0 })), disconnect: vi.fn(), setOutputConnected: vi.fn() }
    const frames = []
    const store = createMidiStore(bridge, {
      navigator: { requestMIDIAccess },
      storage: memoryStorage({ 'picotracker.midi.input.v1': 'in-1', 'picotracker.midi.output.v1': 'out-1' }),
      requestAnimationFrame: (callback) => { frames.push(callback); return frames.length },
      cancelAnimationFrame: vi.fn(),
      now: () => 100,
    })

    expect(requestMIDIAccess).not.toHaveBeenCalled()
    expect(store.snapshot().state).toBe('idle')
    await store.requestMidiAccess()
    expect(requestMIDIAccess).toHaveBeenCalledWith({ sysex: false })
    expect(store.snapshot()).toMatchObject({ state: 'ready', selectedInputId: 'in-1', selectedOutputId: 'out-1' })
    input.onmidimessage({ data: new Uint8Array([0x90, 60, 100]), timeStamp: 42.5 })
    expect(bridge.submitInput).toHaveBeenCalledWith(new Uint8Array([0x90, 60, 100]), 42.5)
    expect(bridge.setOutputConnected).toHaveBeenCalledWith(true)
  })

  it('sends drained packets, preserving future timestamps and delivering stale packets immediately', async () => {
    const output = port('output', 'out-1')
    const access = { inputs: new Map(), outputs: new Map([[output.id, output]]), onstatechange: null }
    const bridge = {
      submitInput: vi.fn(), disconnect: vi.fn(), setOutputConnected: vi.fn(),
      drainOutput: vi.fn(() => ({
        packets: [
          { sequence: 1, timestamp: 90, bytes: new Uint8Array([0xF8]) },
          { sequence: 2, timestamp: 125, bytes: new Uint8Array([0x90, 60, 100]) },
        ],
        droppedNormal: 3, droppedRealtime: 0,
      })),
    }
    let frame
    const store = createMidiStore(bridge, {
      navigator: { requestMIDIAccess: async () => access }, storage: memoryStorage(),
      requestAnimationFrame: (callback) => { frame = callback; return 1 },
      cancelAnimationFrame: vi.fn(), now: () => 100,
    })
    await store.requestMidiAccess()
    await store.selectMidiOutput('out-1')
    frame()

    expect(output.send).toHaveBeenNthCalledWith(1, new Uint8Array([0xF8]))
    expect(output.send).toHaveBeenNthCalledWith(2, new Uint8Array([0x90, 60, 100]), 125)
    expect(store.snapshot()).toMatchObject({ droppedNormal: 3, droppedRealtime: 0 })
  })

  it('keeps disconnected stable ids, rebinds on statechange, and tears down handlers', async () => {
    const firstInput = port('input', 'in-1')
    const access = { inputs: new Map([[firstInput.id, firstInput]]), outputs: new Map(), onstatechange: null }
    let resetGeneration = 0
    const bridge = {
      submitInput: vi.fn(() => true),
      drainOutput: vi.fn(() => ({ packets: [], droppedNormal: 0, droppedRealtime: 0 })),
      disconnect: vi.fn((directions) => (directions & 1) ? resetGeneration + 1 : null),
      inputResetGeneration: () => resetGeneration,
      setOutputConnected: vi.fn(),
    }
    const cancelAnimationFrame = vi.fn()
    const store = createMidiStore(bridge, {
      navigator: { requestMIDIAccess: async () => access }, storage: memoryStorage(),
      requestAnimationFrame: () => 7, cancelAnimationFrame, now: () => 0,
      delay: async () => { resetGeneration += 1 },
    })
    await store.requestMidiAccess()
    await store.selectMidiInput('in-1')
    firstInput.state = 'disconnected'
    access.inputs.delete('in-1')
    await access.onstatechange()
    expect(store.snapshot()).toMatchObject({ selectedInputId: 'in-1', inputConnected: false })

    const reconnected = port('input', 'in-1')
    access.inputs.set('in-1', reconnected)
    await access.onstatechange()
    expect(store.snapshot().inputConnected).toBe(true)
    expect(typeof reconnected.onmidimessage).toBe('function')

    await store.stop()
    expect(access.onstatechange).toBeNull()
    expect(reconnected.onmidimessage).toBeNull()
    expect(bridge.disconnect).toHaveBeenCalledWith(3)
    expect(cancelAnimationFrame).toHaveBeenCalledWith(7)
  })

  it('reports unsupported and denied Web MIDI without requesting during construction', async () => {
    const bridge = { disconnect: vi.fn() }
    const unsupported = createMidiStore(bridge, { navigator: {}, storage: memoryStorage() })
    expect(unsupported.snapshot().state).toBe('unsupported')
    await expect(unsupported.requestMidiAccess()).rejects.toThrow(/unsupported/i)

    const denied = createMidiStore(bridge, {
      navigator: { requestMIDIAccess: async () => { throw new DOMException('no', 'NotAllowedError') } },
      storage: memoryStorage(),
    })
    await expect(denied.requestMidiAccess()).rejects.toThrow('no')
    expect(denied.snapshot()).toMatchObject({ state: 'denied', error: 'no' })
  })
})
