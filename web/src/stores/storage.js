export const StorageState = Object.freeze({
  Initializing: 'initializing',
  Ready: 'ready',
  Syncing: 'syncing',
  Failed: 'failed',
})

const mountPoint = '/data'
const populateDependency = 'picotracker-idbfs-populate'

function isAlreadyExists(error) {
  return error?.code === 'EEXIST' || error?.errno === 17
}

function errorMessage(error) {
  return error instanceof Error ? error.message : String(error)
}

export function createStorageCoordinator() {
  const listeners = new Set()
  let module = null
  let initialized = null
  let drain = null
  let dirty = false
  let initialError = null
  let closedError = null
  let activeMutation = null
  let mutationQueue = Promise.resolve()
  let snapshot = Object.freeze({
    state: StorageState.Initializing,
    dirty: false,
    syncing: false,
    reason: null,
    error: null,
  })

  const publish = (next) => {
    snapshot = Object.freeze({ ...snapshot, ...next, dirty })
    for (const listener of listeners) listener(snapshot)
  }

  const sync = (populate) => new Promise((resolve, reject) => {
    try {
      module.FS.syncfs(populate, (error) => error ? reject(error) : resolve())
    } catch (error) {
      reject(error)
    }
  })

  async function drainDirty() {
    try {
      do {
        while (dirty) {
          if (closedError) throw closedError
          dirty = false
          publish({ state: StorageState.Syncing, syncing: true, error: null })
          await sync(false)
        }
        if (closedError) throw closedError
        publish({ state: StorageState.Ready, syncing: false, error: null })
        // Synchronously invoked observers may ask for another save while the
        // final Ready state is being published. Let their microtasks enqueue
        // before deciding this serialized drain is complete.
        await Promise.resolve()
      } while (dirty)
    } catch (error) {
      dirty = true
      publish({ state: StorageState.Failed, syncing: false, error: errorMessage(error) })
      throw error
    }
  }

  function startDrain() {
    if (drain) return drain
    drain = drainDirty().finally(() => { drain = null })
    return drain
  }

  function enqueueSync(reason) {
    if (closedError) return Promise.reject(closedError)
    if (!module) return Promise.reject(new Error('Persistent storage is not initialized'))
    dirty = true
    publish({ state: drain || activeMutation ? StorageState.Syncing : StorageState.Ready, reason, error: null })
    if (activeMutation) return activeMutation.promise
    return startDrain()
  }

  function runMutation(reason, callback) {
    const action = mutationQueue.then(async () => {
      if (closedError) throw closedError
      if (drain) await drain
      if (closedError) throw closedError
      let resolveBarrier
      let rejectBarrier
      const barrier = new Promise((resolve, reject) => { resolveBarrier = resolve; rejectBarrier = reject })
      barrier.catch(() => {})
      activeMutation = { promise: barrier }
      let callbackCompleted = false
      try {
        const result = await callback()
        callbackCompleted = true
        if (closedError) throw closedError
        dirty = true
        publish({ state: StorageState.Syncing, syncing: true, reason, error: null })
        await startDrain()
        resolveBarrier(result)
        return result
      } catch (error) {
        // A request arriving while a callback rolls back still deserves one
        // serialized drain, unless the disk has explicitly been failed closed.
        if (!closedError && !callbackCompleted && dirty) {
          try { await startDrain() } catch { /* The original error wins. */ }
        }
        const failure = closedError ?? error
        rejectBarrier(failure)
        throw failure
      } finally {
        activeMutation = null
      }
    }, async () => { throw closedError ?? new Error('Storage mutation queue failed') })
    mutationQueue = action.catch(() => {})
    return action
  }

  function initialize(nextModule) {
    if (initialized) return initialized
    module = nextModule
    publish({ state: StorageState.Initializing, syncing: false, error: null, reason: 'populate' })
    let dependencyAdded = false
    const releaseDependency = () => {
      if (!dependencyAdded) return
      dependencyAdded = false
      module.removeRunDependency(populateDependency)
    }
    try {
      module.addRunDependency(populateDependency)
      dependencyAdded = true
      try {
        module.FS.mkdir(mountPoint)
      } catch (error) {
        if (!isAlreadyExists(error)) throw error
      }
      module.FS.mount(module.IDBFS, {}, mountPoint)
    } catch (error) {
      initialError = error
      publish({ state: StorageState.Failed, syncing: false, error: errorMessage(error) })
      initialized = Promise.reject(error)
      releaseDependency()
      return initialized
    }

    initialized = sync(true).then(
      () => {
        publish({ state: StorageState.Ready, syncing: false, error: null, reason: 'populated' })
      },
      (error) => {
        initialError = error
        publish({ state: StorageState.Failed, syncing: false, error: errorMessage(error) })
        throw error
      },
    ).finally(releaseDependency)
    return initialized
  }

  return {
    subscribe(listener) {
      listeners.add(listener)
      listener(snapshot)
      return () => listeners.delete(listener)
    },
    snapshot: () => snapshot,
    ready: () => initialized ?? Promise.reject(new Error('Persistent storage is not initialized')),
    initializationError: () => initialError,
    failClosed(error) {
      closedError = error instanceof Error ? error : new Error(errorMessage(error))
      dirty = true
      publish({ state: StorageState.Failed, syncing: false, error: closedError.message })
      return closedError
    },
    initializePersistentFs: initialize,
    requestSync(reason = 'mutation') { return enqueueSync(reason) },
    flushNow(reason = 'flush') { return enqueueSync(reason) },
    runMutation,
  }
}

export function initializePersistentFs(module, storage = createStorageCoordinator()) {
  return storage.initializePersistentFs(module)
}

// Emscripten calls preRun callbacks with its private Module as argument. This
// hook intentionally does not capture the caller-supplied module options.
export function persistentFsPreRun(storage) {
  return (privateModule) => {
    void storage.initializePersistentFs(privateModule).catch(() => {})
  }
}
