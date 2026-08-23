import { describe, expect, it } from 'vitest'

import { parseBuildMetadata } from '../src/buildMetadata.js'

describe('parseBuildMetadata', () => {
  it('normalizes a complete WASM build identity', () => {
    expect(
      parseBuildMetadata({
        commit: 'abc12345',
        dirty: false,
        builtAt: '2026-08-22T00:00:00Z',
        emscripten: '6.0.5',
      }),
    ).toEqual({
      commit: 'abc12345',
      dirty: false,
      builtAt: '2026-08-22T00:00:00Z',
      emscripten: '6.0.5',
    })
  })

  it('normalizes malformed and missing values', () => {
    expect(parseBuildMetadata({ commit: 42, dirty: 'yes' })).toEqual({
      commit: '42',
      dirty: true,
      builtAt: 'unknown',
      emscripten: 'unknown',
    })
    expect(parseBuildMetadata(null)).toEqual({
      commit: 'unknown',
      dirty: false,
      builtAt: 'unknown',
      emscripten: 'unknown',
    })
  })
})
