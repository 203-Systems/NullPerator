import { afterEach, describe, expect, it, vi } from 'vitest'

import { restartWorkbench } from '../e2e/helpers/runtime.js'

afterEach(() => vi.unstubAllGlobals())

describe('browser acceptance restart fence', () => {
  const page = { evaluate: callback => callback() }

  it('waits until the entire restart promise completes', async () => {
    let finish
    const pending = new Promise(resolve => { finish = resolve })
    const restart = vi.fn(() => pending)
    vi.stubGlobal('__picoTrackerWorkbench', { restart })
    let completed = false
    const operation = restartWorkbench(page).then(() => { completed = true })
    await Promise.resolve()
    await Promise.resolve()
    expect(restart).toHaveBeenCalledOnce()
    expect(completed).toBe(false)
    finish()
    await operation
    expect(completed).toBe(true)
  })

  it('reports a rejected restart instead of leaving an unhandled promise', async () => {
    vi.stubGlobal('__picoTrackerWorkbench', {
      restart: async () => { throw new Error('C++ shutdown failed') },
    })
    await expect(restartWorkbench(page)).rejects.toThrow('C++ shutdown failed')
  })
})
