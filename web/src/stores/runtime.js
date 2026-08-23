import { createRuntime } from '../handles/runtime.js'
import { createAudioStore } from './audio.js'
import { createMidiStore } from './midi.js'
import { createLogStore } from './logs.js'
import { createTraceStore } from './trace.js'

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
  midi: null,
  logs: null,
  trace: null,
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
  const logs = options.logs ?? createLogStore(options.logOptions)
  const createModule = options.createModule ?? (() => createRuntime({ ...options, logs }))
  const isolated = options.crossOriginIsolated ?? globalThis.crossOriginIsolated === true
  const listeners = new Set()
  let snapshot = Object.freeze({ ...initialSnapshot, logs })
  let module = null
  let stopStage = null
  let operation = Promise.resolve()
  let nativeStateTimer = null
  const setNativeStateInterval = options.setNativeStateInterval ??
    globalThis.window?.setInterval?.bind(globalThis.window)
  const clearNativeStateInterval = options.clearNativeStateInterval ??
    globalThis.window?.clearInterval?.bind(globalThis.window)

  function stopNativeStateMonitor() {
    if (nativeStateTimer === null) return
    clearNativeStateInterval?.(nativeStateTimer)
    nativeStateTimer = null
  }

  function quiesceAfterNativeFailure(runtimeModule) {
    const actions = [
      () => runtimeModule.audioStore?.stop?.(),
      () => runtimeModule.midiStore?.stop?.(),
      () => runtimeModule.traceStore?.dispose?.(),
      () => runtimeModule.quiesceAfterFatal?.(),
    ]
    for (const action of actions) {
      try {
        // Every loop is stopped synchronously by its store before any returned
        // promise settles. Teardown errors are deliberately non-fatal here:
        // the native C++ error remains the authoritative recovery diagnosis.
        void Promise.resolve(action()).catch(() => {})
      } catch {
        // A diagnostic loop must never replace the original native failure.
      }
    }
  }

  function startNativeStateMonitor(runtimeModule) {
    stopNativeStateMonitor()
    if (typeof setNativeStateInterval !== 'function') return
    nativeStateTimer = setNativeStateInterval(() => {
      if (module !== runtimeModule || snapshot.state !== 'ready') return
      let state
      let message = ''
      try {
        state = runtimeModule.getState()
        if (state === 1) return
        message = runtimeModule.getLastError?.() || ''
      } catch (error) {
        message = error instanceof Error ? error.message : String(error)
      }
      stopNativeStateMonitor()
      if (!message) {
        if (state === 2) message = 'C++ runtime began stopping unexpectedly'
        else if (state === 4) message = 'C++ runtime stopped unexpectedly'
        else if (state === 3) message = 'C++ runtime failed'
        else message = `C++ runtime entered unexpected state ${String(state)}`
      }
      quiesceAfterNativeFailure(runtimeModule)
      // Preserve storage, files, logs, and trace so diagnostics and a ZIP
      // backup remain available. Input/audio/MIDI are removed from the live UI
      // until the serialized Restart has completed native teardown.
      publish({
        state: 'failed', error: message, frameContent: 'unavailable',
        input: null, audio: null, midi: null,
      })
    }, options.nativeStatePollMs ?? 100)
  }

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
      // Failed remains visible while native fatal cleanup waits for the
      // browser-main audio teardown acknowledgement. It is not a reason to
      // terminate the worker early; wait for Stopped just like normal Stop.
      if (Date.now() >= deadline) throw new Error('Timed out waiting for C++ shutdown')
      await new Promise((resolve) => setTimeout(resolve, 10))
    }
  }

  function publish(next) {
    if (next.state === 'failed' && next.error && next.error !== snapshot.error) {
      const monotonicMs = globalThis.performance?.now?.() ?? 0
      logs.appendLog({
        monotonicUs: monotonicMs * 1_000,
        wallTime: (globalThis.performance?.timeOrigin ?? Date.now() - monotonicMs) + monotonicMs,
        severity: 'error', category: 'RUNTIME', thread: 'browser', message: next.error,
      })
    }
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

    publish({ state: 'booting', error: null, input: null, storage: null, files: null, hostFolder: null, midi: null, trace: null })
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
      const midi = module.midi ? createMidiStore(module.midi, options.midiOptions) : null
      const trace = module.trace ? createTraceStore(module.trace, {
        ...options.traceOptions,
        metadata: { ...options.traceOptions?.metadata, build: buildMetadata },
      }) : null
      module.midiStore = midi
      module.traceStore = trace
      publish({ state: 'ready', buildMetadata, frameContent, input: module.input ?? null, audio, storage: module.storage ?? null, files: module.files ?? null, hostFolder: module.hostFolder ?? null, midi, trace })
      startNativeStateMonitor(module)
      return module
    } catch (error) {
      stopNativeStateMonitor()
      const failedModule = module
      let cleanupError = null
      try {
        if (failedModule) await stopNow()
      } catch (caught) {
        cleanupError = caught
      }
      const primaryMessage = error instanceof Error ? error.message : String(error)
      const message = cleanupError
        ? `${primaryMessage}; cleanup failed: ${cleanupError instanceof Error ? cleanupError.message : String(cleanupError)}`
        : primaryMessage
      const retainedModule = cleanupError && stopStage?.module === failedModule ? failedModule : null
      publish({
        state: 'failed', error: message, frameContent: 'unavailable',
        input: null, audio: null,
        storage: retainedModule?.storage ?? null,
        files: retainedModule?.files ?? null,
        hostFolder: retainedModule?.hostFolder ?? null,
        midi: null,
      })
      throw error
    }
  }

  async function stopNow() {
    if (!module && !stopStage) return
    stopNativeStateMonitor()
    const stage = stopStage ?? {
      module,
      inputReleased: false,
      audioStopped: false,
      midiStopped: false,
      traceStopped: false,
      hostFolderIdle: false,
      shutdownRequested: false,
      cppStopped: false,
      flushed: false,
    }
    const stoppingModule = stage.module
    const recoveryHandles = {
      storage: stoppingModule.storage ?? null,
      files: stoppingModule.files ?? null,
      hostFolder: stoppingModule.hostFolder ?? null,
    }
    publish({ state: 'stopping', input: null, audio: null, ...recoveryHandles, midi: null })
    if (!stage.inputReleased) {
      try {
        stoppingModule.input?.releaseAllActions?.()
        stage.inputReleased = true
      } catch (error) {
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, ...recoveryHandles, midi: null })
        throw error
      }
    }
    if (!stage.audioStopped && stoppingModule.audioStore) {
      try {
        await stoppingModule.audioStore.stop()
        stage.audioStopped = true
      } catch (error) {
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, ...recoveryHandles, midi: null })
        throw error
      }
    } else if (!stage.audioStopped) {
      stage.audioStopped = true
    }
    if (!stage.midiStopped) {
      try {
        if (typeof stoppingModule.midiStore?.stop === 'function') {
          await stoppingModule.midiStore.stop()
        }
        stage.midiStopped = true
      } catch (error) {
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, ...recoveryHandles, midi: null })
        throw error
      }
    }
    if (!stage.hostFolderIdle) {
      try {
        if (typeof stoppingModule.hostFolder?.waitForIdle === 'function') {
          await stoppingModule.hostFolder.waitForIdle()
        }
        stage.hostFolderIdle = true
      } catch (error) {
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, ...recoveryHandles, midi: null })
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
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, ...recoveryHandles, midi: null })
        throw error
      }
    }
    if (!stage.traceStopped) {
      try {
        stoppingModule.traceStore?.dispose?.()
        stage.traceStopped = true
      } catch (error) {
        stopStage = stage
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error) })
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
        publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, ...recoveryHandles, midi: null })
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
      publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), buildMetadata: null, input: null, audio: null, ...recoveryHandles, midi: null, trace: null })
      throw error
    }
    stopStage = null
    module = null
    stopNativeStateMonitor()
    publish({ state: 'idle', error: null, buildMetadata: null, frameContent: 'unavailable', input: null, audio: null, storage: null, files: null, hostFolder: null, midi: null, trace: null })
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
