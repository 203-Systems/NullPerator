import { createRuntime } from '../handles/runtime.js'
import { createAudioStore } from './audio.js'

const initialSnapshot = Object.freeze({
  state: 'idle',
  error: null,
  buildMetadata: null,
  frameContent: 'unavailable',
  input: null,
  audio: null,
})

function classifyFrame(frame) {
  if (!(frame instanceof Uint8Array) || frame.length !== 240 * 240 * 4) {
    return 'unavailable'
  }
  const [red, green, blue, alpha] = frame
  for (let offset = 4; offset < frame.length; offset += 4) {
    if (
      frame[offset] !== red ||
      frame[offset + 1] !== green ||
      frame[offset + 2] !== blue ||
      frame[offset + 3] !== alpha
    ) {
      return 'rendered'
    }
  }
  return 'uniform'
}

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

  async function waitForCppStopped(runtimeModule) {
    if (typeof runtimeModule.getState !== 'function') return
    const deadline = Date.now() + (options.shutdownTimeoutMs ?? 5_000)
    while (true) {
      const state = runtimeModule.getState()
      if (state === 4) return
      if (state === 3) {
        throw new Error(runtimeModule.getLastError?.() || 'C++ shutdown failed')
      }
      if (Date.now() >= deadline) throw new Error('Timed out waiting for C++ shutdown')
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

    publish({ state: 'booting', error: null, input: null })
    try {
      module = await createModule()
      await waitForCppReady(module)
      const buildMetadata = module.getBuildMetadataJson
        ? JSON.parse(module.getBuildMetadataJson())
        : null
      const frameContent = classifyFrame(module.captureFrameRgba?.())
      const audio = module.audio ? createAudioStore(module.audio) : null
      if (audio) {
        await audio.initialize()
        module.audioStore = audio
      }
      publish({ state: 'ready', buildMetadata, frameContent, input: module.input ?? null, audio })
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
      publish({ state: 'failed', error: message, frameContent: 'unavailable', input: null, audio: null })
      throw error
    }
  }

  async function stopNow() {
    if (!module) return
    const stoppingModule = module
    module = null
    publish({ state: 'stopping', input: null, audio: null })
    let cleanupError = null
    try {
      stoppingModule.input?.releaseAllActions?.()
    } catch (error) {
      cleanupError = error
    }
    if (stoppingModule.audioStore) {
      try {
        await stoppingModule.audioStore.stop()
      } catch (error) {
        cleanupError ??= error
      }
    }
    try {
      if (typeof stoppingModule.requestShutdown === 'function') {
        await stoppingModule.requestShutdown()
        // Native handles move to Stopping before their browser-main teardown
        // acknowledgement. Lightweight test/custom handles may instead make
        // their requestShutdown promise itself the acknowledgement and remain
        // in Ready, in which case there is no native state transition to poll.
        if (stoppingModule.getState?.() === 2) {
          await waitForCppStopped(stoppingModule)
        }
      }
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
      publish({ state: 'failed', error: message, buildMetadata: null, input: null, audio: null })
      throw cleanupError
    }
    publish({ state: 'idle', error: null, buildMetadata: null, frameContent: 'unavailable', input: null, audio: null })
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
