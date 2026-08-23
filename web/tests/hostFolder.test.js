import { describe, expect, it, vi } from 'vitest'

import { createHostFolderEndpoint, createHostFolderManager, ignoredNames, scanHostFolder } from '../src/storage/hostFolder.js'
import { createManifest, hashBytesIncrementally } from '../src/storage/syncManifest.js'

function file(name, text) {
  let blob = new Blob([text])
  return {
    kind: 'file', name,
    getFile: async () => blob,
    createWritable: async () => {
      const chunks = []
      return {
        write: async (chunk) => chunks.push(chunk),
        close: async () => { blob = new Blob(chunks) },
        abort: async () => { chunks.length = 0 },
      }
    },
  }
}

function directory(name, children = []) {
  const map = new Map(children.map((child) => [child.name, child]))
  return {
    kind: 'directory', name,
    async *entries() { yield * map.entries() },
    async getDirectoryHandle(child, { create = false } = {}) {
      if (!map.has(child) && create) map.set(child, directory(child))
      const result = map.get(child)
      if (!result || result.kind !== 'directory') throw new Error('NotFoundError')
      return result
    },
    async getFileHandle(child, { create = false } = {}) {
      if (!map.has(child) && create) {
        map.set(child, file(child, ''))
      }
      const result = map.get(child)
      if (!result || result.kind !== 'file') throw new Error('NotFoundError')
      return result
    },
    async removeEntry(child) { map.delete(child) },
  }
}

describe('host folder handles and manifests', () => {
  it('walks deterministic relative paths, ignores only .DS_Store, and hashes one file at a time', async () => {
    const root = directory('factory', [
      file('.DS_Store', 'ignored'),
      directory('projects', [file('demo.dat', 'PT')]),
      file('readme.txt', 'hello'),
    ])

    const progress = []
    const manifest = await scanHostFolder(root, { chunkBytes: 2, onProgress: (next) => progress.push(next) })

    expect(ignoredNames).toEqual(new Set(['.DS_Store']))
    expect([...manifest.keys()]).toEqual(['projects', 'projects/demo.dat', 'readme.txt'])
    expect(manifest.get('projects/demo.dat')).toMatchObject({ kind: 'file', size: 2 })
    expect(progress.at(-1)).toEqual({ entries: 3, bytes: 7 })
  })

  it('stops consuming a directory iterator as soon as the entry limit is exceeded', async () => {
    let yielded = 0
    const root = {
      kind: 'directory', name: 'huge',
      async *entries() {
        for (let index = 0; index < 100; index += 1) {
          yielded += 1
          yield [`file-${index}`, file(`file-${index}`, 'x')]
        }
      },
    }

    await expect(scanHostFolder(root, { limits: { maxEntries: 2 } })).rejects.toThrow(/entry limit/i)
    expect(yielded).toBe(3)
  })

  it('only requests permission from explicit mount, while reload restoration only queries it', async () => {
    const root = directory('chosen')
    root.queryPermission = vi.fn(async () => 'prompt')
    root.requestPermission = vi.fn(async () => 'granted')
    const metadata = { get: vi.fn(async () => root), put: vi.fn(async () => {}), delete: vi.fn(async () => {}) }
    const picker = vi.fn(async () => root)
    const mounted = createHostFolderManager({ picker, metadata })

    await mounted.mountHostFolder()
    expect(picker).toHaveBeenCalledWith({ mode: 'readwrite' })
    expect(root.requestPermission).toHaveBeenCalledTimes(1)
    expect(metadata.put).toHaveBeenCalledWith(root)

    root.queryPermission.mockResolvedValue('denied')
    const restored = createHostFolderManager({ picker, metadata })
    await restored.restoreHostFolderHandle()
    expect(restored.snapshot()).toMatchObject({ state: 'denied', permission: 'denied' })
    expect(root.requestPermission).toHaveBeenCalledTimes(1)
    await restored.unmountHostFolder()
    expect(metadata.delete).toHaveBeenCalledTimes(1)
  })

  it('reports permission loss before starting a mounted sync', async () => {
    const root = directory('chosen')
    root.queryPermission = vi.fn(async () => 'granted')
    const coordinator = {
      subscribe: () => () => {},
      syncHostFolder: vi.fn(async () => {}),
      waitForIdle: async () => {},
    }
    const manager = createHostFolderManager({
      picker: async () => root,
      metadata: { put: async () => {} },
      coordinator,
    })
    await manager.mountHostFolder()
    root.queryPermission.mockResolvedValue('denied')

    await expect(manager.syncHostFolder('bidirectional')).rejects.toThrow(/permission/i)
    expect(manager.snapshot()).toMatchObject({ state: 'denied', permission: 'denied' })
    expect(coordinator.syncHostFolder).not.toHaveBeenCalled()
  })

  it('rebinds synchronization and clears the old base when a different folder is selected', async () => {
    const first = directory('first')
    const second = directory('second')
    first.queryPermission = second.queryPermission = vi.fn(async () => 'granted')
    const picker = vi.fn().mockResolvedValueOnce(first).mockResolvedValueOnce(second)
    let base = createManifest()
    const metadata = {
      put: vi.fn(async () => {}),
      deleteBase: vi.fn(async () => { base = createManifest() }),
      getBase: vi.fn(async () => base),
      putBase: vi.fn(async (next) => { base = next }),
    }
    const browserManifest = createManifest([{ path: 'project.dat', kind: 'file', size: 2, hash: await hashBytesIncrementally(new TextEncoder().encode('PT')) }])
    const browser = {
      manifest: async () => browserManifest,
      apply: async () => {},
      copyFile: async (_path, sink) => sink.write(new TextEncoder().encode('PT')),
    }
    const manager = createHostFolderManager({ picker, metadata, browser })

    await manager.mountHostFolder()
    await manager.syncHostFolder('push')
    await manager.mountHostFolder()
    await manager.syncHostFolder('push')

    expect(await (await second.getFileHandle('project.dat')).getFile().then((value) => value.text())).toBe('PT')
    expect(metadata.deleteBase).toHaveBeenCalledTimes(2)
  })

  it('copies host writes in bounded chunks and closes before reporting progress', async () => {
    const root = directory('factory')
    const endpoint = createHostFolderEndpoint(root, { chunkBytes: 2 })
    const progress = []
    const source = {
      copyFile: async (_path, sink) => {
        await sink.write(new Uint8Array([0x50, 0x54]))
        await sink.write(new Uint8Array([0x39]))
      },
    }

    await endpoint.apply([{ type: 'write', path: 'projects/demo.dat', source: { path: 'projects/demo.dat', kind: 'file', size: 3, hash: 'x' } }], source, (completed) => progress.push(completed))
    const copied = []
    await endpoint.copyFile('projects/demo.dat', { write: async (chunk) => copied.push(...chunk) })

    expect(progress).toEqual([1])
    expect(copied).toEqual([0x50, 0x54, 0x39])
  })

  it('rolls back all affected host paths when a multi-file apply fails midway', async () => {
    const root = directory('factory', [file('one.dat', 'old-one'), file('two.dat', 'old-two')])
    const endpoint = createHostFolderEndpoint(root)
    const source = {
      async copyFile(path, sink) {
        if (path === 'two.dat') throw new Error('source disappeared')
        await sink.write(new TextEncoder().encode('new-one'))
      },
    }

    await expect(endpoint.apply([
      { type: 'write', path: 'one.dat', source: { path: 'one.dat', kind: 'file', size: 7, hash: 'one' }, target: { kind: 'file' } },
      { type: 'write', path: 'two.dat', source: { path: 'two.dat', kind: 'file', size: 7, hash: 'two' }, target: { kind: 'file' } },
    ], source)).rejects.toThrow('source disappeared')

    expect(await (await root.getFileHandle('one.dat')).getFile().then((value) => value.text())).toBe('old-one')
    expect(await (await root.getFileHandle('two.dat')).getFile().then((value) => value.text())).toBe('old-two')
  })

  it('removes parent directories created by a failed host apply', async () => {
    const root = directory('factory')
    const endpoint = createHostFolderEndpoint(root)

    await expect(endpoint.apply([
      { type: 'write', path: 'new/nested.dat', source: { path: 'new/nested.dat', kind: 'file', size: 1, hash: 'x' } },
    ], { copyFile: async () => { throw new Error('source failed') } })).rejects.toThrow('source failed')

    await expect(root.getDirectoryHandle('new')).rejects.toThrow('NotFoundError')
  })

  it('replaces entry kinds and treats an already-applied deletion as success', async () => {
    const root = directory('factory', [file('swap', 'old'), file('gone.dat', 'old')])
    const endpoint = createHostFolderEndpoint(root)
    const emptySource = { copyFile: async () => {} }

    await endpoint.apply([{ type: 'write', path: 'swap', source: { path: 'swap', kind: 'directory', size: 0, hash: null }, target: { path: 'swap', kind: 'file', size: 3, hash: 'old' } }], emptySource)
    await endpoint.apply([{ type: 'delete', path: 'gone.dat', source: null, target: { path: 'gone.dat', kind: 'file', size: 3, hash: 'old' } }], emptySource)
    await endpoint.apply([{ type: 'delete', path: 'gone.dat', source: null, target: { path: 'gone.dat', kind: 'file', size: 3, hash: 'old' } }], emptySource)
    await endpoint.apply([{ type: 'delete', path: 'missing/parent/gone.dat', source: null, target: { path: 'missing/parent/gone.dat', kind: 'file', size: 3, hash: 'old' } }], emptySource)

    expect((await root.getDirectoryHandle('swap')).kind).toBe('directory')
  })

  it('exposes an idle barrier that waits for the active coordinator queue', async () => {
    let release
    const coordinator = {
      subscribe: () => () => {},
      waitForIdle: () => new Promise((resolve) => { release = resolve }),
    }
    const manager = createHostFolderManager({ coordinator, metadata: {} })

    const waiting = manager.waitForIdle()
    let settled = false
    waiting.then(() => { settled = true })
    await Promise.resolve()
    expect(settled).toBe(false)
    release()
    await waiting
    expect(settled).toBe(true)
  })

  it('publishes an actionable failure when persisted-handle restoration fails', async () => {
    const manager = createHostFolderManager({
      picker: async () => directory('unused'),
      metadata: { get: async () => { throw new Error('metadata unavailable') } },
    })

    await expect(manager.restoreHostFolderHandle()).rejects.toThrow('metadata unavailable')
    expect(manager.snapshot()).toMatchObject({ state: 'failed', error: 'metadata unavailable' })
  })
})
