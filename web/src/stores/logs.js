const SEVERITIES = Object.freeze(['debug', 'info', 'warn', 'error'])
const SEVERITY_RANK = Object.freeze(Object.fromEntries(SEVERITIES.map((value, index) => [value, index])))
const DEFAULT_FILTER = Object.freeze({ minimumSeverity: 'debug', category: '', thread: '', text: '' })

const clampCapacity = (value) => Math.min(10_000, Math.max(1, Number.isFinite(value) ? Math.trunc(value) : 1_000))
const finiteNumber = (value, fallback = 0) => Number.isFinite(value) ? value : fallback
const cleanText = (value, fallback) => String(value ?? fallback)

function normalizeRecord(record, sequence) {
  const severity = SEVERITY_RANK[record?.severity] === undefined ? 'info' : record.severity
  return Object.freeze({
    sequence,
    sourceSequence: Number.isSafeInteger(record?.sequence) ? record.sequence : null,
    monotonicUs: Math.max(0, Math.trunc(finiteNumber(record?.monotonicUs))),
    wallTime: finiteNumber(record?.wallTime),
    severity,
    category: cleanText(record?.category, 'GENERAL'),
    thread: cleanText(record?.thread, 'unknown'),
    message: cleanText(record?.message, ''),
    repeat: Math.max(1, Math.trunc(finiteNumber(record?.repeat, 1))),
    truncated: Boolean(record?.truncated),
  })
}

function sameMessage(left, right) {
  return left.severity === right.severity && left.category === right.category &&
    left.thread === right.thread && left.message === right.message &&
    left.truncated === right.truncated
}

function matches(record, filter) {
  if (SEVERITY_RANK[record.severity] < SEVERITY_RANK[filter.minimumSeverity]) return false
  if (filter.category && record.category !== filter.category) return false
  if (filter.thread && record.thread !== filter.thread) return false
  const text = filter.text.trim().toLocaleLowerCase()
  return !text || `${record.category}\n${record.thread}\n${record.message}`.toLocaleLowerCase().includes(text)
}

export function createLogStore(options = {}) {
  const capacity = clampCapacity(options.capacity ?? 1_000)
  const coalesceWindowUs = Math.max(0, finiteNumber(options.coalesceWindowUs, 1_000_000))
  const clipboard = options.clipboard ?? globalThis.navigator?.clipboard
  const document = options.document ?? globalThis.document
  const url = options.url ?? globalThis.URL
  const BlobCtor = options.Blob ?? globalThis.Blob
  const listeners = new Set()
  const ring = new Array(capacity)
  let head = 0
  let size = 0
  let dropped = 0
  let nextSequence = 1
  let paused = false
  let filter = DEFAULT_FILTER
  let snapshot

  const retainedRecords = () => Array.from({ length: size }, (_, index) => ring[(head + index) % capacity])
  const buildSnapshot = () => {
    const retained = retainedRecords()
    return Object.freeze({
      records: Object.freeze(retained.filter((record) => matches(record, filter))),
      categories: Object.freeze([...new Set(retained.map((record) => record.category))].sort()),
      threads: Object.freeze([...new Set(retained.map((record) => record.thread))].sort()),
      retained: size,
      capacity,
      dropped,
      paused,
      filter,
    })
  }
  const publish = (force = false) => {
    if (paused && !force) return
    snapshot = buildSnapshot()
    for (const listener of listeners) listener(snapshot)
  }
  const exportText = () => {
    const header = { type: 'picotracker-log-export', version: 1, dropped: snapshot.dropped, filter: snapshot.filter }
    return [JSON.stringify(header), ...snapshot.records.map((record) => JSON.stringify(record))].join('\n') + '\n'
  }
  const appendOne = (value) => {
    const record = normalizeRecord(value, nextSequence++)
    if (size > 0) {
      const index = (head + size - 1) % capacity
      const previous = ring[index]
      const elapsed = record.monotonicUs - previous.monotonicUs
      if (sameMessage(previous, record) && elapsed >= 0 && elapsed <= coalesceWindowUs) {
        ring[index] = Object.freeze({
          ...record,
          sequence: previous.sequence,
          repeat: previous.repeat + record.repeat,
        })
        return ring[index]
      }
    }
    if (size === capacity) {
      ring[head] = record
      head = (head + 1) % capacity
      dropped += 1
    } else {
      ring[(head + size) % capacity] = record
      size += 1
    }
    return record
  }

  snapshot = buildSnapshot()
  return Object.freeze({
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    snapshot: () => snapshot,
    appendLog(value) {
      const record = appendOne(value)
      publish()
      return record
    },
    appendLogs(values) {
      let count = 0
      for (const value of values ?? []) { appendOne(value); count += 1 }
      if (count) publish()
      return count
    },
    addDropped(count) {
      const value = Math.max(0, Math.trunc(finiteNumber(count)))
      dropped = Math.min(Number.MAX_SAFE_INTEGER, dropped + value)
      publish()
    },
    setLogFilter(next = {}) {
      const minimumSeverity = SEVERITY_RANK[next.minimumSeverity] === undefined
        ? filter.minimumSeverity : next.minimumSeverity
      filter = Object.freeze({
        minimumSeverity,
        category: cleanText(next.category, filter.category),
        thread: cleanText(next.thread, filter.thread),
        text: cleanText(next.text, filter.text),
      })
      publish(true)
    },
    setPaused(value) {
      const next = Boolean(value)
      if (next === paused) return
      paused = next
      publish(true)
    },
    clearLogs() {
      ring.fill(undefined)
      head = 0
      size = 0
      dropped = 0
      publish(true)
    },
    exportLogs: exportText,
    async copyLogs() {
      if (typeof clipboard?.writeText !== 'function') throw new Error('Clipboard is unavailable')
      const text = exportText()
      await clipboard.writeText(text)
      return text
    },
    downloadLogs(filename = 'picotracker-logs.jsonl') {
      if (!document?.createElement || !url?.createObjectURL || !BlobCtor) throw new Error('Log download is unavailable')
      const blob = new BlobCtor([exportText()], { type: 'application/x-ndjson;charset=utf-8' })
      const objectUrl = url.createObjectURL(blob)
      try {
        const anchor = document.createElement('a')
        anchor.href = objectUrl
        anchor.download = filename
        anchor.click()
      } finally {
        url.revokeObjectURL(objectUrl)
      }
    },
  })
}
