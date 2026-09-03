import {
  TRACE_CATEGORIES,
  TRACE_INPUT_ACTION_MASK,
  TRACE_INPUT_DROP_FLAGS,
  TRACE_INPUT_TO_FRAME_LATENCY_NAME,
  TRACE_MIDI_INSTANT_NAMES,
  TRACE_MIDI_LATENCY_NAMES,
  TRACE_NAMES,
  TRACE_PHASES,
  TRACE_STORAGE_SYNC_FLAGS,
  TRACE_THREADS,
  validateTraceRecord,
} from './registry.js'

const pid = 1

export function toChromeTrace(records, metadata = {}) {
  const ordered = [...records].sort((left, right) => left.sequence - right.sequence)
  const threadIds = [...new Set(ordered.map((record) => validateTraceRecord(record).thread))].sort((a, b) => a - b)
  const traceEvents = [
    { name: 'process_name', ph: 'M', pid, tid: 0, args: { name: 'NullPerator WASM' } },
    ...threadIds.map((thread) => ({ name: 'thread_name', ph: 'M', pid, tid: thread, args: { name: TRACE_THREADS[thread] } })),
  ]
  for (const record of ordered) {
    const phase = TRACE_PHASES[record.phase]
    const event = {
      name: TRACE_NAMES[record.name], cat: TRACE_CATEGORIES[record.category], phase,
      ph: phase, ts: Math.trunc(record.timestampUs), pid, tid: record.thread,
      // TraceRecord's numeric payload is meaningful for every phase. In
      // particular benchmark B/E records carry their block index, and instant
      // MIDI events carry a byte count. Chrome accepts args on B/E/i/C events.
      args: {
        value: record.value,
        sequence: record.sequence,
        generation: record.generation,
        flags: record.flags,
      },
    }
    delete event.phase
    if (phase === 'i') event.s = 't'
    if (record.name === 23) {
      event.args.syncId = record.value
      event.args.populate = (record.flags & TRACE_STORAGE_SYNC_FLAGS.Populate) !== 0
      if (phase === 'E') {
        event.args.outcome = (record.flags & TRACE_STORAGE_SYNC_FLAGS.Success) !== 0
          ? 'success' : 'failure'
      }
    }
    if (record.name === 24 || record.name === 25) {
      event.args.correlation = record.value
      event.args.action = record.flags & TRACE_INPUT_ACTION_MASK
    } else if (record.name === TRACE_INPUT_TO_FRAME_LATENCY_NAME) {
      event.args.latencyUs = record.value
      event.args.correlation = record.flags
    } else if (record.name === 27) {
      event.args.correlation = record.value
      event.args.action = record.flags & TRACE_INPUT_ACTION_MASK
      event.args.reason = (record.flags & TRACE_INPUT_DROP_FLAGS.Overflow) !== 0
        ? 'overflow'
        : (record.flags & TRACE_INPUT_DROP_FLAGS.NoPresentation) !== 0
            ? 'no-presentation'
            : (record.flags & TRACE_INPUT_DROP_FLAGS.Coalesced) !== 0
                ? 'coalesced'
                : 'unknown'
    } else if (TRACE_MIDI_INSTANT_NAMES.includes(record.name)) {
      event.args.correlation = record.value
    } else if (TRACE_MIDI_LATENCY_NAMES.includes(record.name)) {
      event.args.latencyUs = record.value
      event.args.correlation = record.flags
    }
    traceEvents.push(event)
  }
  // Sequence order is the publication order, not necessarily timestamp order
  // across application/browser producers. Derive the time range independently.
  let recordStartUs = 0
  let recordEndUs = 0
  if (ordered.length) {
    recordStartUs = ordered[0].timestampUs
    recordEndUs = ordered[0].timestampUs
    for (const record of ordered) {
      recordStartUs = Math.min(recordStartUs, record.timestampUs)
      recordEndUs = Math.max(recordEndUs, record.timestampUs)
    }
  }
  return {
    traceEvents,
    displayTimeUnit: 'ms',
    metadata: {
      version: 1,
      ...metadata,
      recordCount: ordered.length,
      recordStartUs,
      recordEndUs,
      recordDurationUs: Math.max(0, recordEndUs - recordStartUs),
      recordTimeUnit: 'us',
      captureTimeUnit: 'ms',
    },
  }
}

const nearestRank = (sorted, percentile) => sorted.length
  ? sorted[Math.max(0, Math.ceil(percentile * sorted.length) - 1)] : 0

export function summarizeTrace(records) {
  const stacks = new Map()
  const durations = new Map()
  const outcomes = new Map()
  for (const record of [...records].sort((a, b) => a.sequence - b.sequence)) {
    validateTraceRecord(record)
    if (record.phase === 3 &&
        (record.name === TRACE_INPUT_TO_FRAME_LATENCY_NAME ||
         TRACE_MIDI_LATENCY_NAMES.includes(record.name))) {
      const key = `${record.thread}:${record.category}:${record.name}`
      const samples = durations.get(key) ?? []
      samples.push(record.value)
      durations.set(key, samples)
      continue
    }
    if (record.phase !== 0 && record.phase !== 1) continue
    const key = `${record.thread}:${record.category}:${record.name}`
    // Non-zero values are correlation IDs for paired browser/native work
    // such as IDBFS syncs and benchmark blocks. Keep pairing precise while
    // still aggregating every ID under one scope summary.
    const pairKey = `${key}:${record.value || 0}`
    if (record.phase === 0) {
      const stack = stacks.get(pairKey) ?? []
      stack.push(record.timestampUs)
      stacks.set(pairKey, stack)
    } else {
      const stack = stacks.get(pairKey)
      if (!stack?.length) continue
      const start = stack.pop()
      const duration = Math.max(0, record.timestampUs - start)
      const samples = durations.get(key) ?? []
      samples.push(duration)
      durations.set(key, samples)
      if (record.name === 23) {
        const result = outcomes.get(key) ?? { successCount: 0, failureCount: 0 }
        if ((record.flags & TRACE_STORAGE_SYNC_FLAGS.Success) !== 0) result.successCount += 1
        else result.failureCount += 1
        outcomes.set(key, result)
      }
    }
  }
  return [...durations.entries()].map(([key, samples]) => {
    samples.sort((a, b) => a - b)
    const [thread, category, name] = key.split(':').map(Number)
    return Object.freeze({
      thread, threadName: TRACE_THREADS[thread], category, categoryName: TRACE_CATEGORIES[category],
      name, nameText: TRACE_NAMES[name], count: samples.length,
      p50Us: nearestRank(samples, 0.5), p95Us: nearestRank(samples, 0.95),
      p99Us: nearestRank(samples, 0.99), maxUs: samples.at(-1) ?? 0,
      totalUs: samples.reduce((total, value) => total + value, 0),
      successCount: outcomes.get(key)?.successCount ?? 0,
      failureCount: outcomes.get(key)?.failureCount ?? 0,
    })
  }).sort((left, right) => right.totalUs - left.totalUs || left.name - right.name)
}
