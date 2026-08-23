import { createRuntime } from '../handles/runtime.js'
import { createAudioStore } from './audio.js'

const initialSnapshot = Object.freeze({
  state: 'idle',
  error: null,
  buildMetadata: null,
  frameContent: 'unavailable',
  input: null,
  audio: null,
  storage: null,
  files: null,
  hostFolder: null,
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
  let stopStage = null
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
    if (stopStage) {
      throw new Error('Runtime shutdown is incomplete; retry Stop before starting again')
    }
    if (!isolated) {
      const message =
        'Cross-origin isolation is required. Serve with Cross-Origin-Opener-Policy (COOP): same-origin and Cross-Origin-Embedder-Policy (COEP): require-corp.'
      publish({ state: 'failed', error: message })
      throw new Error(message)
    }

    publish({ state: 'booting', error: null, input: null, storage: null, files: null, hostFolder: null })
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
      publish({ state: 'ready', buildMetadata, frameContent, input: module.input ?? null, audio, storage: module.storage ?? null, files: module.files ?? null, hostFolder: module.hostFolder ?? null })
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
      publish({ state: 'failed', error: message, frameContent: 'unavailable', input: null, audio: null, storage: null, files: null, hostFolder: null })
      throw error
    }
  }

  async function stopNow() {
    if (!module && !stopStage) return
    const stage = stopStage ?? {
      module,
      inputReleased: false,
      audioStopped: false,
      hostFolderIdle: false,
      shutdownRequested: false,
      cppStopped: false,
      flushed: false,
    }
    const stoppingModule = stage.module
    publish({ state: 'stopping', input: null, audio: null, storage: stoppingModule.storage ?? null, files: null, hostFolder: null })
    if (!stage.inputReleased) {
      try {
        stoppingModule.input?.releaseAllActions?.()
        stage.inputReleased = true
      } catch (error) {
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, storage: stoppingModule.storage ?? null, files: null })
        throw error
      }
    }
    if (!stage.audioStopped && stoppingModule.audioStore) {
      try {
        await stoppingModule.audioStore.stop()
        stage.audioStopped = true
      } catch (error) {
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, storage: stoppingModule.storage ?? null, files: null })
        throw error
      }
    } else if (!stage.audioStopped) {
      stage.audioStopped = true
    }
    if (!stage.hostFolderIdle) {
      try {
        await stoppingModule.hostFolder?.waitForIdle?.()
        stage.hostFolderIdle = true
      } catch (error) {
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, storage: stoppingModule.storage ?? null, files: null, hostFolder: null })
        throw error
      }
    }
    if (!stage.cppStopped) {
      try {
        if (!stage.shutdownRequested && typeof stoppingModule.requestShutdown === 'function') {
          try {
            await stoppingModule.requestShutdown()
            stage.shutdownRequested = true
          } catch (error) {
            // A throwing bridge call may still have reached C++. Keep the
            // request marked only when the native lifecycle confirms it has
            // moved beyond Ready; otherwise a later Stop may safely resend.
            const state = stoppingModule.getState?.()
            stage.shutdownRequested = state === 2 || state === 4
            throw error
          }
        }
        // Native handles move to Stopping before their browser-main teardown
        // acknowledgement. Once RequestShutdown was issued, every retry only
        // confirms that acknowledgement; it never issues a second shutdown.
        if (typeof stoppingModule.getState === 'function' && stoppingModule.getState() !== 4) {
          await waitForCppStopped(stoppingModule)
        }
        stage.cppStopped = true
      } catch (error) {
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, storage: stoppingModule.storage ?? null, files: null })
        throw error
      }
    }
    if (!stage.flushed) {
      try {
        await stoppingModule.storage?.flushNow?.('shutdown')
        stage.flushed = true
      } catch (error) {
        // Keep the stopped WASM module alive: its MEMFS is the only durable
        // source until a later Stop retries this flush.
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, storage: stoppingModule.storage ?? null, files: null })
        throw error
      }
    }
    try {
      await stoppingModule.terminate?.()
    } catch (error) {
      // The filesystem is already durably flushed. Preserve the established
      // lifecycle recovery behaviour for a worker-cleanup failure: report it,
      // but do not retain a stale module as a future start candidate.
      stopStage = null
      module = null
      publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, storage: stoppingModule.storage ?? null, files: null, hostFolder: null })
      throw error
    }
    stopStage = null
    module = null
    publish({ state: 'idle', error: null, buildMetadata: null, frameContent: 'unavailable', input: null, audio: null, storage: null, files: null, hostFolder: null })
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
      return enqueue(async () => {
        if (stopStage) return startNow()
        return module ?? startNow()
      })
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
