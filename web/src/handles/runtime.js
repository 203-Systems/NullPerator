import { createInputBridge } from './input.js'
import { createAudioBridge } from './audio.js'
import { createFilesHandle } from './files.js'
import { createMidiBridge } from './midi.js'
import { createLogsHandle } from './logs.js'
import { createTraceBridge } from './trace.js'
import { createViewDiagnostics } from './views.js'
import { createLogStore } from '../stores/logs.js'
import { normalizePersistentPath } from './filesystem.js'
import { createHostFolderManager } from '../storage/hostFolder.js'
import { createStorageCoordinator, persistentFsPreRun } from '../stores/storage.js'
import { settingsStore } from '../stores/settings.js'

const defaultModuleUrl = '/wasm/picotracker.js'
const frameRgbaLength = 240 * 240 * 4

function toMessage(module, pointer) {
  if (!pointer) return ''
  return module.UTF8ToString(pointer)
}

function createStorageAcceptanceHandle(module, storage) {
  const pathFor = (path) => normalizePersistentPath(path)
  const originalSyncfs = typeof module?.FS?.syncfs === 'function'
    ? module.FS.syncfs.bind(module.FS)
    : null
  let nextSyncFailure = null
  if (originalSyncfs) {
    module.FS.syncfs = (populate, callback) => {
      if (!populate && nextSyncFailure) {
        const failure = nextSyncFailure
        nextSyncFailure = null
        queueMicrotask(() => callback(failure))
        return
      }
      originalSyncfs(populate, callback)
    }
  }
  return Object.freeze({
    write(path, bytes) {
      module.FS.writeFile(pathFor(path), new Uint8Array(bytes))
    },
    writePendingMutation(path, bytes) {
      module.FS.writeFile(pathFor(path), new Uint8Array(bytes))
      if (typeof module.picoTrackerStorageMutation !== 'function') {
        throw new Error('WASM storage mutation callback is unavailable')
      }
      // Match the native WasmStorageBridge notification without awaiting the
      // resulting IDBFS transaction. Recovery acceptance uses this to inject a
      // fatal while the mutation is still only present in MEMFS/in flight.
      module.picoTrackerStorageMutation()
    },
    read(path) {
      return Array.from(module.FS.readFile(pathFor(path)))
    },
    exists(path) {
      try {
        module.FS.stat(pathFor(path))
        return true
      } catch {
        return false
      }
    },
    flush() {
      return storage.flushNow('e2e-fixture')
    },
    snapshot() {
      return storage.snapshot()
    },
    awaitDurable(generation) {
      return storage.awaitDurable(generation)
    },
    failNextSync(message = 'Injected IDBFS quota failure') {
      if (!originalSyncfs) throw new Error('IDBFS sync injection is unavailable')
      if (nextSyncFailure) throw new Error('An IDBFS sync failure is already pending')
      nextSyncFailure = new Error(String(message))
    },
  })
}

function createMidiAcceptanceHandle(module) {
  const snapshotExport = module._PicoTracker_Wasm_MidiDiagnosticSnapshot
  const outputExport = module._PicoTracker_Wasm_MidiDiagnosticOutput
  if (typeof snapshotExport !== 'function' || typeof outputExport !== 'function' ||
      !(module.HEAPU8 instanceof Uint8Array)) {
    throw new Error('WASM MIDI acceptance diagnostics are unavailable')
  }
  return Object.freeze({
    snapshot() {
      const pointer = snapshotExport() >>> 0
      if (!pointer) throw new Error('WASM MIDI diagnostic snapshot is unavailable')
      const view = new DataView(module.HEAPU8.buffer, pointer, 32)
      if (view.getUint32(0, true) !== 1 || view.getUint32(4, true) !== 32) {
        throw new Error('WASM MIDI diagnostic snapshot is incompatible')
      }
      return Object.freeze({
        processedInputBytes: Number(view.getBigUint64(8, true)),
        lastInputTimestamp: view.getFloat64(16, true),
        lastInputByte: view.getUint32(24, true),
        inputResetGeneration: view.getUint32(28, true),
      })
    },
    emitOutput(bytes, delayMilliseconds = 0) {
      const packet = Uint8Array.from(bytes ?? [])
      const delay = Number(delayMilliseconds)
      if (packet.length < 1 || packet.length > 3 || packet[0] < 0x80) {
        throw new TypeError('A one-to-three byte MIDI message is required')
      }
      if (!Number.isInteger(delay) || delay < 0 || delay > 1_000) {
        throw new RangeError('MIDI diagnostic delay must be 0..1000 milliseconds')
      }
      return outputExport(packet[0], packet[1] ?? 0, packet[2] ?? 0, delay) === 1
    },
  })
}

export async function createRuntime(options = {}) {
  const {
    canvas = globalThis.document?.querySelector?.('#picotracker-canvas'),
    moduleFactory,
    midiBridgeFactory = createMidiBridge,
    logsHandleFactory = createLogsHandle,
    traceBridgeFactory = createTraceBridge,
    moduleUrl = defaultModuleUrl,
    locateFile = (path) => new URL(path, new URL(moduleUrl, window.location.href)).href,
  } = options

  const runtimeFailureTest = new URLSearchParams(globalThis.location?.search ?? '')
    .get('runtime-fail-test') === '1'
  const runtimeFailureInjected = globalThis.sessionStorage?.getItem?.(
    'picotracker.runtime-failure-injected',
  ) === '1'
  if (runtimeFailureTest && !runtimeFailureInjected) {
    globalThis.sessionStorage?.setItem?.('picotracker.runtime-failure-injected', '1')
    throw new Error('Injected one-shot WASM load failure')
  }

  const factory = moduleFactory ?? (await import(/* @vite-ignore */ moduleUrl)).default
  const workbenchSettings = options.settings ?? settingsStore.snapshot()
  const logs = options.logs ?? createLogStore(options.logOptions)
  const exposeLogsForTesting = new URLSearchParams(globalThis.location?.search ?? '').get('logs-test') === '1'
  let logsTestHandle = null
  if (!exposeLogsForTesting) delete globalThis.__picoTrackerLogsTest
  const runtimeConsole = options.console ?? globalThis.console
  const appendConsole = (severity, value) => {
    const monotonicMs = globalThis.performance?.now?.() ?? 0
    logs.appendLog({
      monotonicUs: monotonicMs * 1_000,
      wallTime: (globalThis.performance?.timeOrigin ?? Date.now() - monotonicMs) + monotonicMs,
      severity, category: 'EMSCRIPTEN', thread: 'browser', message: String(value),
    })
  }
  // This is deliberately an acceptance-only escape hatch. It is not a file
  // browser or a production filesystem API (those belong to later tasks).
  const exposeStorageForTesting = new URLSearchParams(globalThis.location?.search ?? '').get('storage-test') === '1'
  if (!exposeStorageForTesting) delete globalThis.__picoTrackerStorageTest
  const exposeViewsForTesting = new URLSearchParams(globalThis.location?.search ?? '').get('views-test') === '1'
  if (!exposeViewsForTesting) delete globalThis.__picoTrackerViewsTest
  const exposeMidiForTesting = new URLSearchParams(globalThis.location?.search ?? '').get('midi-test') === '1'
  if (!exposeMidiForTesting) delete globalThis.__picoTrackerMidiTest
  const exposeRuntimeForTesting = new URLSearchParams(globalThis.location?.search ?? '').get('runtime-test') === '1'
  if (!exposeRuntimeForTesting) delete globalThis.__picoTrackerRuntimeTest
  const audioParameter = new URLSearchParams(globalThis.location?.search ?? '').get('audio')
  const audioWorkletEnabled = options.audioWorkletEnabled ??
    (audioParameter === 'worklet' || (audioParameter === null && workbenchSettings.lowLatencyAudio))
  const audioCapability = audioWorkletEnabled
    ? { available: true, mode: 'worklet', reason: null }
    : { available: false, mode: 'disabled', reason: 'Audio disabled; enable low-latency audio and reload.' }
  let audioBootstrapped = false
  const storage = options.storage ?? createStorageCoordinator()
  const detachAttachedBeforeUnloadGuard = storage.attachBeforeUnloadGuard?.(
    options.beforeUnloadTarget ?? globalThis.window,
  )
  let beforeUnloadGuardAttached = typeof detachAttachedBeforeUnloadGuard === 'function'
  const detachBeforeUnloadGuard = () => {
    if (!beforeUnloadGuardAttached) return
    beforeUnloadGuardAttached = false
    detachAttachedBeforeUnloadGuard()
  }
  const moduleOptions = {
    canvas,
    locateFile,
    print(value) {
      appendConsole('info', value)
      runtimeConsole?.log?.(value)
    },
    printErr(value) {
      appendConsole('error', value)
      runtimeConsole?.error?.(value)
    },
    preRun: [persistentFsPreRun(storage)],
    onRuntimeInitialized() {
      const initialStorageError = storage.initializationError()
      if (initialStorageError) {
        // This hook runs after the balanced preRun dependency is released and
        // before Emscripten invokes proxied C main.
        throw initialStorageError
      }
      // Emscripten 6 modularized builds invoke this as a method of their
      // private Module rather than the caller-supplied options object.  This
      // is the last browser-main hook before PROXY_TO_PTHREAD starts C main.
      // Worklet setup is opt-in: a known-bad browser audio service must never
      // keep the normal UI runtime from starting.
      if (audioBootstrapped) return
      const bootstrap = audioCapability.available
        ? this._PicoTracker_Wasm_BootstrapAudio
        : this._PicoTracker_Wasm_MarkAudioUnavailable
      if (typeof bootstrap !== 'function') {
        throw new Error('WASM module does not export browser audio capability handling')
      }
      this._PicoTracker_Wasm_ConfigureAudio?.(
        workbenchSettings.audioBufferFrames >>> 0,
        Math.round(workbenchSettings.outputVolume * 65536 / 100) >>> 0,
      )
      const copyWords = (pointer, count) => {
        if (!pointer || !this.HEAPU32) return null
        return Array.from(this.HEAPU32.slice(pointer >>> 2, (pointer >>> 2) + count))
      }
      const descriptorPointer = this._PicoTracker_Wasm_GetBrowserSnapshots?.() ?? 0
      const descriptorHeader = copyWords(descriptorPointer, 2)
      let descriptorWords = null
      if (descriptorHeader) {
        const [version, byteSize] = descriptorHeader
        if (version === 1 && byteSize === 28) descriptorWords = copyWords(descriptorPointer, 7)
        else if (version === 2 && byteSize === 32) descriptorWords = copyWords(descriptorPointer, 8)
        else throw new Error('WASM browser snapshot descriptor is incompatible')
      }
      // Cache every browser-readable audio ABI address/value before
      // PROXY_TO_PTHREAD begins C main. Runtime UI polling must never call a
      // proxied export: browser-main and application pthread can otherwise
      // wait on each other during startup or a real worklet callback.
      this.__picoTrackerAudioSnapshot = {
        metrics: descriptorWords?.[4] ?? 0,
        error: descriptorWords?.[5] ?? 0,
        oracles: {
          44100: copyWords(descriptorWords?.[6] ?? 0, 6),
          48000: copyWords((descriptorWords?.[6] ?? 0) + 24, 6),
        },
      }
      this.__picoTrackerFrameSnapshot = {
        data: descriptorWords?.[2] ?? 0,
        sequence: descriptorWords?.[3] ?? 0,
      }
      this.__picoTrackerApplicationSnapshot = {
        data: descriptorWords?.[7] ?? 0,
      }
      // C++ mutation notifications are marshalled to browser main by the
      // adapter. They only schedule serialized IDBFS syncs and never block
      // the synchronous tracker filesystem API.
      this.picoTrackerStorageMutation = () => {
        void storage.requestSync('mutation').catch(() => {})
      }
      bootstrap.call(this)
      audioBootstrapped = true
    },
  }
  let module
  try {
    module = await factory(moduleOptions)
  } catch (error) {
    detachBeforeUnloadGuard()
    throw error
  }
  let logsHandle = null
  let storageTestHandle = null
  let viewsTestHandle = null
  let midiTestHandle = null
  let runtimeTestHandle = null
  let files = null
  let hostFolder = null
  let trace = null
  let input = null
  let audio = null
  let midi = null
  let diagnosticsQuiesced = false

  async function waitForShutdown() {
    const deadline = Date.now() + (options.shutdownTimeoutMs ?? 5_000)
    while (true) {
      const state = module._PicoTracker_Wasm_GetState()
      if (state === 4) return
      // Failed is an observable state while the application pthread performs
      // the same audio/platform teardown as an explicit Stop. Keep waiting for
      // its Stopped acknowledgement so Restart cannot terminate live workers.
      if (Date.now() >= deadline) throw new Error('Timed out waiting for C++ shutdown')
      await new Promise((resolve) => setTimeout(resolve, 10))
    }
  }

  function cleanupAcceptanceHandles() {
    if (globalThis.__picoTrackerStorageTest === storageTestHandle) {
      delete globalThis.__picoTrackerStorageTest
    }
    if (globalThis.__picoTrackerLogsTest === logsTestHandle) {
      delete globalThis.__picoTrackerLogsTest
    }
    if (globalThis.__picoTrackerViewsTest === viewsTestHandle) {
      delete globalThis.__picoTrackerViewsTest
    }
    if (globalThis.__picoTrackerMidiTest === midiTestHandle) {
      delete globalThis.__picoTrackerMidiTest
    }
    if (globalThis.__picoTrackerRuntimeTest === runtimeTestHandle) {
      delete globalThis.__picoTrackerRuntimeTest
    }
  }

  function quiesceDiagnostics() {
    if (diagnosticsQuiesced) return
    diagnosticsQuiesced = true
    try {
      // Keep the accumulated log store available to the recovery UI while
      // preventing its browser-main polling loop from touching failed WASM.
      logsHandle?.stop()
    } catch {
      // Quiescing is best-effort and must not mask a native fatal or prevent
      // the later authoritative worker/storage teardown sequence.
    }
  }

  async function terminateWorkers() {
    quiesceDiagnostics()
    if (!module.PThread) throw new Error('Emscripten PThread runtime API is unavailable')
    module.PThread.terminateAllThreads()
    await new Promise((resolve) => setTimeout(resolve, 0))
  }

  try {
    if (exposeLogsForTesting) {
      logsTestHandle = Object.freeze({ append: (record) => logs.appendLog(record) })
      globalThis.__picoTrackerLogsTest = logsTestHandle
    }
    logsHandle = logsHandleFactory && typeof module._PicoTracker_Wasm_LogDrain === 'function'
      ? logsHandleFactory(module, logs, options.logHandleOptions) : null
    logsHandle?.start()
    if (exposeStorageForTesting) {
      storageTestHandle = createStorageAcceptanceHandle(module, storage)
      globalThis.__picoTrackerStorageTest = storageTestHandle
    }
    if (exposeViewsForTesting) {
      viewsTestHandle = createViewDiagnostics(module)
      globalThis.__picoTrackerViewsTest = viewsTestHandle
    }
    if (exposeMidiForTesting) {
      midiTestHandle = createMidiAcceptanceHandle(module)
      globalThis.__picoTrackerMidiTest = midiTestHandle
    }
    if (exposeRuntimeForTesting) {
      runtimeTestHandle = Object.freeze({
        failCpp() {
          if (typeof module._PicoTracker_Wasm_DiagnosticFail !== 'function') {
            throw new Error('WASM runtime recovery diagnostic is unavailable')
          }
          module._PicoTracker_Wasm_DiagnosticFail()
        },
      })
      globalThis.__picoTrackerRuntimeTest = runtimeTestHandle
    }

    files = module.FS ? createFilesHandle(module, storage) : null
    hostFolder = files ? createHostFolderManager({ browser: files.createHostSyncEndpoint() }) : null
    // Permission restoration only queries the persisted handle. It never makes
    // a browser permission request during automatic runtime startup.
    void hostFolder?.restoreHostFolderHandle().catch(() => {})

    trace = traceBridgeFactory && typeof module._PicoTracker_Wasm_TraceDrain === 'function'
      ? traceBridgeFactory(module) : null
    storage.setTraceSink?.(trace)
    input = createInputBridge(module)
    audio = createAudioBridge(module, audioCapability)
    midi = midiBridgeFactory ? midiBridgeFactory(module) : null
  } catch (error) {
    quiesceDiagnostics()
    cleanupAcceptanceHandles()
    let cleanupError = null
    try {
      if (typeof module._PicoTracker_Wasm_GetState !== 'function') {
        throw new Error('WASM runtime state export is unavailable during cleanup')
      }
      await hostFolder?.waitForIdle?.()
      if (module._PicoTracker_Wasm_GetState() !== 4) {
        if (typeof module._PicoTracker_Wasm_RequestShutdown !== 'function') {
          throw new Error('WASM shutdown export is unavailable during cleanup')
        }
        module._PicoTracker_Wasm_RequestShutdown()
        await waitForShutdown()
      }
      await storage.flushNow?.('assembly-failure')
      storage.setTraceSink?.(null)
      await terminateWorkers()
      detachBeforeUnloadGuard()
    } catch (caught) {
      // If native shutdown or durability failed, keep the guard attached and
      // never terminate the pthread early. Reload remains the safe escape hatch.
      cleanupError = caught
    }
    if (cleanupError) {
      const primary = error instanceof Error ? error.message : String(error)
      const cleanup = cleanupError instanceof Error ? cleanupError.message : String(cleanupError)
      throw new Error(`${primary}; cleanup failed: ${cleanup}`, { cause: error })
    }
    throw error
  }

  return {
    module,
    storage,
    files,
    hostFolder,
    input,
    audio,
    midi,
    logs,
    trace,
    viewDiagnostics: viewsTestHandle,
    getBuildMetadataJson() {
      return toMessage(module, module._PicoTracker_Wasm_GetBuildMetadataJson())
    },
    getLastError() {
      return toMessage(module, module._PicoTracker_Wasm_GetLastError())
    },
    getState() {
      return module._PicoTracker_Wasm_GetState()
    },
    captureFrameRgba() {
      const snapshot = module.__picoTrackerFrameSnapshot
      const pointer = snapshot?.data >>> 0
      const sequence = snapshot?.sequence >>> 0
      if (!pointer || !sequence || !module.HEAPU8 || !module.HEAPU32) return null
      while (true) {
        const before = Atomics.load(module.HEAPU32, sequence >>> 2)
        if ((before & 1) !== 0) continue
        const frame = module.HEAPU8.slice(pointer, pointer + frameRgbaLength)
        const after = Atomics.load(module.HEAPU32, sequence >>> 2)
        if (before === after && (after & 1) === 0) return frame
      }
    },
    quiesceAfterFatal() {
      quiesceDiagnostics()
    },
    async requestShutdown() {
      module._PicoTracker_Wasm_RequestShutdown()
      await waitForShutdown()
    },
    async terminate() {
      try {
        await terminateWorkers()
      } finally {
        storage.setTraceSink?.(null)
        cleanupAcceptanceHandles()
      }
      // RuntimeManager reaches this point only after native shutdown and the
      // final persistent flush. A failed teardown deliberately leaves the
      // guard attached so a still-live dirty runtime remains protected.
      detachBeforeUnloadGuard()
    },
  }
}
