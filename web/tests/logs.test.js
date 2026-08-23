import { describe, expect, it, vi } from 'vitest'

import { createLogStore } from '../src/stores/logs.js'
import { createLogsHandle } from '../src/handles/logs.js'

const makeLog = (index, overrides = {}) => ({
  monotonicUs: index * 1_000_001,
  wallTime: 1_700_000_000_000 + index,
  severity: 'info', category: 'TEST', thread: 'application', message: `message ${index}`,
  ...overrides,
})

describe('bounded structured logs', () => {
  it('bounds retained logs with a ring and reports overwritten records', () => {
    const logs = createLogStore({ capacity: 3 })
    for (let index = 0; index < 5; index += 1) logs.appendLog(makeLog(index))
    expect(logs.snapshot()).toMatchObject({ retained: 3, capacity: 3, dropped: 2 })
    expect(logs.snapshot().records.map((record) => record.message)).toEqual(['message 2', 'message 3', 'message 4'])
  })

  it('coalesces only consecutive identical high-rate records', () => {
    const logs = createLogStore({ capacity: 4, coalesceWindowUs: 100 })
    logs.appendLog(makeLog(0, { monotonicUs: 10, message: 'tick' }))
    logs.appendLog(makeLog(1, { monotonicUs: 20, message: 'tick' }))
    logs.appendLog(makeLog(2, { monotonicUs: 200, message: 'tick' }))
    expect(logs.snapshot().records.map(({ repeat, monotonicUs }) => ({ repeat, monotonicUs }))).toEqual([
      { repeat: 2, monotonicUs: 20 }, { repeat: 1, monotonicUs: 200 },
    ])
  })

  it('keeps accepting bounded records while presentation is paused', () => {
    const logs = createLogStore({ capacity: 2 })
    logs.appendLog(makeLog(0))
    logs.setPaused(true)
    logs.appendLog(makeLog(1))
    logs.appendLog(makeLog(2))
    expect(logs.snapshot()).toMatchObject({ paused: true, retained: 1, dropped: 0 })
    logs.setPaused(false)
    expect(logs.snapshot()).toMatchObject({ paused: false, retained: 2, dropped: 1 })
    expect(logs.snapshot().records.map((record) => record.message)).toEqual(['message 1', 'message 2'])
  })

  it('combines severity, category, thread, and case-insensitive text filters', () => {
    const logs = createLogStore()
    logs.appendLog(makeLog(0, { severity: 'debug', category: 'AUDIO', message: 'Callback ready' }))
    logs.appendLog(makeLog(1, { severity: 'error', category: 'AUDIO', message: 'Buffer underrun' }))
    logs.appendLog(makeLog(2, { severity: 'error', category: 'FILES', message: 'Write failed' }))
    logs.setLogFilter({ minimumSeverity: 'warn', category: 'AUDIO', thread: 'application', text: 'UNDERRUN' })
    expect(logs.snapshot().records.map((record) => record.message)).toEqual(['Buffer underrun'])
  })

  it('exports deterministic filtered JSONL, copies, downloads, revokes, and clears', async () => {
    const clipboard = { writeText: vi.fn(async () => {}) }
    const anchor = { click: vi.fn(), href: '', download: '' }
    const document = { createElement: vi.fn(() => anchor) }
    const url = { createObjectURL: vi.fn(() => 'blob:logs'), revokeObjectURL: vi.fn() }
    class FakeBlob { constructor(parts, options) { this.parts = parts; this.options = options } }
    const logs = createLogStore({ capacity: 2, clipboard, document, url, Blob: FakeBlob })
    logs.appendLog(makeLog(0, { severity: 'debug' }))
    logs.appendLog(makeLog(1, { severity: 'error' }))
    logs.setLogFilter({ minimumSeverity: 'error' })
    const first = logs.exportLogs()
    expect(logs.exportLogs()).toBe(first)
    expect(first).toContain('message 1')
    expect(first).not.toContain('message 0')
    await expect(logs.copyLogs()).resolves.toBe(first)
    expect(clipboard.writeText).toHaveBeenCalledWith(first)
    logs.downloadLogs('session.jsonl')
    expect(anchor).toMatchObject({ href: 'blob:logs', download: 'session.jsonl' })
    expect(anchor.click).toHaveBeenCalledOnce()
    expect(url.revokeObjectURL).toHaveBeenCalledWith('blob:logs')
    logs.clearLogs()
    expect(logs.snapshot()).toMatchObject({ retained: 0, dropped: 0 })
  })

  it('validates and drains the fixed native ABI with wall-clock conversion and drops', () => {
    const heap = new Uint8Array(new ArrayBuffer(1_024))
    const view = new DataView(heap.buffer)
    const pointer = 64
    view.setUint32(pointer, 1, true)
    view.setUint32(pointer + 4, 32, true)
    view.setUint32(pointer + 8, 320, true)
    view.setUint32(pointer + 12, 1, true)
    view.setBigUint64(pointer + 16, 2n, true)
    const record = pointer + 32
    view.setBigUint64(record, 7n, true)
    view.setBigUint64(record + 8, 12_500n, true)
    heap[record + 16] = 3
    heap[record + 17] = 1
    heap[record + 18] = 5
    heap[record + 19] = 11
    view.setUint16(record + 20, 4, true)
    heap.set(new TextEncoder().encode('FILES'), record + 24)
    heap.set(new TextEncoder().encode('application'), record + 48)
    heap.set(new TextEncoder().encode('oops'), record + 64)
    const logs = createLogStore()
    const handle = createLogsHandle({ HEAPU8: heap, _PicoTracker_Wasm_LogDrain: () => pointer }, logs, { timeOrigin: 1_000 })
    expect(handle.drainNow()).toBe(1)
    expect(logs.snapshot()).toMatchObject({ dropped: 2 })
    expect(logs.snapshot().records[0]).toMatchObject({
      sourceSequence: 7, monotonicUs: 12_500, wallTime: 1_012.5,
      severity: 'error', category: 'FILES', thread: 'application', message: 'oops', truncated: true,
    })
    view.setUint32(pointer, 99, true)
    expect(() => handle.drainNow()).toThrow(/incompatible/i)
  })
})
