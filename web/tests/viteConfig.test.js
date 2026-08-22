import { describe, expect, it } from 'vitest'

import config from '../vite.config.js'

describe('Vite isolation headers', () => {
  it('enables cross-origin isolation in development and preview', () => {
    const expectedHeaders = {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    }

    expect(config.server?.headers).toEqual(expectedHeaders)
    expect(config.preview?.headers).toEqual(expectedHeaders)
  })
})
