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
    expect(storage.snapshot()).toMatchObject({
      state: StorageState.Ready,
      dirty: false,
      mutationGeneration: 0,
      durableGeneration: 0,
    })
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
    expect(storage.snapshot()).toMatchObject({ mutationGeneration: 1, durableGeneration: 0 })
    expect(syncs).toHaveLength(1)
    const second = storage.requestSync('sample-edit')
    expect(storage.snapshot()).toMatchObject({ mutationGeneration: 2, durableGeneration: 0 })
    expect(syncs).toHaveLength(1)

    syncs.shift()(null)
    await Promise.resolve()
    expect(storage.snapshot()).toMatchObject({ mutationGeneration: 2, durableGeneration: 1 })
    expect(syncs).toHaveLength(1)
    syncs.shift()(null)
    await Promise.all([first, second])

    expect(storage.snapshot()).toMatchObject({
      state: StorageState.Ready,
      dirty: false,
      mutationGeneration: 2,
      durableGeneration: 2,
    })
  })

  it('awaits the requested durability generation across coalesced syncs', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)

    const first = storage.requestSync('project-save')
    const firstGeneration = storage.snapshot().mutationGeneration
    let durable = false
    const fence = storage.awaitDurable(firstGeneration).then(() => { durable = true })
    storage.requestSync('sample-save')
    syncs.shift()(null)
    await fence
    expect(durable).toBe(true)
    expect(storage.snapshot()).toMatchObject({
      mutationGeneration: firstGeneration + 1,
      durableGeneration: firstGeneration,
      syncing: true,
    })
    syncs.shift()(null)
    await first
    expect(storage.snapshot().durableGeneration).toBe(storage.snapshot().mutationGeneration)
  })

  it('guards browser unload only while mutations are not durable', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    let handler = null
    const target = {
      addEventListener: (name, next) => { if (name === 'beforeunload') handler = next },
      removeEventListener: (name, next) => { if (name === 'beforeunload' && handler === next) handler = null },
    }
    const detach = storage.attachBeforeUnloadGuard(target)
    const clean = { preventDefault() { throw new Error('clean storage must not block unload') } }
    expect(handler(clean)).toBeUndefined()

    const pending = storage.requestSync('save')
    const dirtyEvent = { prevented: false, returnValue: undefined, preventDefault() { this.prevented = true } }
    expect(handler(dirtyEvent)).toBe('')
    expect(dirtyEvent).toMatchObject({ prevented: true, returnValue: '' })
    syncs.shift()(null)
    await pending
    expect(handler(clean)).toBeUndefined()
    detach()
    expect(handler).toBeNull()
  })

  it('coalesces hidden visibility and pagehide into one best-effort flush and detaches both', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    const windowListeners = new Map()
    const documentListeners = new Map()
    const documentTarget = {
      visibilityState: 'visible',
      addEventListener(name, listener) { documentListeners.set(name, listener) },
      removeEventListener(name, listener) {
        if (documentListeners.get(name) === listener) documentListeners.delete(name)
      },
    }
    const target = {
      document: documentTarget,
      addEventListener(name, listener) { windowListeners.set(name, listener) },
      removeEventListener(name, listener) {
        if (windowListeners.get(name) === listener) windowListeners.delete(name)
      },
    }
    const detach = storage.attachBeforeUnloadGuard(target)
    const hiddenHandler = documentListeners.get('visibilitychange')
    const pageHideHandler = windowListeners.get('pagehide')

    expect(hiddenHandler()).toBeUndefined()
    expect(syncs).toHaveLength(0)
    documentTarget.visibilityState = 'hidden'
    expect(hiddenHandler()).toBeUndefined()
    expect(pageHideHandler()).toBeUndefined()
    expect(syncs).toHaveLength(1)
    expect(storage.snapshot()).toMatchObject({
      reason: 'visibility-hidden-best-effort',
      mutationGeneration: 1,
      durableGeneration: 0,
      dirty: true,
    })

    syncs.shift()(null)
    await Promise.resolve()
    await Promise.resolve()
    expect(storage.snapshot()).toMatchObject({
      state: StorageState.Ready,
      mutationGeneration: 1,
      durableGeneration: 1,
      dirty: false,
    })

    detach()
    expect(windowListeners.has('beforeunload')).toBe(false)
    expect(windowListeners.has('pagehide')).toBe(false)
    expect(documentListeners.has('visibilitychange')).toBe(false)
    expect(hiddenHandler()).toBeUndefined()
    expect(pageHideHandler()).toBeUndefined()
    expect(syncs).toHaveLength(0)
    expect(storage.snapshot().mutationGeneration).toBe(1)
  })

  it('observes a failed lifecycle flush while preserving failed and dirty state', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    const listeners = new Map()
    const target = {
      addEventListener(name, listener) { listeners.set(name, listener) },
      removeEventListener(name, listener) {
        if (listeners.get(name) === listener) listeners.delete(name)
      },
    }
    storage.attachBeforeUnloadGuard(target)

    expect(listeners.get('pagehide')()).toBeUndefined()
    syncs.shift()(new Error('IDB quota exceeded'))
    await Promise.resolve()
    await Promise.resolve()

    expect(storage.snapshot()).toMatchObject({
      state: StorageState.Failed,
      error: 'IDB quota exceeded',
      mutationGeneration: 1,
      durableGeneration: 0,
      dirty: true,
      syncing: false,
    })
  })

  it('fails an explicit shutdown flush and does not report clean storage', async () => {
    const module = makeModule((populate, callback) => callback(populate ? null : new Error('quota exceeded')))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)

    await expect(storage.flushNow('shutdown')).rejects.toThrow('quota exceeded')
    expect(storage.snapshot()).toMatchObject({ state: StorageState.Failed, dirty: true })
  })

  it('uses a real persistence epoch for a clean explicit flush', async () => {
    const calls = []
    const module = makeModule((populate, callback) => {
      calls.push(populate)
      callback(null)
    })
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)

    await storage.flushNow('manual-flush')

    expect(calls).toEqual([true, false])
    expect(storage.snapshot()).toMatchObject({
      state: StorageState.Ready,
      reason: 'manual-flush',
      mutationGeneration: 1,
      durableGeneration: 1,
      dirty: false,
    })
  })

  it('retries failed pending generations only after an explicit new request', async () => {
    let fail = true
    const module = makeModule((populate, callback) => {
      if (populate) callback(null)
      else if (fail) { fail = false; callback(new Error('quota exceeded')) }
      else callback(null)
    })
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)

    await expect(storage.requestSync('first')).rejects.toThrow('quota exceeded')
    expect(storage.snapshot()).toMatchObject({
      mutationGeneration: 1,
      durableGeneration: 0,
      dirty: true,
      state: StorageState.Failed,
    })

    await storage.requestSync('retry')
    expect(storage.snapshot()).toMatchObject({
      mutationGeneration: 2,
      durableGeneration: 2,
      dirty: false,
      state: StorageState.Ready,
    })
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

  it('marks an active mutation dirty and allocates exactly one generation after success', async () => {
    const syncs = []
    const gate = deferred()
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)
    let handler
    storage.attachBeforeUnloadGuard({
      addEventListener: (name, next) => { if (name === 'beforeunload') handler = next },
      removeEventListener() {},
    })

    const mutation = storage.runMutation('files-upload', () => gate.promise)
    await Promise.resolve()
    await Promise.resolve()
    expect(storage.snapshot()).toMatchObject({
      mutating: true,
      dirty: true,
      mutationGeneration: 0,
      durableGeneration: 0,
    })
    const event = { prevented: false, preventDefault() { this.prevented = true } }
    expect(handler(event)).toBe('')
    expect(event.prevented).toBe(true)

    gate.resolve('uploaded')
    await Promise.resolve()
    await Promise.resolve()
    expect(syncs).toHaveLength(1)
    syncs.shift()(null)
    await expect(mutation).resolves.toBe('uploaded')
    expect(storage.snapshot()).toMatchObject({
      mutating: false,
      dirty: false,
      mutationGeneration: 1,
      durableGeneration: 1,
    })
  })

  it('does not allocate a generation for a mutation whose rollback succeeds', async () => {
    const module = makeModule((populate, callback) => callback(null))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)

    await expect(storage.runMutation('files-rename', async () => {
      throw new Error('rename rolled back')
    })).rejects.toThrow('rename rolled back')

    expect(storage.snapshot()).toMatchObject({
      state: StorageState.Ready,
      mutating: false,
      dirty: false,
      mutationGeneration: 0,
      durableGeneration: 0,
    })
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

  it('validates durability fences and rejects pending waiters when failed closed', async () => {
    const syncs = []
    const module = makeModule((populate, callback) => populate ? callback(null) : syncs.push(callback))
    const storage = createStorageCoordinator()
    await initializePersistentFs(module, storage)

    await expect(storage.awaitDurable(-1)).rejects.toThrow(/non-negative safe integer/i)
    await expect(storage.awaitDurable(0.5)).rejects.toThrow(/non-negative safe integer/i)
    await expect(storage.awaitDurable(1)).rejects.toThrow(/unobserved/i)
    await expect(storage.awaitDurable(0)).resolves.toBe(0)

    const pendingSync = storage.requestSync('save')
    const fence = storage.awaitDurable(1)
    const failure = storage.failClosed(new Error('rollback unsafe'))
    await expect(fence).rejects.toBe(failure)
    syncs.shift()(null)
    await expect(pendingSync).rejects.toBe(failure)
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
