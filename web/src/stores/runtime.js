import { createRuntime } from '../handles/runtime.js'

const initialSnapshot = Object.freeze({
  state: 'idle',
  error: null,
  buildMetadata: null,
})

export function createRuntimeManager(options = {}) {
  const createModule = options.createModule ?? (() => createRuntime(options))
  const isolated = options.crossOriginIsolated ?? globalThis.crossOriginIsolated === true
  const listeners = new Set()
  let snapshot = initialSnapshot
  let module = null
  let operation = Promise.resolve()

  async function waitForCppReady(runtimeModule) {
    if (typeof runtimeModule.getState !== 'function') {
      throw new TypeError('Runtime handle must provide getState()')
    }
    const deadline = Date.now() + (options.readyTimeoutMs ?? 10_000)
    while (true) {
      const state = runtimeModule.getState()
      if (state === 1) return
      if (state === 3) {
        throw new Error(runtimeModule.getLastError?.() || 'C++ runtime initialization failed')
      }
      if (state === 2) throw new Error('C++ runtime stopped during initialization')
      if (Date.now() >= deadline) throw new Error('Timed out waiting for the C++ runtime')
      await new Promise((resolve) => setTimeout(resolve, 10))
    }
  }

  function publish(next) {
    snapshot = Object.freeze({ ...snapshot, ...next })
    for (const listener of listeners) listener(snapshot)
  }

  function enqueue(action) {
    const result = operation.then(action, action)
    operation = result.catch(() => {})
    return result
  }

  async function startNow() {
    if (!isolated) {
      const message =
        'Cross-origin isolation is required. Serve with Cross-Origin-Opener-Policy (COOP): same-origin and Cross-Origin-Embedder-Policy (COEP): require-corp.'
      publish({ state: 'failed', error: message })
      throw new Error(message)
    }

    publish({ state: 'booting', error: null })
    try {
      module = await createModule()
      await waitForCppReady(module)
      const buildMetadata = module.getBuildMetadataJson
        ? JSON.parse(module.getBuildMetadataJson())
        : null
      publish({ state: 'ready', buildMetadata })
      return module
    } catch (error) {
      const failedModule = module
      module = null
      let cleanupError = null
      try {
        await failedModule?.terminate?.()
      } catch (caught) {
        cleanupError = caught
      }
      const primaryMessage = error instanceof Error ? error.message : String(error)
      const message = cleanupError
        ? `${primaryMessage}; cleanup failed: ${cleanupError instanceof Error ? cleanupError.message : String(cleanupError)}`
        : primaryMessage
      publish({ state: 'failed', error: message })
      throw error
    }
  }

  async function stopNow() {
    if (!module) return
    const stoppingModule = module
    module = null
    publish({ state: 'stopping' })
    let cleanupError = null
    try {
      await stoppingModule.requestShutdown?.()
    } catch (error) {
      cleanupError = error
    }
    try {
      await stoppingModule.terminate?.()
    } catch (error) {
      cleanupError ??= error
    }

    if (cleanupError) {
      const message = cleanupError instanceof Error ? cleanupError.message : String(cleanupError)
      publish({ state: 'failed', error: message, buildMetadata: null })
      throw cleanupError
    }
    publish({ state: 'idle', error: null, buildMetadata: null })
  }

  return {
    subscribe(listener) {
      listeners.add(listener)
      listener(snapshot)
      return () => listeners.delete(listener)
    },
    getSnapshot() {
      return snapshot
    },
    start() {
      return enqueue(async () => module ?? startNow())
    },
    stop() {
      return enqueue(stopNow)
    },
    restart() {
      return enqueue(async () => {
        await stopNow()
        return startNow()
      })
    },
  }
}

export const runtimeStore = createRuntimeManager()

export function restartRuntime() {
  return runtimeStore.restart()
}
