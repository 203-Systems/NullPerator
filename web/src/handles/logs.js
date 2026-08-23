const ABI_VERSION = 1
const HEADER_BYTES = 32
const RECORD_BYTES = 320
const MAX_RECORDS = 64
const severities = ['debug', 'info', 'warn', 'error']
const decoder = new TextDecoder()

// TextDecoder intentionally rejects SharedArrayBuffer-backed views. Pthread
// WASM memory is shared, so take a bounded snapshot before decoding a field.
const decodeShared = (bytes) => decoder.decode(Uint8Array.from(bytes))

function safeInteger(value, name) {
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error(`WASM log ${name} exceeds JavaScript precision`)
  return Number(value)
}

export function createLogsHandle(module, store, options = {}) {
  if (typeof module?._PicoTracker_Wasm_LogDrain !== 'function' || !(module.HEAPU8 instanceof Uint8Array)) {
    throw new Error('WASM module does not expose the structured log ABI')
  }
  const requestFrame = options.requestAnimationFrame ?? globalThis.requestAnimationFrame?.bind(globalThis)
  const cancelFrame = options.cancelAnimationFrame ?? globalThis.cancelAnimationFrame?.bind(globalThis)
  const timeOrigin = Number.isFinite(options.timeOrigin) ? options.timeOrigin : (globalThis.performance?.timeOrigin ?? 0)
  let lastNativeDropped = 0
  let frameId = null
  let stopped = false

  function drainNow() {
    const pointer = module._PicoTracker_Wasm_LogDrain() >>> 0
    if (!pointer) return 0
    const heap = module.HEAPU8
    if (pointer + HEADER_BYTES > heap.byteLength) throw new Error('WASM log header is outside linear memory')
    const view = new DataView(heap.buffer, heap.byteOffset, heap.byteLength)
    const version = view.getUint32(pointer, true)
    const headerBytes = view.getUint32(pointer + 4, true)
    const recordBytes = view.getUint32(pointer + 8, true)
    const count = view.getUint32(pointer + 12, true)
    if (version !== ABI_VERSION || headerBytes !== HEADER_BYTES || recordBytes !== RECORD_BYTES || count > MAX_RECORDS) {
      throw new Error('WASM log ABI is incompatible')
    }
    const end = pointer + headerBytes + count * recordBytes
    if (!Number.isSafeInteger(end) || end > heap.byteLength) throw new Error('WASM log records are outside linear memory')
    const nativeDropped = safeInteger(view.getBigUint64(pointer + 16, true), 'drop count')
    const delta = nativeDropped >= lastNativeDropped ? nativeDropped - lastNativeDropped : nativeDropped
    if (delta) store.addDropped(delta)
    lastNativeDropped = nativeDropped
    const records = []
    for (let index = 0; index < count; index += 1) {
      const offset = pointer + headerBytes + index * recordBytes
      const severity = heap[offset + 16]
      const categoryLength = heap[offset + 18]
      const threadLength = heap[offset + 19]
      const messageLength = view.getUint16(offset + 20, true)
      if (severity >= severities.length || categoryLength > 24 || threadLength > 16 || messageLength > 256) {
        throw new Error('WASM log record is malformed')
      }
      const monotonicUs = safeInteger(view.getBigUint64(offset + 8, true), 'timestamp')
      records.push({
        sequence: safeInteger(view.getBigUint64(offset, true), 'sequence'),
        monotonicUs,
        wallTime: timeOrigin + monotonicUs / 1_000,
        severity: severities[severity],
        truncated: (heap[offset + 17] & 1) !== 0,
        category: decodeShared(heap.subarray(offset + 24, offset + 24 + categoryLength)),
        thread: decodeShared(heap.subarray(offset + 48, offset + 48 + threadLength)),
        message: decodeShared(heap.subarray(offset + 64, offset + 64 + messageLength)),
      })
    }
    store.appendLogs(records)
    return count
  }

  function pump() {
    if (stopped) return
    try { drainNow() }
    catch (error) {
      stopped = true
      frameId = null
      store.appendLog({
        monotonicUs: (globalThis.performance?.now?.() ?? 0) * 1_000,
        wallTime: Date.now(), severity: 'error', category: 'LOGGING', thread: 'browser',
        message: error instanceof Error ? error.message : String(error),
      })
      return
    }
    frameId = requestFrame?.(pump) ?? null
  }

  return Object.freeze({
    drainNow,
    start() {
      if (!stopped && frameId === null) frameId = requestFrame?.(pump) ?? null
    },
    stop() {
      stopped = true
      if (frameId !== null) cancelFrame?.(frameId)
      frameId = null
      try { drainNow() }
      catch (error) {
        store.appendLog({
          monotonicUs: (globalThis.performance?.now?.() ?? 0) * 1_000,
          wallTime: Date.now(), severity: 'error', category: 'LOGGING', thread: 'browser',
          message: error instanceof Error ? error.message : String(error),
        })
      }
    },
  })
}
