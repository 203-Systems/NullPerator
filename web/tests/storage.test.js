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

  it('fails closed after an unrecoverable filesystem rollback and rejects every later sync', async () => {
    const module = makeModule((populate, callback) => callback(null))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    const failure = storage.failClosed(new Error('rollback could not restore backup'))
    expect(storage.snapshot()).toMatchObject({ state: StorageState.Failed, error: failure.message })
    await expect(storage.requestSync('later mutation')).rejects.toBe(failure)
    await expect(storage.flushNow('later flush')).rejects.toBe(failure)
  })

  it('does not start another IDBFS sync when storage is failed closed during an active drain', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => {
      if (populate) callback(null)
      else syncs.push(callback)
    })
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    const first = storage.requestSync('first')
    expect(syncs).toHaveLength(1)
    const closed = storage.failClosed(new Error('rollback unsafe'))
    syncs.shift()(null)
    await expect(first).rejects.toBe(closed)
    await Promise.resolve()
    expect(syncs).toHaveLength(0)
    await expect(storage.requestSync('later')).rejects.toBe(closed)
    await expect(storage.flushNow('later')).rejects.toBe(closed)
  })

  it('waits for an existing sync before entering an exclusive file mutation, then persists it once', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    const existing = storage.requestSync('C++ mutation')
    expect(syncs).toHaveLength(1)
    let entered = false
    const mutation = storage.runMutation('files-upload', async () => { entered = true })
    await Promise.resolve()
    expect(entered).toBe(false)
    syncs.shift()(null)
    await existing
    await Promise.resolve()
    expect(entered).toBe(true)
    expect(syncs).toHaveLength(1)
    syncs.shift()(null)
    await mutation
  })

  it('serializes file mutations and folds a request arriving inside one mutation into its release sync', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    const events = []
    let queuedRequest
    const first = storage.runMutation('first', async () => {
      events.push('first-enter')
      queuedRequest = storage.requestSync('during-first')
      events.push('first-exit')
    })
    const second = storage.runMutation('second', async () => { events.push('second-enter') })
    await Promise.resolve(); await Promise.resolve()
    expect(events).toEqual(['first-enter', 'first-exit'])
    expect(syncs).toHaveLength(1)
    expect(events).not.toContain('second-enter')
    syncs.shift()(null)
    await first
    await queuedRequest
    await Promise.resolve()
    expect(events).toEqual(['first-enter', 'first-exit', 'second-enter'])
    expect(syncs).toHaveLength(1)
    syncs.shift()(null)
    await second
  })

  it('does not start a follow-up sync when a mutation fails storage closed during rollback', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    const rollbackFailure = new Error('rollback could not restore backup')

    const mutation = storage.runMutation('files-delete', async () => {
      storage.failClosed(rollbackFailure)
      throw rollbackFailure
    })

    await expect(mutation).rejects.toBe(rollbackFailure)
    expect(syncs).toHaveLength(0)
    await expect(storage.requestSync('later')).rejects.toBe(rollbackFailure)
  })

  it('does not retry a failed mutation release sync until a later explicit request', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)

    const mutation = storage.runMutation('files-upload', async () => {})
    await Promise.resolve()
    await Promise.resolve()
    expect(syncs).toHaveLength(1)
    syncs.shift()(new Error('quota exceeded'))

    await expect(mutation).rejects.toThrow('quota exceeded')
    expect(syncs).toHaveLength(0)
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
