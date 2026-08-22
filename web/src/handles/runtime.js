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
  const module = await factory({ canvas, locateFile })

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
      const pointer = module._PicoTracker_Wasm_CaptureFrameRgba?.()
      if (!pointer || !module.HEAPU8) return null
      return module.HEAPU8.slice(pointer, pointer + frameRgbaLength)
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
