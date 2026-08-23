import { describe, expect, it } from 'vitest'

import {
  StorageState,
  createStorageCoordinator,
  initializePersistentFs,
  persistentFsPreRun,
} from '../src/stores/storage.js'
import { normalizePersistentPath } from '../src/handles/filesystem.js'

function deferred() {
  let resolve
  let reject
  const promise = new Promise((nextResolve, nextReject) => {
    resolve = nextResolve
    reject = nextReject
  })
  return { promise, resolve, reject }
}

function makeModule(syncfs) {
  const calls = []
  return {
    IDBFS: { name: 'IDBFS' },
    FS: {
      mkdir(path) { calls.push(['mkdir', path]) },
      mount(type, options, path) { calls.push(['mount', type, options, path]) },
      syncfs,
    },
    addRunDependency(id) { calls.push(['add', id]) },
    removeRunDependency(id) { calls.push(['remove', id]) },
    calls,
  }
}

describe('IDBFS storage coordinator', () => {
  it('mounts /data and holds startup on a populate run dependency', async () => {
    let finishPopulate
    const module = makeModule((populate, callback) => {
      expect(populate).toBe(true)
      finishPopulate = callback
    })
    const storage = createStorageCoordinator()

    const initialized = initializePersistentFs(module, storage)

    expect(storage.snapshot()).toMatchObject({ state: StorageState.Initializing })
    expect(module.calls).toEqual([
      ['add', 'picotracker-idbfs-populate'],
      ['mkdir', '/data'],
      ['mount', module.IDBFS, {}, '/data'],
    ])

    finishPopulate(null)
    await expect(initialized).resolves.toBeUndefined()
    expect(module.calls.at(-1)).toEqual(['remove', 'picotracker-idbfs-populate'])
    expect(storage.snapshot()).toMatchObject({ state: StorageState.Ready, dirty: false })
  })

  it('propagates a failed initial population instead of permitting C++ startup', async () => {
    const module = makeModule((populate, callback) => callback(new Error('IDB unavailable')))
    const storage = createStorageCoordinator()

    await expect(initializePersistentFs(module, storage)).rejects.toThrow('IDB unavailable')
    expect(storage.snapshot()).toMatchObject({ state: StorageState.Failed, dirty: false })
    expect(module.calls).toContainEqual(['add', 'picotracker-idbfs-populate'])
    expect(module.calls.filter(([kind]) => kind === 'remove')).toEqual([
      ['remove', 'picotracker-idbfs-populate'],
    ])
    expect(storage.initializationError()).toEqual(expect.any(Error))
  })

  it('serializes overlapping mutations and follows an active sync with dirty work', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => {
      if (populate) return callback(null)
      syncs.push(callback)
    })
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)

    const first = storage.requestSync('save')
    expect(syncs).toHaveLength(1)
    const second = storage.requestSync('sample-edit')
    expect(syncs).toHaveLength(1)

    syncs.shift()(null)
    await Promise.resolve()
    expect(syncs).toHaveLength(1)
    syncs.shift()(null)
    await Promise.all([first, second])

    expect(storage.snapshot()).toMatchObject({ state: StorageState.Ready, dirty: false })
  })

  it('fails an explicit shutdown flush and does not report clean storage', async () => {
    const module = makeModule((populate, callback) => callback(populate ? null : new Error('quota exceeded')))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)

    await expect(storage.flushNow('shutdown')).rejects.toThrow('quota exceeded')
    expect(storage.snapshot()).toMatchObject({ state: StorageState.Failed, dirty: true })
  })

  it('drains a listener-reentrant save before resolving the original request', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => {
      if (populate) return callback(null)
      syncs.push(callback)
    })
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    let sawSync = false
    let reentered = false
    storage.subscribe((snapshot) => {
      if (snapshot.syncing) sawSync = true
      if (sawSync && !snapshot.syncing && !reentered && snapshot.state === StorageState.Ready) {
        reentered = true
        void storage.requestSync('listener')
      }
    })

    const first = storage.requestSync('save')
    syncs.shift()(null)
    await new Promise((resolve) => setTimeout(resolve, 0))
    expect(syncs).toHaveLength(1)
    syncs.shift()(null)
    await first
    expect(storage.snapshot()).toMatchObject({ state: StorageState.Ready, dirty: false })
  })

  it('installs an authoritative private-Module preRun hook', async () => {
    let finishPopulate
    const module = makeModule((populate, callback) => { finishPopulate = callback })
    const storage = createStorageCoordinator()
    const preRun = persistentFsPreRun(storage)

    preRun(module)
    expect(module.calls[0]).toEqual(['add', 'picotracker-idbfs-populate'])
    finishPopulate(null)
    await storage.ready()
  })
})

describe('persistent filesystem paths', () => {
  it('accepts normalized descendants of /data only', () => {
    expect(normalizePersistentPath('/data/projects/demo/lgptsav.dat')).toBe('/data/projects/demo/lgptsav.dat')
    expect(normalizePersistentPath('/data/projects/../samples/kick.wav')).toBe('/data/samples/kick.wav')
  })

  it('rejects absolute escapes, traversal, and ambiguous separators', () => {
    expect(() => normalizePersistentPath('/tmp/escaped')).toThrow(/outside \/data/i)
    expect(() => normalizePersistentPath('/data/../../escaped')).toThrow(/outside \/data/i)
    expect(() => normalizePersistentPath('projects/demo')).toThrow(/absolute/i)
    expect(() => normalizePersistentPath('/data\\escaped')).toThrow(/separator/i)
  })
})
