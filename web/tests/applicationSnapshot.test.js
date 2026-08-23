import { describe, expect, it } from 'vitest'

import { readApplicationSnapshot } from '../src/handles/applicationSnapshot.js'

describe('application snapshot reader', () => {
  it('reads a coherent application model snapshot without a proxied export', () => {
    const memory = new SharedArrayBuffer(256)
    const words = new Uint32Array(memory)
    const pointer = 16
    const base = pointer >>> 2
    words.set([2, 1, 52, 164, 23, 1, 0x12345678, 8], base)
    const name = new TextEncoder().encode('oneCycAc')
    for (let index = 0; index < name.length; index += 1) {
      words[base + 8 + (index >>> 2)] |= name[index] << ((index & 3) * 8)
    }

    expect(readApplicationSnapshot({
      HEAPU32: words,
      __picoTrackerApplicationSnapshot: { data: pointer },
    })).toEqual({
      sequence: 2,
      projectName: 'oneCycAc',
      tempo: 164,
      sampleCount: 23,
      playerRunning: true,
      masterLevel: 0x12345678,
    })
  })

  it('rejects unavailable, incompatible, and malformed snapshots', () => {
    expect(() => readApplicationSnapshot({})).toThrow(/unavailable/i)

    const memory = new SharedArrayBuffer(256)
    const words = new Uint32Array(memory)
    const pointer = 16
    words.set([2, 2, 52, 0, 0, 0, 0, 0], pointer >>> 2)
    expect(() => readApplicationSnapshot({ HEAPU32: words, __picoTrackerApplicationSnapshot: { data: pointer } })).toThrow(/incompatible/i)

    words.set([4, 1, 52, 0, 0, 0, 0, 17], pointer >>> 2)
    expect(() => readApplicationSnapshot({ HEAPU32: words, __picoTrackerApplicationSnapshot: { data: pointer } })).toThrow(/project name/i)
  })
})
