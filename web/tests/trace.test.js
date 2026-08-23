import { describe, expect, it } from 'vitest'

import { createTraceBridge } from '../src/handles/trace.js'
import { summarizeTrace, toChromeTrace } from '../src/trace/chromeTrace.js'
import { createTraceStore } from '../src/stores/trace.js'
import {
  TRACE_BENCHMARK_FIXTURE_GOLDEN_32,
  TRACE_INPUT_DROP_FLAGS,
} from '../src/trace/registry.js'

const event = (sequence, timestampUs, phase, overrides = {}) => ({
  sequence, timestampUs, value: 0, generation: 1, category: 1, name: 1,
  phase, thread: 1, flags: 0, ...overrides,
})

describe('WASM performance tracing', () => {
  it('exports ordered B/E/i/C events without dropping numeric record args', () => {
    const records = [
      event(4, 40, 3, { value: 44, generation: 7, flags: 4 }),
      // Publication order may not be timestamp order across native producers.
      event(2, 60, 1, { value: 22, generation: 7, flags: 2 }),
      event(1, 10, 0, { value: 11, generation: 7, flags: 1 }),
      event(3, 30, 2, { value: 33, generation: 7, flags: 3 }),
    ]
    const trace = toChromeTrace(records, { dropped: 2, captureDurationMs: 12.5 })
    expect(trace.traceEvents.map((entry) => entry.ph)).toEqual(['M', 'M', 'B', 'E', 'i', 'C'])
    expect(trace.traceEvents.slice(2).map((entry) => entry.args)).toEqual([
      { value: 11, sequence: 1, generation: 7, flags: 1 },
      { value: 22, sequence: 2, generation: 7, flags: 2 },
      { value: 33, sequence: 3, generation: 7, flags: 3 },
      { value: 44, sequence: 4, generation: 7, flags: 4 },
    ])
    expect(trace.traceEvents[4]).toMatchObject({ name: 'frame', cat: 'ui', s: 't' })
    expect(trace).toMatchObject({
      displayTimeUnit: 'ms',
      metadata: {
        version: 1, dropped: 2, captureDurationMs: 12.5,
        recordCount: 4, recordStartUs: 10, recordEndUs: 60, recordDurationUs: 50,
        recordTimeUnit: 'us', captureTimeUnit: 'ms',
      },
    })
  })

  it('exports every audio snapshot counter with its fixed name and value', () => {
    const counters = [
      event(1, 10, 3, { category: 4, name: 7, value: 2048 }),
      event(2, 11, 3, { category: 4, name: 20, value: 17 }),
      event(3, 12, 3, { category: 4, name: 21, value: 3 }),
      event(4, 13, 3, { category: 4, name: 22, value: 5 }),
      event(5, 14, 3, { category: 4, name: 28, value: 611 }),
      event(6, 15, 3, { category: 4, name: 29, value: 913 }),
      event(7, 16, 3, { category: 4, name: 30, value: 1201 }),
      event(8, 17, 3, { category: 4, name: 31, value: 2903 }),
      event(9, 18, 3, { category: 4, name: 32, value: 2 }),
    ]
    expect(toChromeTrace(counters).traceEvents.slice(2)).toEqual([
      expect.objectContaining({ name: 'audio.snapshot', cat: 'audio', ph: 'C', args: expect.objectContaining({ value: 2048 }) }),
      expect.objectContaining({ name: 'audio.callback_count', cat: 'audio', ph: 'C', args: expect.objectContaining({ value: 17 }) }),
      expect.objectContaining({ name: 'audio.underrun_frames', cat: 'audio', ph: 'C', args: expect.objectContaining({ value: 3 }) }),
      expect.objectContaining({ name: 'audio.overrun_frames', cat: 'audio', ph: 'C', args: expect.objectContaining({ value: 5 }) }),
      expect.objectContaining({ name: 'audio.render_duration_us', cat: 'audio', ph: 'C', args: expect.objectContaining({ value: 611 }) }),
      expect.objectContaining({ name: 'audio.callback_duration_us', cat: 'audio', ph: 'C', args: expect.objectContaining({ value: 913 }) }),
      expect.objectContaining({ name: 'audio.callback_max_duration_us', cat: 'audio', ph: 'C', args: expect.objectContaining({ value: 1201 }) }),
      expect.objectContaining({ name: 'audio.callback_deadline_us', cat: 'audio', ph: 'C', args: expect.objectContaining({ value: 2903 }) }),
      expect.objectContaining({ name: 'audio.callback_processing_deadline_misses', cat: 'audio', ph: 'C', args: expect.objectContaining({ value: 2 }) }),
    ])
  })

  it('exports correlated input acceptance and presentation with a real latency counter', () => {
    const records = [
      event(1, 1_000, 2, { category: 2, name: 24, thread: 2, value: 41, flags: 3 }),
      event(2, 1_010, 2, { category: 2, name: 24, thread: 2, value: 43, flags: 6 }),
      // InputDispatch may complete earlier, but it is intentionally not the
      // endpoint of the latency measurement.
      event(3, 1_040, 0, { category: 2, name: 5 }),
      event(4, 1_060, 1, { category: 2, name: 5 }),
      event(5, 1_175, 2, { category: 2, name: 25, value: 41, flags: 3 }),
      event(6, 1_175, 3, { category: 2, name: 26, value: 175, flags: 41 }),
      event(7, 1_175, 2, { category: 2, name: 25, value: 43, flags: 6 }),
      event(8, 1_175, 3, { category: 2, name: 26, value: 165, flags: 43 }),
      event(9, 3_000, 2, {
        category: 2, name: 27, thread: 2, value: 42,
        flags: TRACE_INPUT_DROP_FLAGS.NoPresentation | 6,
      }),
      event(10, 3_010, 2, {
        category: 2, name: 27, thread: 2, value: 44,
        flags: TRACE_INPUT_DROP_FLAGS.Coalesced | 2,
      }),
    ]

    const events = toChromeTrace(records).traceEvents.filter((entry) => entry.ph !== 'M')
    expect(events.find((entry) => entry.name === 'input.accepted' && entry.args.correlation === 41)).toMatchObject({
      name: 'input.accepted', ph: 'i',
      args: { correlation: 41, action: 3 },
    })
    expect(events.find((entry) => entry.name === 'input.presented' && entry.args.correlation === 41)).toMatchObject({
      name: 'input.presented', ph: 'i',
      args: { correlation: 41, action: 3 },
    })
    expect(events.find((entry) => entry.name === 'input.to_frame_latency_us' && entry.args.correlation === 41)).toMatchObject({
      name: 'input.to_frame_latency_us', ph: 'C',
      args: { latencyUs: 175, correlation: 41 },
    })
    expect(events.find((entry) => entry.name === 'input.to_frame_latency_us' && entry.args.correlation === 43)).toMatchObject({
      name: 'input.to_frame_latency_us', ph: 'C',
      args: { latencyUs: 165, correlation: 43 },
    })
    expect(events.find((entry) => entry.name === 'input.latency_dropped')).toMatchObject({
      name: 'input.latency_dropped', ph: 'i',
      args: { correlation: 42, action: 6, reason: 'no-presentation' },
    })
    expect(events.find((entry) => entry.name === 'input.latency_dropped' && entry.args.correlation === 44)).toMatchObject({
      args: { correlation: 44, action: 2, reason: 'coalesced' },
    })

    const summaries = summarizeTrace(records)
    expect(summaries).toEqual(expect.arrayContaining([
      expect.objectContaining({
        nameText: 'input.to_frame_latency_us', count: 2,
        p50Us: 165, p95Us: 175, maxUs: 175,
      }),
      expect.objectContaining({
        nameText: 'input.dispatch', count: 1, p50Us: 20, maxUs: 20,
      }),
    ]))
  })

  it('exports correlated Web MIDI queue hand-offs and summarizes their latency', () => {
    const records = [
      event(1, 10_000, 2, { category: 256, name: 33, thread: 2, value: 101 }),
      event(2, 12_500, 3, { category: 256, name: 34, value: 2_500, flags: 101 }),
      // The scheduled MIDIOutput timestamp is intentionally not represented
      // in queue latency: output settles when browser-main drains the packet.
      event(3, 20_000, 2, { category: 256, name: 35, value: 202 }),
      event(4, 20_750, 3, { category: 256, name: 36, thread: 2, value: 750, flags: 202 }),
    ]

    const events = toChromeTrace(records).traceEvents.filter((entry) => entry.ph !== 'M')
    expect(events).toEqual([
      expect.objectContaining({
        name: 'midi.input_accepted', ph: 'i', tid: 2,
        args: expect.objectContaining({ correlation: 101 }),
      }),
      expect.objectContaining({
        name: 'midi.input_latency_us', ph: 'C', tid: 1,
        args: expect.objectContaining({ latencyUs: 2_500, correlation: 101 }),
      }),
      expect.objectContaining({
        name: 'midi.output_queued', ph: 'i', tid: 1,
        args: expect.objectContaining({ correlation: 202 }),
      }),
      expect.objectContaining({
        name: 'midi.output_latency_us', ph: 'C', tid: 2,
        args: expect.objectContaining({ latencyUs: 750, correlation: 202 }),
      }),
    ])
    expect(summarizeTrace(records)).toEqual(expect.arrayContaining([
      expect.objectContaining({
        nameText: 'midi.input_latency_us', count: 1,
        p50Us: 2_500, maxUs: 2_500,
      }),
      expect.objectContaining({
        nameText: 'midi.output_latency_us', count: 1,
        p50Us: 750, maxUs: 750,
      }),
    ]))
  })

  it('pairs nested scopes and computes deterministic nearest-rank summaries', () => {
    const records = [
      event(1, 0, 0), event(2, 2, 0), event(3, 5, 1), event(4, 10, 1),
      event(5, 20, 0), event(6, 40, 1), event(7, 50, 0), event(8, 90, 1),
    ]
    expect(summarizeTrace(records)[0]).toMatchObject({ count: 4, p50Us: 10, p95Us: 40, p99Us: 40, maxUs: 40, totalUs: 73 })
  })

  it('strictly decodes the fixed native ABI and rejects unknown ids', () => {
    const heap = new Uint8Array(new ArrayBuffer(512))
    const view = new DataView(heap.buffer)
    const pointer = 64
    view.setUint32(pointer, 1, true); view.setUint32(pointer + 4, 40, true)
    view.setUint32(pointer + 8, 32, true); view.setUint32(pointer + 12, 1, true)
    view.setBigUint64(pointer + 16, 3n, true); view.setUint32(pointer + 24, 1, true)
    view.setUint32(pointer + 28, 9, true); view.setUint32(pointer + 32, 1, true)
    const offset = pointer + 40
    view.setBigUint64(offset, 5n, true); view.setBigUint64(offset + 8, 123n, true)
    view.setUint32(offset + 20, 9, true); view.setUint16(offset + 24, 1, true)
    view.setUint16(offset + 26, 1, true); heap[offset + 28] = 0; heap[offset + 29] = 1
    const benchmark = 256
    view.setUint32(benchmark, 1, true); view.setUint32(benchmark + 4, 80, true)
    view.setUint32(benchmark + 8, 1, true); view.setUint32(benchmark + 12, 1, true)
    view.setUint32(benchmark + 16, 32, true); view.setUint32(benchmark + 24, 32, true)
    view.setBigUint64(benchmark + 32, 10n, true); view.setBigUint64(benchmark + 40, 20n, true)
    view.setBigUint64(benchmark + 48, 30n, true); view.setBigUint64(benchmark + 56, 40n, true)
    view.setBigUint64(benchmark + 64, 50n, true)
    view.setUint32(benchmark + 72, TRACE_BENCHMARK_FIXTURE_GOLDEN_32, true)
    view.setUint32(benchmark + 76, 32768, true)
    const module = {
      HEAPU8: heap, _PicoTracker_Wasm_TraceStart: (mask) => mask,
      _PicoTracker_Wasm_TraceStop: () => 9, _PicoTracker_Wasm_TraceDrain: () => pointer,
      _PicoTracker_Wasm_TraceStorageSync: () => {},
      _PicoTracker_Wasm_RunBenchmark: () => benchmark,
    }
    const bridge = createTraceBridge(module)
    expect(bridge.start(1)).toBe(1)
    expect(bridge.drain()).toMatchObject({ dropped: 3, mask: 1, generation: 9, enabled: true, records: [{ sequence: 5, timestampUs: 123, phase: 0 }] })
    expect(bridge.runBenchmark()).toMatchObject({
      sampleCount: 32, medianUs: 10, p99Us: 30,
      fixtureHash: TRACE_BENCHMARK_FIXTURE_GOLDEN_32, totalWork: 32768,
    })
    view.setUint32(benchmark + 72, 0x1234, true)
    expect(() => bridge.runBenchmark()).toThrow(/synthetic fixture hash changed/i)
    view.setUint32(benchmark + 72, TRACE_BENCHMARK_FIXTURE_GOLDEN_32, true)
    view.setUint32(benchmark + 76, 1, true)
    expect(() => bridge.runBenchmark()).toThrow(/work counters/i)
    view.setUint32(benchmark + 76, 32768, true)
    view.setUint16(offset + 26, 999, true)
    expect(() => bridge.drain()).toThrow(/unknown registry/i)
  })

  it('emits capture-generation-safe storage sync scopes through the native ring', () => {
    const emitted = []
    let generation = 0
    const module = {
      HEAPU8: new Uint8Array(new ArrayBuffer(128)),
      _PicoTracker_Wasm_TraceStart: () => ++generation,
      _PicoTracker_Wasm_TraceStop: () => generation,
      _PicoTracker_Wasm_TraceDrain: () => 1,
      _PicoTracker_Wasm_TraceStorageSync: (...values) => emitted.push(values),
      _PicoTracker_Wasm_RunBenchmark: () => 1,
    }
    const bridge = createTraceBridge(module)

    bridge.start(128)
    const token = bridge.beginStorageSync({ id: 41, populate: false })
    expect(token).toEqual({ id: 41, generation: 1, populate: false })
    expect(bridge.endStorageSync(token, { success: true })).toBe(true)
    expect(emitted).toEqual([[0, 41, 0, 1], [1, 41, 1, 1]])

    const stale = bridge.beginStorageSync({ id: 42, populate: true })
    bridge.stop()
    bridge.start(128)
    expect(bridge.endStorageSync(stale, { success: false })).toBe(false)
    expect(emitted.at(-1)).toEqual([0, 42, 4, 1])

    bridge.stop()
    bridge.start(1)
    expect(bridge.beginStorageSync({ id: 43 })).toBeNull()
    expect(emitted).toHaveLength(3)
  })

  it('exports and summarizes correlated successful and failed IDBFS syncs', () => {
    const records = [
      event(1, 100, 0, { category: 128, name: 23, thread: 2, value: 7 }),
      event(2, 110, 0, { category: 128, name: 23, thread: 2, value: 8, flags: 4 }),
      event(3, 150, 1, { category: 128, name: 23, thread: 2, value: 7, flags: 1 }),
      event(4, 180, 1, { category: 128, name: 23, thread: 2, value: 8, flags: 6 }),
    ]

    const storageEvents = toChromeTrace(records).traceEvents.slice(2)
    expect(storageEvents).toEqual([
      expect.objectContaining({ name: 'storage.sync', ph: 'B', args: expect.objectContaining({ syncId: 7, populate: false }) }),
      expect.objectContaining({ name: 'storage.sync', ph: 'B', args: expect.objectContaining({ syncId: 8, populate: true }) }),
      expect.objectContaining({ name: 'storage.sync', ph: 'E', args: expect.objectContaining({ syncId: 7, outcome: 'success' }) }),
      expect.objectContaining({ name: 'storage.sync', ph: 'E', args: expect.objectContaining({ syncId: 8, outcome: 'failure' }) }),
    ])
    expect(summarizeTrace(records)).toEqual([
      expect.objectContaining({
        nameText: 'storage.sync', threadName: 'browser', count: 2,
        successCount: 1, failureCount: 1, p50Us: 50, p95Us: 70,
        maxUs: 70, totalUs: 120,
      }),
    ])
  })

  it('coordinates bounded capture, summaries, benchmark, export, and teardown', () => {
    let generation = 0
    const batches = [
      { records: [event(1, 10, 0), event(2, 30, 1)], dropped: 2, generation: 1 },
      { records: [], dropped: 2, generation: 1 },
    ]
    const bridge = {
      start: () => ++generation, stop: () => generation,
      drain: () => ({ mask: 1, enabled: true, ...(batches.shift() ?? { records: [], dropped: 2, generation }) }),
      runBenchmark: () => ({ fixtureHash: 123, medianUs: 10 }),
    }
    const frames = []
    let timeMs = 1_000
    const trace = createTraceStore(bridge, {
      capacity: 256, now: () => timeMs, metadata: { build: { commit: 'abc123' } },
      requestAnimationFrame: (callback) => { frames.push(callback); return frames.length }, cancelAnimationFrame: () => {} })
    trace.setMask(1)
    trace.start()
    expect(trace.snapshot()).toMatchObject({ captureStartedAtMs: 1_000, captureDurationMs: 0 })
    timeMs = 1_250
    frames.shift()()
    expect(trace.snapshot()).toMatchObject({
      state: 'capturing', recordCount: 2, nativeDropped: 2,
      captureStartedAtMs: 1_000, captureDurationMs: 250,
    })
    expect(trace.snapshot().summaries[0]).toMatchObject({ nameText: 'frame', count: 1, p95Us: 20 })
    expect(trace.runBenchmark()).toMatchObject({ fixtureHash: 123 })
    timeMs = 1_900
    trace.stop()
    expect(trace.snapshot()).toMatchObject({
      state: 'stopped', benchmark: { fixtureHash: 123 },
      captureStartedAtMs: 1_000, captureStoppedAtMs: 1_900,
      captureDurationMs: 900,
    })
    expect(trace.exportTrace().traceEvents.map((entry) => entry.ph)).toEqual(['M', 'M', 'B', 'E'])
    expect(trace.exportTrace().metadata).toMatchObject({
      captureStartedAtMs: 1_000, captureStoppedAtMs: 1_900,
      captureDurationMs: 900, recordCount: 2, recordDurationUs: 20,
      benchmarkFixtureGolden32: TRACE_BENCHMARK_FIXTURE_GOLDEN_32,
      build: { commit: 'abc123' }, recordTimeUnit: 'us', captureTimeUnit: 'ms',
    })
    trace.dispose()
  })
})
