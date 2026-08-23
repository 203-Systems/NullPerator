import { describe, expect, it } from 'vitest'

import { createManifest } from '../src/storage/syncManifest.js'
import { createSyncCoordinator } from '../src/storage/syncCoordinator.js'

const entry = (path, hash) => ({ path, kind: 'file', size: 1, hash })

function fakeEndpoint(entries) {
  const state = { manifest: createManifest(entries), applied: [] }
  return {
    manifest: async () => state.manifest,
    apply: async (operations, source) => {
      state.applied.push(...operations)
      const next = new Map(state.manifest)
      const sourceManifest = await source.manifest()
      for (const operation of operations) {
        if (operation.type === 'delete') {
          for (const path of [...next.keys()]) if (path === operation.path || path.startsWith(`${operation.path}/`)) next.delete(path)
        }
        else {
          if (operation.source?.kind === 'file' && operation.target?.kind === 'directory') {
            for (const path of [...next.keys()]) if (path.startsWith(`${operation.path}/`)) next.delete(path)
          }
          const parentSlash = operation.path.lastIndexOf('/')
          let parent = parentSlash < 0 ? '' : operation.path.slice(0, parentSlash)
          while (parent) {
            if (!next.has(parent)) next.set(parent, { path: parent, kind: 'directory', size: 0, hash: null })
            const slash = parent.lastIndexOf('/')
            parent = slash < 0 ? '' : parent.slice(0, slash)
          }
          next.set(operation.path, { ...sourceManifest.get(operation.source.path), path: operation.path })
        }
      }
      state.manifest = createManifest([...next.values()])
    },
    state,
  }
}

describe('host-folder sync coordinator', () => {
  it('pushes and pulls non-conflicting changes in ordered serialized runs', async () => {
    const browser = fakeEndpoint([entry('browser.dat', 'browser')])
    const host = fakeEndpoint([entry('host.dat', 'host')])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => createManifest(), saveBase: async () => {} })

    await coordinator.syncHostFolder('bidirectional')

    expect(host.state.applied).toEqual([expect.objectContaining({ type: 'write', path: 'browser.dat' })])
    expect(browser.state.applied).toEqual([expect.objectContaining({ type: 'write', path: 'host.dat' })])
  })

  it('stops at a three-way conflict without advancing the base manifest', async () => {
    const base = createManifest([entry('samples/kick.wav', 'old')])
    const browser = fakeEndpoint([entry('samples/kick.wav', 'browser')])
    const host = fakeEndpoint([entry('samples/kick.wav', 'host')])
    const saves = []
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async (next) => saves.push(next) })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    expect(coordinator.snapshot().conflicts).toEqual([expect.objectContaining({ path: 'samples/kick.wav' })])
    expect(saves).toEqual([])
  })

  it('waits for active work before unmounting after conflict resolution', async () => {
    const base = createManifest([entry('demo.dat', 'old')])
    const browser = fakeEndpoint([entry('demo.dat', 'browser')])
    const host = fakeEndpoint([entry('demo.dat', 'host')])
    let release
    const applyHost = host.apply
    host.apply = async (...args) => {
      await new Promise((resolve) => { release = resolve })
      return applyHost(...args)
    }
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    const resolving = coordinator.resolveConflict('demo.dat', 'keep-browser')
    const unmounting = coordinator.unmountHostFolder()
    await Promise.resolve()
    expect(coordinator.snapshot().state).toBe('syncing')
    release()
    await resolving
    await unmounting
    expect(coordinator.snapshot().state).toBe('unmounted')
  })

  it('keeps both file variants as deterministic common siblings before advancing base', async () => {
    const base = createManifest([entry('demo.dat', 'old')])
    const browser = fakeEndpoint([entry('demo.dat', 'browser')])
    const host = fakeEndpoint([entry('demo.dat', 'host')])
    const saves = []
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async (next) => saves.push(next) })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    await coordinator.resolveConflict('demo.dat', 'keep-both')

    expect([...browser.state.manifest.keys()]).toEqual(['demo (browser).dat', 'demo (host).dat'])
    expect([...host.state.manifest.keys()]).toEqual(['demo (browser).dat', 'demo (host).dat'])
    expect(browser.state.manifest.get('demo (browser).dat').hash).toBe('browser')
    expect(browser.state.manifest.get('demo (host).dat').hash).toBe('host')
    expect(saves).toHaveLength(1)
  })

  it('keeps both sides of a file-versus-directory conflict as complete sibling trees', async () => {
    const base = createManifest([entry('swap', 'old')])
    const browser = fakeEndpoint([
      { path: 'swap', kind: 'directory', size: 0 },
      entry('swap/inside.dat', 'browser-child'),
    ])
    const host = fakeEndpoint([entry('swap', 'host-file')])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    await coordinator.resolveConflict('swap', 'keep-both')

    const expected = ['swap (browser)', 'swap (browser)/inside.dat', 'swap (host)']
    expect([...browser.state.manifest.keys()]).toEqual(expected)
    expect([...host.state.manifest.keys()]).toEqual(expected)
    expect(browser.state.manifest.get('swap (host)').hash).toBe('host-file')
  })

  it('rebuilds the complete browser subtree when resolving a parent kind conflict', async () => {
    const base = createManifest([entry('swap', 'old')])
    const browser = fakeEndpoint([
      { path: 'swap', kind: 'directory', size: 0 },
      entry('swap/inside.dat', 'browser-child'),
    ])
    const host = fakeEndpoint([entry('swap', 'host-file')])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    await coordinator.resolveConflict('swap', 'keep-browser')

    expect([...browser.state.manifest.keys()]).toEqual(['swap', 'swap/inside.dat'])
    expect([...host.state.manifest.keys()]).toEqual(['swap', 'swap/inside.dat'])
    expect(host.state.manifest.get('swap/inside.dat').hash).toBe('browser-child')
  })

  it('rebuilds the complete host subtree when resolving a parent kind conflict', async () => {
    const base = createManifest([entry('swap', 'old')])
    const browser = fakeEndpoint([entry('swap', 'browser-file')])
    const host = fakeEndpoint([
      { path: 'swap', kind: 'directory', size: 0 },
      entry('swap/inside.dat', 'host-child'),
    ])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    await coordinator.resolveConflict('swap', 'keep-host')

    expect([...browser.state.manifest.keys()]).toEqual(['swap', 'swap/inside.dat'])
    expect([...host.state.manifest.keys()]).toEqual(['swap', 'swap/inside.dat'])
    expect(browser.state.manifest.get('swap/inside.dat').hash).toBe('host-child')
  })

  it('expands keep-host to a file ancestor that would otherwise block the winning child', async () => {
    const base = createManifest([
      { path: 'swap', kind: 'directory', size: 0 },
      entry('swap/inside.dat', 'old'),
    ])
    const browser = fakeEndpoint([entry('swap', 'browser-file')])
    const host = fakeEndpoint([
      { path: 'swap', kind: 'directory', size: 0 },
      entry('swap/inside.dat', 'host-child'),
    ])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    await coordinator.resolveConflict('swap/inside.dat', 'keep-host')

    expect([...browser.state.manifest.keys()]).toEqual(['swap', 'swap/inside.dat'])
    expect(browser.state.manifest.get('swap/inside.dat').hash).toBe('host-child')
  })

  it('expands keep-browser to a file ancestor that would otherwise block the winning child', async () => {
    const base = createManifest([
      { path: 'swap', kind: 'directory', size: 0 },
      entry('swap/inside.dat', 'old'),
    ])
    const browser = fakeEndpoint([
      { path: 'swap', kind: 'directory', size: 0 },
      entry('swap/inside.dat', 'browser-child'),
    ])
    const host = fakeEndpoint([entry('swap', 'host-file')])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    await coordinator.resolveConflict('swap/inside.dat', 'keep-browser')

    expect([...host.state.manifest.keys()]).toEqual(['swap', 'swap/inside.dat'])
    expect(host.state.manifest.get('swap/inside.dat').hash).toBe('browser-child')
  })

  it('applies non-conflicting changes after the last conflict is resolved', async () => {
    const base = createManifest([entry('demo.dat', 'old')])
    const browser = fakeEndpoint([entry('browser.dat', 'browser'), entry('demo.dat', 'browser')])
    const host = fakeEndpoint([entry('demo.dat', 'host'), entry('host.dat', 'host')])
    const saves = []
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async (next) => saves.push(next) })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    await coordinator.resolveConflict('demo.dat', 'keep-browser')

    expect([...browser.state.manifest.keys()]).toEqual(['browser.dat', 'demo.dat', 'host.dat'])
    expect([...host.state.manifest.keys()]).toEqual(['browser.dat', 'demo.dat', 'host.dat'])
    expect(saves).toHaveLength(1)
  })

  it('keeps the modified side under a sibling when the other side deleted the original', async () => {
    const base = createManifest([entry('demo.dat', 'old')])
    const browser = fakeEndpoint([])
    const host = fakeEndpoint([entry('demo.dat', 'host')])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    await coordinator.resolveConflict('demo.dat', 'keep-both')

    expect([...browser.state.manifest.keys()]).toEqual(['demo (host).dat'])
    expect([...host.state.manifest.keys()]).toEqual(['demo (host).dat'])
    expect(host.state.manifest.get('demo (host).dat').hash).toBe('host')
  })

  it('does not apply an ancestor deletion after keeping a modified descendant', async () => {
    const base = createManifest([
      { path: 'project', kind: 'directory', size: 0 },
      entry('project/song.dat', 'old'),
    ])
    const browser = fakeEndpoint([])
    const host = fakeEndpoint([
      { path: 'project', kind: 'directory', size: 0 },
      entry('project/song.dat', 'host'),
    ])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    await coordinator.resolveConflict('project/song.dat', 'keep-host')

    expect([...browser.state.manifest.keys()]).toEqual(['project', 'project/song.dat'])
    expect([...host.state.manifest.keys()]).toEqual(['project', 'project/song.dat'])
    expect(browser.state.manifest.get('project/song.dat').hash).toBe('host')
  })

  it('does not delete a newly-added host descendant when the browser deleted its parent', async () => {
    const base = createManifest([{ path: 'project', kind: 'directory', size: 0 }])
    const browser = fakeEndpoint([])
    const host = fakeEndpoint([
      { path: 'project', kind: 'directory', size: 0 },
      entry('project/new.dat', 'host-new'),
    ])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    expect([...host.state.manifest.keys()]).toEqual(['project', 'project/new.dat'])
    await coordinator.resolveConflict('project', 'keep-host')

    expect([...browser.state.manifest.keys()]).toEqual(['project', 'project/new.dat'])
    expect([...host.state.manifest.keys()]).toEqual(['project', 'project/new.dat'])
  })

  it('does not delete a newly-added browser descendant when the host deleted its parent', async () => {
    const base = createManifest([{ path: 'project', kind: 'directory', size: 0 }])
    const browser = fakeEndpoint([
      { path: 'project', kind: 'directory', size: 0 },
      entry('project/new.dat', 'browser-new'),
    ])
    const host = fakeEndpoint([])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async () => {} })

    await expect(coordinator.syncHostFolder('bidirectional')).rejects.toThrow(/conflict/i)
    expect([...browser.state.manifest.keys()]).toEqual(['project', 'project/new.dat'])
    await coordinator.resolveConflict('project', 'keep-browser')

    expect([...browser.state.manifest.keys()]).toEqual(['project', 'project/new.dat'])
    expect([...host.state.manifest.keys()]).toEqual(['project', 'project/new.dat'])
  })

  it('treats deletion as a first-class push and preserves base when apply is interrupted', async () => {
    const base = createManifest([entry('obsolete.dat', 'old')])
    const browser = fakeEndpoint([])
    const host = fakeEndpoint([entry('obsolete.dat', 'old')])
    const saves = []
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => base, saveBase: async (next) => saves.push(next) })

    await coordinator.syncHostFolder('bidirectional')
    expect(host.state.applied).toEqual([expect.objectContaining({ type: 'delete', path: 'obsolete.dat' })])
    expect(saves).toHaveLength(1)

    const failedHost = fakeEndpoint([entry('obsolete.dat', 'old')])
    failedHost.apply = async () => { throw new Error('write interrupted') }
    const interrupted = createSyncCoordinator({ browser: fakeEndpoint([]), host: failedHost, loadBase: async () => base, saveBase: async (next) => saves.push(next) })
    await expect(interrupted.syncHostFolder('bidirectional')).rejects.toThrow('write interrupted')
    expect(saves).toHaveLength(1)
  })

  it('makes explicit Pull and Push authoritative in their selected direction', async () => {
    const browser = fakeEndpoint([entry('browser.dat', 'browser')])
    const host = fakeEndpoint([entry('host.dat', 'host')])
    const pull = createSyncCoordinator({ browser, host, loadBase: async () => createManifest(), saveBase: async () => {} })

    await pull.syncHostFolder('pull')
    expect([...browser.state.manifest.keys()]).toEqual(['host.dat'])

    const pushingBrowser = fakeEndpoint([entry('browser.dat', 'browser')])
    const pushingHost = fakeEndpoint([entry('host.dat', 'host')])
    const push = createSyncCoordinator({ browser: pushingBrowser, host: pushingHost, loadBase: async () => createManifest(), saveBase: async () => {} })
    await push.syncHostFolder('push')
    expect([...pushingHost.state.manifest.keys()]).toEqual(['browser.dat'])
  })

  it('omits stale child deletes when a directory is replaced by a file', async () => {
    const browser = fakeEndpoint([entry('swap', 'new-file')])
    const host = fakeEndpoint([
      { path: 'swap', kind: 'directory', size: 0 },
      entry('swap/old.dat', 'old-child'),
    ])
    const coordinator = createSyncCoordinator({ browser, host, loadBase: async () => createManifest(), saveBase: async () => {} })

    await coordinator.syncHostFolder('push')

    expect(host.state.applied).toEqual([expect.objectContaining({ type: 'write', path: 'swap', source: expect.objectContaining({ kind: 'file' }) })])
    expect([...host.state.manifest.keys()]).toEqual(['swap'])
  })
})
