import {
  TRACE_ABI_VERSION,
  TRACE_BENCHMARK_FIXTURE_GOLDEN_32,
  TRACE_STORAGE_MASK,
  TRACE_STORAGE_SYNC_FLAGS,
  validateTraceRecord,
} from '../trace/registry.js'

const HEADER_BYTES = 40
const RECORD_BYTES = 32
const MAX_RECORDS = 256

function number64(view, offset, label) {
  const value = view.getBigUint64(offset, true)
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error(`Trace ${label} exceeds JavaScript precision`)
  return Number(value)
}

export function createTraceBridge(module) {
  if (typeof module?._PicoTracker_Wasm_TraceStart !== 'function' ||
      typeof module?._PicoTracker_Wasm_TraceStop !== 'function' ||
      typeof module?._PicoTracker_Wasm_TraceDrain !== 'function' ||
      typeof module?._PicoTracker_Wasm_TraceStorageSync !== 'function' ||
      typeof module?._PicoTracker_Wasm_RunBenchmark !== 'function' ||
      !(module.HEAPU8 instanceof Uint8Array)) throw new Error('WASM module does not expose the trace ABI')
  let generation = 0
  let mask = 0
  let enabled = false
  return Object.freeze({
    start(nextMask) {
      mask = nextMask >>> 0
      generation = module._PicoTracker_Wasm_TraceStart(mask) >>> 0
      enabled = true
      return generation
    },
    stop() {
      enabled = false
      return module._PicoTracker_Wasm_TraceStop() >>> 0
    },
    beginStorageSync({ id, populate = false }) {
      if (!enabled || (mask & TRACE_STORAGE_MASK) === 0) return null
      const syncId = Number(id)
      if (!Number.isInteger(syncId) || syncId < 1 || syncId > 0xffff_ffff) {
        throw new RangeError('Storage trace sync id must be a non-zero uint32')
      }
      const flags = populate ? TRACE_STORAGE_SYNC_FLAGS.Populate : 0
      module._PicoTracker_Wasm_TraceStorageSync(0, syncId, flags, generation)
      return Object.freeze({ id: syncId, generation, populate: Boolean(populate) })
    },
    endStorageSync(token, { success }) {
      if (!token || !enabled || token.generation !== generation) return false
      const flags = (token.populate ? TRACE_STORAGE_SYNC_FLAGS.Populate : 0) |
        (success ? TRACE_STORAGE_SYNC_FLAGS.Success : TRACE_STORAGE_SYNC_FLAGS.Failure)
      module._PicoTracker_Wasm_TraceStorageSync(1, token.id, flags, token.generation)
      return true
    },
    runBenchmark({ iterations = 256, warmupIterations = 8, deadlineUs = 3_000 } = {}) {
      const pointer = module._PicoTracker_Wasm_RunBenchmark(iterations >>> 0, warmupIterations >>> 0, deadlineUs >>> 0) >>> 0
      if (!pointer || pointer + 80 > module.HEAPU8.byteLength) throw new Error('WASM benchmark result is outside linear memory')
      const view = new DataView(module.HEAPU8.buffer, module.HEAPU8.byteOffset, module.HEAPU8.byteLength)
      if (view.getUint32(pointer, true) !== 1 || view.getUint32(pointer + 4, true) !== 80 ||
          view.getUint32(pointer + 8, true) !== 1 || view.getUint32(pointer + 12, true) !== 1) {
        throw new Error('WASM benchmark result is incompatible')
      }
      const result = {
        version: 1, fixtureVersion: 1, iterations: view.getUint32(pointer + 16, true),
        warmupIterations: view.getUint32(pointer + 20, true), sampleCount: view.getUint32(pointer + 24, true),
        deadlineMisses: view.getUint32(pointer + 28, true), medianUs: number64(view, pointer + 32, 'benchmark median'),
        p95Us: number64(view, pointer + 40, 'benchmark p95'), p99Us: number64(view, pointer + 48, 'benchmark p99'),
        maximumUs: number64(view, pointer + 56, 'benchmark maximum'), totalUs: number64(view, pointer + 64, 'benchmark total'),
        fixtureHash: view.getUint32(pointer + 72, true), totalWork: view.getUint32(pointer + 76, true),
      }
      if (result.sampleCount !== result.iterations ||
          result.totalWork !== result.iterations * 128 * 8) {
        throw new Error('WASM benchmark result has inconsistent work counters')
      }
      if (result.iterations === 32 && result.fixtureHash !== TRACE_BENCHMARK_FIXTURE_GOLDEN_32) {
        throw new Error(
          `WASM synthetic fixture hash changed: expected 0x${TRACE_BENCHMARK_FIXTURE_GOLDEN_32.toString(16)}, ` +
          `received 0x${result.fixtureHash.toString(16)}`,
        )
      }
      return Object.freeze(result)
    },
    drain() {
      const pointer = module._PicoTracker_Wasm_TraceDrain() >>> 0
      if (!pointer) throw new Error('WASM trace drain returned a null pointer')
      const heap = module.HEAPU8
      if (pointer + HEADER_BYTES > heap.byteLength) throw new Error('Trace header is outside WASM memory')
      const view = new DataView(heap.buffer, heap.byteOffset, heap.byteLength)
      const version = view.getUint32(pointer, true)
      const headerBytes = view.getUint32(pointer + 4, true)
      const recordBytes = view.getUint32(pointer + 8, true)
      const count = view.getUint32(pointer + 12, true)
      if (version !== TRACE_ABI_VERSION || headerBytes !== HEADER_BYTES || recordBytes !== RECORD_BYTES || count > MAX_RECORDS) {
        throw new Error('WASM trace ABI is incompatible')
      }
      const end = pointer + headerBytes + count * recordBytes
      if (!Number.isSafeInteger(end) || end > heap.byteLength) throw new Error('Trace records are outside WASM memory')
      const records = []
      for (let index = 0; index < count; index += 1) {
        const offset = pointer + headerBytes + index * recordBytes
        records.push(validateTraceRecord({
          sequence: number64(view, offset, 'sequence'),
          timestampUs: number64(view, offset + 8, 'timestamp'),
          value: view.getUint32(offset + 16, true), generation: view.getUint32(offset + 20, true),
          category: view.getUint16(offset + 24, true), name: view.getUint16(offset + 26, true),
          phase: heap[offset + 28], thread: heap[offset + 29], flags: view.getUint16(offset + 30, true),
        }))
      }
      return {
        records, dropped: number64(view, pointer + 16, 'drop count'),
        mask: view.getUint32(pointer + 24, true), generation: view.getUint32(pointer + 28, true),
        enabled: view.getUint32(pointer + 32, true) !== 0,
      }
    },
  })
}
