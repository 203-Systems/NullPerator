const requiredExports = [
  '_PicoTracker_Wasm_MidiInputBuffer',
  '_PicoTracker_Wasm_MidiInputCapacity',
  '_PicoTracker_Wasm_MidiInput',
  '_PicoTracker_Wasm_MidiDrainOutput',
  '_PicoTracker_Wasm_MidiDisconnect',
  '_PicoTracker_Wasm_MidiSetOutputConnected',
]

function checkedModule(module) {
  if (!module?.HEAPU8) throw new Error('WASM shared byte memory is unavailable for MIDI')
  for (const name of requiredExports) {
    if (typeof module[name] !== 'function') throw new Error(`WASM MIDI export is unavailable: ${name}`)
  }
  return module
}

function checkedRange(heap, pointer, length, label) {
  if (!Number.isSafeInteger(pointer) || pointer <= 0 || !Number.isSafeInteger(length) || length < 0 || pointer + length > heap.byteLength) {
    throw new Error(`WASM MIDI ${label} is outside shared memory`)
  }
}

export function createMidiBridge(module, options = {}) {
  module = checkedModule(module)
  const candidateTimeOrigin = options.timeOrigin ?? globalThis.performance?.timeOrigin
  const timeOrigin = Number.isFinite(candidateTimeOrigin) && candidateTimeOrigin >= 0
    ? candidateTimeOrigin
    : null

  return Object.freeze({
    submitInput(input, timestamp) {
      const bytes = input instanceof Uint8Array
        ? input
        : ArrayBuffer.isView(input)
          ? new Uint8Array(input.buffer, input.byteOffset, input.byteLength)
          : new Uint8Array(input)
      if (!Number.isFinite(timestamp) || timestamp < 0) throw new Error('MIDI input timestamp is invalid')
      const capacity = module._PicoTracker_Wasm_MidiInputCapacity()
      if (!Number.isSafeInteger(capacity) || capacity <= 0 || bytes.byteLength > capacity) throw new Error('MIDI input exceeds the fixed WASM staging capacity')
      const pointer = module._PicoTracker_Wasm_MidiInputBuffer()
      checkedRange(module.HEAPU8, pointer, capacity, 'input buffer')
      module.HEAPU8.set(bytes, pointer)
      return module._PicoTracker_Wasm_MidiInput(pointer, bytes.byteLength, timestamp) === 1
    },
    drainOutput() {
      const pointer = module._PicoTracker_Wasm_MidiDrainOutput()
      if (!pointer) return { packets: [], droppedNormal: 0, droppedRealtime: 0 }
      checkedRange(module.HEAPU8, pointer, 24, 'drain header')
      const view = new DataView(module.HEAPU8.buffer)
      const version = view.getUint32(pointer, true)
      const headerBytes = view.getUint32(pointer + 4, true)
      const recordBytes = view.getUint32(pointer + 8, true)
      const count = view.getUint32(pointer + 12, true)
      if (version !== 1 || headerBytes !== 24 || recordBytes !== 24 || count > 128) throw new Error('WASM MIDI drain ABI is incompatible')
      checkedRange(module.HEAPU8, pointer, headerBytes + count * recordBytes, 'drain buffer')
      const packets = []
      for (let index = 0; index < count; index += 1) {
        const offset = pointer + headerBytes + index * recordBytes
        const sequenceLow = view.getUint32(offset, true)
        const sequenceHigh = view.getUint32(offset + 4, true)
        const sequence = sequenceHigh * 0x1_0000_0000 + sequenceLow
        if (!Number.isSafeInteger(sequence)) throw new Error('WASM MIDI sequence exceeds JavaScript precision')
        const rawTimestamp = view.getFloat64(offset + 8, true)
        const length = module.HEAPU8[offset + 16]
        if (!Number.isFinite(rawTimestamp) || rawTimestamp < 0 || length < 1 || length > 3) throw new Error('WASM MIDI drain record is invalid')
        // Emscripten 6's get_now() is epoch-based, which keeps the C++
        // producer timestamp comparable across pthread globals. Web MIDI
        // consumes page-relative DOMHighResTimeStamp values instead.
        const timestamp = timeOrigin !== null && rawTimestamp >= timeOrigin
          ? rawTimestamp - timeOrigin
          : rawTimestamp
        packets.push({
          sequence,
          timestamp,
          bytes: module.HEAPU8.slice(offset + 17, offset + 17 + length),
        })
      }
      return {
        packets,
        droppedNormal: view.getUint32(pointer + 16, true),
        droppedRealtime: view.getUint32(pointer + 20, true),
      }
    },
    disconnect(directions) {
      if (!Number.isInteger(directions) || directions < 1 || directions > 3) throw new Error('MIDI disconnect direction is invalid')
      module._PicoTracker_Wasm_MidiDisconnect(directions)
    },
    setOutputConnected(connected) {
      module._PicoTracker_Wasm_MidiSetOutputConnected(connected ? 1 : 0)
    },
  })
}
