import { describe, expect, it } from 'vitest'

import {
  compareManifests,
  createManifest,
  hashBytesIncrementally,
  hashFileIncrementally,
  validateHostRelativePath,
} from '../src/storage/syncManifest.js'

const bytes = (text) => new TextEncoder().encode(text)

describe('host-folder sync manifests', () => {
  it('hashes incrementally and produces a deterministic path/kind/size map', async () => {
    const chunks = []
    const hash = await hashBytesIncrementally(bytes('abcdefgh'), {
      chunkBytes: 3,
      onChunk: (size) => chunks.push(size),
    })
    const manifest = createManifest([
      { path: 'samples/kick.wav', kind: 'file', size: 8, hash },
      { path: 'projects', kind: 'directory', size: 0 },
    ])

    expect(chunks).toEqual([3, 3, 2])
    expect([...manifest.keys()]).toEqual(['projects', 'samples/kick.wav'])
    expect(manifest.get('samples/kick.wav')).toMatchObject({ kind: 'file', size: 8, hash })
  })

  it('rejects a zero or invalid chunk size before hashing a host file', async () => {
    const file = new Blob([bytes('abc')])
    await expect(hashFileIncrementally(file, { chunkBytes: 0 })).rejects.toThrow(/chunk size/i)
    await expect(hashFileIncrementally(file, { chunkBytes: 1.5 })).rejects.toThrow(/chunk size/i)
  })

  it('rejects unsafe host-relative paths before walking or applying them', () => {
    for (const path of ['', '.', '..', '../escape', 'a/../b', '/absolute', 'C:drive', '\\server', 'a\\b', 'a\0b']) {
      expect(() => validateHostRelativePath(path)).toThrow(/unsafe|invalid/i)
    }
    expect(validateHostRelativePath('projects/pico/demo.dat')).toBe('projects/pico/demo.dat')
  })

  it('does not treat similarly-prefixed top-level names as ancestors', () => {
    expect(() => createManifest([
      { path: 'swap', kind: 'file', size: 1, hash: 'a' },
      { path: 'swap (browser)', kind: 'directory', size: 0 },
      { path: 'swap (browser)/inside.dat', kind: 'file', size: 1, hash: 'b' },
    ])).not.toThrow()
  })

  it('classifies browser-only and host-only changes plus equal convergence', () => {
    const base = createManifest([{ path: 'same.dat', kind: 'file', size: 3, hash: 'old' }])
    const browser = createManifest([
      { path: 'same.dat', kind: 'file', size: 3, hash: 'browser' },
      { path: 'browser.dat', kind: 'file', size: 1, hash: 'b' },
    ])
    const host = createManifest([
      { path: 'same.dat', kind: 'file', size: 3, hash: 'browser' },
      { path: 'host.dat', kind: 'file', size: 1, hash: 'h' },
    ])

    const diff = compareManifests(base, browser, host)

    expect(diff.converged.map(({ path }) => path)).toEqual(['same.dat'])
    expect(diff.push.map(({ path }) => path)).toEqual(['browser.dat'])
    expect(diff.pull.map(({ path }) => path)).toEqual(['host.dat'])
    expect(diff.conflicts).toEqual([])
  })

  it('reports modify-vs-modify and delete-vs-modify conflicts', () => {
    const base = createManifest([
      { path: 'samples/kick.wav', kind: 'file', size: 1, hash: 'old' },
      { path: 'projects/demo.dat', kind: 'file', size: 1, hash: 'old' },
    ])
    const browser = createManifest([{ path: 'samples/kick.wav', kind: 'file', size: 1, hash: 'browser' }])
    const host = createManifest([
      { path: 'samples/kick.wav', kind: 'file', size: 1, hash: 'host' },
      { path: 'projects/demo.dat', kind: 'file', size: 1, hash: 'host' },
    ])

    const diff = compareManifests(base, browser, host)

    expect(diff.conflicts.map(({ path, reason }) => ({ path, reason }))).toEqual([
      { path: 'projects/demo.dat', reason: 'delete-vs-modify' },
      { path: 'samples/kick.wav', reason: 'both-modified' },
    ])
  })

  it('reports parent deletion versus a new descendant as a structural conflict in both directions', () => {
    const base = createManifest([{ path: 'project', kind: 'directory', size: 0 }])
    const hostAddsChild = compareManifests(
      base,
      createManifest(),
      createManifest([
        { path: 'project', kind: 'directory', size: 0 },
        { path: 'project/new.dat', kind: 'file', size: 1, hash: 'host' },
      ]),
    )
    const browserAddsChild = compareManifests(
      base,
      createManifest([
        { path: 'project', kind: 'directory', size: 0 },
        { path: 'project/new.dat', kind: 'file', size: 1, hash: 'browser' },
      ]),
      createManifest(),
    )

    expect(hostAddsChild.conflicts).toEqual([
      expect.objectContaining({ path: 'project', reason: 'ancestor-delete-vs-descendant-change' }),
    ])
    expect(browserAddsChild.conflicts).toEqual([
      expect.objectContaining({ path: 'project', reason: 'ancestor-delete-vs-descendant-change' }),
    ])
  })

})
