import { summarizeTrace, toChromeTrace } from '../trace/chromeTrace.js'
import { TRACE_ALL_MASK, TRACE_BENCHMARK_FIXTURE_GOLDEN_32 } from '../trace/registry.js'

const clampCapacity = (value) => Math.min(100_000, Math.max(256, Math.trunc(value || 50_000)))

export function createTraceStore(bridge, options = {}) {
  const capacity = clampCapacity(options.capacity)
  const requestFrame = options.requestAnimationFrame ?? globalThis.requestAnimationFrame?.bind(globalThis)
  const cancelFrame = options.cancelAnimationFrame ?? globalThis.cancelAnimationFrame?.bind(globalThis)
  const now = options.now ?? (() => globalThis.performance?.now?.() ?? 0)
  const document = options.document ?? globalThis.document
  const url = options.url ?? globalThis.URL
  const BlobCtor = options.Blob ?? globalThis.Blob
  const exportMetadata = Object.freeze({ ...(options.metadata ?? {}) })
  const listeners = new Set()
  const ring = new Array(capacity)
  let head = 0, size = 0, browserDropped = 0, nativeDropped = 0
  let state = 'idle', mask = TRACE_ALL_MASK, generation = 0, frameId = null
  let summaries = [], benchmark = null, error = null, lastSummaryAt = -Infinity
  let captureStartedAtMs = null, captureStoppedAtMs = null
  let snapshot

  const records = () => Array.from({ length: size }, (_, index) => ring[(head + index) % capacity])
  const captureDurationMs = () => captureStartedAtMs === null ? 0 : Math.max(
    0,
    (state === 'capturing' ? now() : captureStoppedAtMs ?? captureStartedAtMs) - captureStartedAtMs,
  )
  const buildSnapshot = () => Object.freeze({
    state, mask, generation, recordCount: size, capacity,
    dropped: browserDropped + nativeDropped, browserDropped, nativeDropped,
    summaries: Object.freeze(summaries), benchmark, error,
    captureStartedAtMs, captureStoppedAtMs, captureDurationMs: captureDurationMs(),
  })
  const publish = () => {
    snapshot = buildSnapshot()
    for (const listener of listeners) listener(snapshot)
  }
  const refreshSummaries = (force = false) => {
    const current = now()
    if (force || current - lastSummaryAt >= 250) {
      summaries = summarizeTrace(records())
      lastSummaryAt = current
    }
  }
  const append = (incoming) => {
    for (const record of incoming) {
      if (size === capacity) {
        ring[head] = record
        head = (head + 1) % capacity
        browserDropped += 1
      } else {
        ring[(head + size) % capacity] = record
        size += 1
      }
    }
  }
  const acceptDrain = (drained, force = false) => {
    if (drained.generation !== generation) return
    append(drained.records)
    nativeDropped = drained.dropped
    refreshSummaries(force)
    publish()
  }
  const fail = (caught) => {
    if (state === 'capturing' && captureStartedAtMs !== null) captureStoppedAtMs = now()
    state = 'failed'
    error = caught instanceof Error ? caught.message : String(caught)
    if (frameId !== null) cancelFrame?.(frameId)
    frameId = null
    publish()
  }
  const pump = () => {
    if (state !== 'capturing') return
    try { acceptDrain(bridge.drain()) }
    catch (caught) { fail(caught); return }
    frameId = requestFrame?.(pump) ?? null
  }
  const resetRecords = () => {
    ring.fill(undefined); head = 0; size = 0; browserDropped = 0; nativeDropped = 0
    summaries = []; benchmark = null; error = null; lastSummaryAt = -Infinity
    captureStartedAtMs = null; captureStoppedAtMs = null
  }
  const exportObject = () => toChromeTrace(records(), {
    ...exportMetadata,
    generation, mask, dropped: browserDropped + nativeDropped, benchmark,
    captureStartedAtMs, captureStoppedAtMs, captureDurationMs: captureDurationMs(),
    benchmarkFixtureGolden32: TRACE_BENCHMARK_FIXTURE_GOLDEN_32,
  })

  snapshot = buildSnapshot()
  return Object.freeze({
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    snapshot: () => snapshot,
    start(nextMask = mask) {
      mask = (nextMask >>> 0) & TRACE_ALL_MASK
      if (!mask) throw new Error('Select at least one trace category')
      if (frameId !== null) cancelFrame?.(frameId)
      resetRecords()
      generation = bridge.start(mask)
      captureStartedAtMs = now()
      state = 'capturing'
      publish()
      frameId = requestFrame?.(pump) ?? null
    },
    stop() {
      if (state !== 'capturing') return snapshot
      if (frameId !== null) cancelFrame?.(frameId)
      frameId = null
      bridge.stop()
      for (let index = 0; index < 32; index += 1) {
        const drained = bridge.drain()
        acceptDrain(drained)
        if (drained.records.length === 0) break
      }
      captureStoppedAtMs = now()
      state = 'stopped'
      refreshSummaries(true)
      publish()
      return snapshot
    },
    clear() {
      resetRecords()
      if (state === 'capturing') {
        generation = bridge.start(mask)
        captureStartedAtMs = now()
      }
      publish()
    },
    setMask(nextMask) {
      if (state === 'capturing') throw new Error('Stop capture before changing categories')
      mask = (nextMask >>> 0) & TRACE_ALL_MASK
      publish()
    },
    runBenchmark(config) {
      benchmark = bridge.runBenchmark(config)
      if (state === 'capturing') acceptDrain(bridge.drain(), true)
      else publish()
      return benchmark
    },
    drainNow() { const drained = bridge.drain(); acceptDrain(drained, true); return drained.records.length },
    exportTrace: exportObject,
    downloadTrace(filename = 'picotracker-trace.json') {
      if (!document?.createElement || !url?.createObjectURL || !BlobCtor) throw new Error('Trace download is unavailable')
      const blob = new BlobCtor([JSON.stringify(exportObject())], { type: 'application/json;charset=utf-8' })
      const objectUrl = url.createObjectURL(blob)
      try {
        const anchor = document.createElement('a')
        anchor.href = objectUrl; anchor.download = filename; anchor.click()
      } finally { url.revokeObjectURL(objectUrl) }
    },
    dispose() {
      if (state === 'capturing') this.stop()
      if (frameId !== null) cancelFrame?.(frameId)
      frameId = null
    },
  })
}
