import { createInputBridge } from './input.js'
import { createAudioBridge } from './audio.js'

const defaultModuleUrl = '/wasm/picotracker.js'
const frameRgbaLength = 240 * 240 * 4

function toMessage(module, pointer) {
  if (!pointer) return ''
  return module.UTF8ToString(pointer)
}

export async function createRuntime(options = {}) {
  const {
    canvas = globalThis.document?.querySelector?.('#picotracker-canvas'),
    moduleFactory,
    moduleUrl = defaultModuleUrl,
    locateFile = (path) => new URL(path, new URL(moduleUrl, window.location.href)).href,
  } = options

  const factory = moduleFactory ?? (await import(/* @vite-ignore */ moduleUrl)).default
  const audioWorkletEnabled = options.audioWorkletEnabled ??
    new URLSearchParams(globalThis.location?.search ?? '').get('audio') === 'worklet'
  const audioCapability = audioWorkletEnabled
    ? { available: true, mode: 'worklet', reason: null }
    : { available: false, mode: 'disabled', reason: 'Audio disabled; enable low-latency audio and reload.' }
  let audioBootstrapped = false
  const moduleOptions = {
    canvas,
    locateFile,
    onRuntimeInitialized() {
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
      const copyWords = (pointer, count) => {
        if (!pointer || !this.HEAPU32) return null
        return Array.from(this.HEAPU32.slice(pointer >>> 2, (pointer >>> 2) + count))
      }
      const descriptorWords = copyWords(this._PicoTracker_Wasm_GetBrowserSnapshots?.() ?? 0, 7)
      if (descriptorWords && (descriptorWords[0] !== 1 || descriptorWords[1] !== 28)) {
        throw new Error('WASM browser snapshot descriptor is incompatible')
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
      bootstrap.call(this)
      audioBootstrapped = true
    },
  }
  const module = await factory(moduleOptions)

  async function waitForShutdown() {
    const deadline = Date.now() + (options.shutdownTimeoutMs ?? 5_000)
    while (true) {
      const state = module._PicoTracker_Wasm_GetState()
      if (state === 4) return
      if (state === 3) {
        throw new Error(toMessage(module, module._PicoTracker_Wasm_GetLastError()) || 'C++ shutdown failed')
      }
      if (Date.now() >= deadline) throw new Error('Timed out waiting for C++ shutdown')
      await new Promise((resolve) => setTimeout(resolve, 10))
    }
  }

  return {
    module,
    input: createInputBridge(module),
    audio: createAudioBridge(module, audioCapability),
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
    async requestShutdown() {
      module._PicoTracker_Wasm_RequestShutdown()
      await waitForShutdown()
    },
    async terminate() {
      if (!module.PThread) throw new Error('Emscripten PThread runtime API is unavailable')
      module.PThread.terminateAllThreads()
      await new Promise((resolve) => setTimeout(resolve, 0))
    },
  }
}
