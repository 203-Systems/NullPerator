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

export function createStorageCoordinator(options = {}) {
  const listeners = new Set()
  const durabilityWaiters = new Set()
  let module = null
  let initialized = null
  let drain = null
  let mutationGeneration = 0
  let durableGeneration = 0
  let mutating = false
  let initialError = null
  let closedError = null
  let activeMutation = null
  let mutationQueue = Promise.resolve()
  let traceSink = options.trace ?? null
  let nextSyncId = 1
  let snapshot = Object.freeze({
    state: StorageState.Initializing,
    dirty: false,
    mutating: false,
    mutationGeneration: 0,
    durableGeneration: 0,
    syncing: false,
    reason: null,
    error: null,
  })

  const publish = (next) => {
    snapshot = Object.freeze({
      ...snapshot,
      ...next,
      dirty: mutating || mutationGeneration > durableGeneration,
      mutating,
      mutationGeneration,
      durableGeneration,
    })
    for (const listener of listeners) listener(snapshot)
  }

  const settleDurabilityWaiters = () => {
    for (const waiter of durabilityWaiters) {
      if (durableGeneration < waiter.generation) continue
      durabilityWaiters.delete(waiter)
      waiter.resolve(durableGeneration)
    }
  }

  const rejectDurabilityWaiters = (error) => {
    for (const waiter of durabilityWaiters) waiter.reject(error)
    durabilityWaiters.clear()
  }

  const reserveMutationGeneration = () => {
    if (mutationGeneration >= Number.MAX_SAFE_INTEGER) {
      throw new RangeError('Persistent storage mutation generation overflow')
    }
    mutationGeneration += 1
    return mutationGeneration
  }

  const sync = (populate) => new Promise((resolve, reject) => {
    const id = nextSyncId
    nextSyncId = nextSyncId === 0xffff_ffff ? 1 : nextSyncId + 1
    // Keep the sink that observed Begin for the matching End even if runtime
    // wiring changes while IndexedDB is in flight.
    const operationTrace = traceSink
    let traceToken = null
    try { traceToken = operationTrace?.beginStorageSync?.({ id, populate }) ?? null }
    catch { /* Tracing is observational and must never break persistence. */ }
    let settled = false
    const finish = (error) => {
      if (settled) return
      settled = true
      try { operationTrace?.endStorageSync?.(traceToken, { success: !error }) }
      catch { /* Tracing is observational and must never break persistence. */ }
      if (error) reject(error)
      else resolve()
    }
    try {
      module.FS.syncfs(populate, finish)
    } catch (error) {
      finish(error)
    }
  })

  async function drainPending() {
    try {
      do {
        while (durableGeneration < mutationGeneration) {
          if (closedError) throw closedError
          const syncingThrough = mutationGeneration
          publish({ state: StorageState.Syncing, syncing: true, error: null })
          await sync(false)
          // A fail-closed rollback can arrive while syncfs is in flight. Never
          // acknowledge that unsafe filesystem state or start another sync.
          if (closedError) throw closedError
          // Only the generation observed before syncfs began is proven durable.
          // Mutations arriving during the await remain pending for another pass.
          durableGeneration = Math.max(durableGeneration, syncingThrough)
          settleDurabilityWaiters()
          publish({ state: StorageState.Syncing, syncing: true, error: null })
        }
        if (closedError) throw closedError
        publish(mutating
          ? { state: StorageState.Syncing, syncing: true, error: null }
          : { state: StorageState.Ready, syncing: false, error: null })
        // Synchronously invoked observers may ask for another save while the
        // final Ready state is being published. Let their microtasks enqueue
        // before deciding this serialized drain is complete.
        await Promise.resolve()
      } while (durableGeneration < mutationGeneration)
    } catch (error) {
      publish({ state: StorageState.Failed, syncing: false, error: errorMessage(error) })
      rejectDurabilityWaiters(error)
      throw error
    }
  }

  function startDrain() {
    if (drain) return drain
    drain = drainPending().finally(() => { drain = null })
    return drain
  }

  function enqueueSync(reason) {
    if (closedError) return Promise.reject(closedError)
    if (!module) return Promise.reject(new Error('Persistent storage is not initialized'))
    try { reserveMutationGeneration() }
    catch (error) { return Promise.reject(error) }
    publish({ state: StorageState.Syncing, syncing: true, reason, error: null })
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
      mutating = true
      publish({ state: StorageState.Syncing, syncing: true, reason, error: null })
      let callbackCompleted = false
      try {
        const result = await callback()
        callbackCompleted = true
        if (closedError) throw closedError
        reserveMutationGeneration()
        publish({ state: StorageState.Syncing, syncing: true, reason, error: null })
        await startDrain()
        resolveBarrier(result)
        return result
      } catch (error) {
        // A request arriving while a callback rolls back still deserves one
        // serialized drain, unless the disk has explicitly been failed closed.
        if (!closedError && !callbackCompleted && durableGeneration < mutationGeneration) {
          try { await startDrain() } catch { /* The original error wins. */ }
        }
        const failure = closedError ?? error
        rejectBarrier(failure)
        throw failure
      } finally {
        activeMutation = null
        mutating = false
        if (!closedError && !drain && durableGeneration >= mutationGeneration) {
          publish({ state: StorageState.Ready, syncing: false, error: null })
        } else {
          publish({})
        }
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

  function awaitDurable(generation = mutationGeneration) {
    if (!Number.isSafeInteger(generation) || generation < 0) {
      return Promise.reject(new RangeError('Durability generation must be a non-negative safe integer'))
    }
    if (durableGeneration >= generation) return Promise.resolve(durableGeneration)
    if (generation > mutationGeneration) {
      return Promise.reject(new RangeError('Cannot await an unobserved durability generation'))
    }
    if (closedError) return Promise.reject(closedError)
    if (snapshot.state === StorageState.Failed) {
      return Promise.reject(new Error(snapshot.error ?? 'Persistent storage failed'))
    }
    return new Promise((resolve, reject) => {
      durabilityWaiters.add({ generation, resolve, reject })
    })
  }

  function attachBeforeUnloadGuard(target = globalThis.window) {
    if (typeof target?.addEventListener !== 'function') return () => {}
    const documentTarget = target.document ?? globalThis.document
    let detached = false
    let lifecycleFlush = null

    // pagehide cannot guarantee that an asynchronous IDBFS transaction will
    // finish before browser teardown. It is still useful as a best-effort
    // durability request, and shares the coordinator's serialized drain with
    // normal saves. Keeping the observed promise here folds visibilitychange
    // and pagehide from the same lifecycle transition into one flush and
    // prevents a rejected transaction from becoming an unhandled rejection.
    const requestBestEffortFlush = (reason) => {
      if (detached || lifecycleFlush) return
      try {
        lifecycleFlush = enqueueSync(reason)
      } catch (error) {
        lifecycleFlush = Promise.reject(error)
      }
      const observed = lifecycleFlush
      void observed.then(
        () => { if (lifecycleFlush === observed) lifecycleFlush = null },
        () => { if (lifecycleFlush === observed) lifecycleFlush = null },
      )
    }
    const onBeforeUnload = (event) => {
      if (!snapshot.dirty && !snapshot.mutating && !snapshot.syncing) return undefined
      event?.preventDefault?.()
      if (event) event.returnValue = ''
      return ''
    }
    const onPageHide = () => requestBestEffortFlush('pagehide-best-effort')
    const onVisibilityChange = () => {
      if (documentTarget?.visibilityState === 'hidden') {
        requestBestEffortFlush('visibility-hidden-best-effort')
      }
    }
    target.addEventListener('beforeunload', onBeforeUnload)
    target.addEventListener('pagehide', onPageHide)
    documentTarget?.addEventListener?.('visibilitychange', onVisibilityChange)
    return () => {
      if (detached) return
      detached = true
      target.removeEventListener?.('beforeunload', onBeforeUnload)
      target.removeEventListener?.('pagehide', onPageHide)
      documentTarget?.removeEventListener?.('visibilitychange', onVisibilityChange)
    }
  }

  return {
    subscribe(listener) {
      listeners.add(listener)
      listener(snapshot)
      return () => listeners.delete(listener)
    },
    snapshot: () => snapshot,
    setTraceSink(nextTraceSink) { traceSink = nextTraceSink ?? null },
    ready: () => initialized ?? Promise.reject(new Error('Persistent storage is not initialized')),
    initializationError: () => initialError,
    failClosed(error) {
      if (closedError) return closedError
      closedError = error instanceof Error ? error : new Error(errorMessage(error))
      try { reserveMutationGeneration() }
      catch { /* The closed error remains authoritative. */ }
      publish({ state: StorageState.Failed, syncing: false, error: closedError.message })
      rejectDurabilityWaiters(closedError)
      return closedError
    },
    initializePersistentFs: initialize,
    requestSync(reason = 'mutation') { return enqueueSync(reason) },
    flushNow(reason = 'flush') { return enqueueSync(reason) },
    awaitDurable,
    attachBeforeUnloadGuard,
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
