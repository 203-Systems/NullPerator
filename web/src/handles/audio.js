const METRICS_WORDS = 13
const METRICS_VERSION = 4
const METRICS_SIZE = METRICS_WORDS * Uint32Array.BYTES_PER_ELEMENT

function decodeMessage(module, pointer) {
  return pointer ? module.UTF8ToString(pointer) : ''
}

function readSharedWords(module, pointer, count) {
  if (!pointer || !module.HEAPU32) return null
  const start = pointer >>> 2
  const words = new Uint32Array(count)
  for (let index = 0; index < count; index += 1) {
    words[index] = Atomics.load(module.HEAPU32, start + index)
  }
  return words
}

function readStableMetrics(module, pointer) {
  if (!pointer || !module.HEAPU32) return null
  const sequence = pointer >>> 2
  for (;;) {
    const before = Atomics.load(module.HEAPU32, sequence)
    if ((before & 1) !== 0) continue
    const words = new Uint32Array(METRICS_WORDS)
    for (let index = 0; index < METRICS_WORDS; index += 1) {
      words[index] = Atomics.load(module.HEAPU32, sequence + 1 + index)
    }
    const after = Atomics.load(module.HEAPU32, sequence)
    if (before === after && (after & 1) === 0) return words
  }
}

function decodeSharedError(module, pointer) {
  const words = readSharedWords(module, pointer, 40)
  if (!words) return null
  const bytes = new Uint8Array(words.length * Uint32Array.BYTES_PER_ELEMENT)
  for (let index = 0; index < words.length; index += 1) {
    const word = words[index]
    bytes[index * 4] = word & 0xff
    bytes[index * 4 + 1] = (word >>> 8) & 0xff
    bytes[index * 4 + 2] = (word >>> 16) & 0xff
    bytes[index * 4 + 3] = word >>> 24
  }
  const end = bytes.indexOf(0)
  return new TextDecoder().decode(bytes.subarray(0, end < 0 ? bytes.length : end))
}

export function createAudioBridge(module, capability = { available: true, reason: null }) {
  const snapshot = module.__picoTrackerAudioSnapshot
  const sharedMetrics = snapshot?.metrics >>> 0
  const sharedError = snapshot?.error >>> 0
  return {
    capability,
    unlockAudio() { return module._PicoTracker_Wasm_UnlockAudio() },
    stopAudio() { module._PicoTracker_Wasm_StopAudio?.() },
    getAudioState() {
      // metrics snapshot word 0 is the seqlock generation; state is payload
      // word 2 immediately after it.
      if (sharedMetrics && module.HEAPU32) return Atomics.load(module.HEAPU32, (sharedMetrics >>> 2) + 3)
      return module._PicoTracker_Wasm_GetAudioState()
    },
    getAudioError() {
      if (sharedError && module.HEAPU32) return decodeSharedError(module, sharedError)
      return decodeMessage(module, module._PicoTracker_Wasm_GetAudioError?.())
    },
    getAudioMetrics() {
      const pointer = sharedMetrics || module._PicoTracker_Wasm_GetAudioMetrics?.()
      const words = sharedMetrics
        ? readStableMetrics(module, pointer)
        : module.HEAPU32?.slice(pointer >>> 2, (pointer >>> 2) + METRICS_WORDS)
      if (!words || words.length !== METRICS_WORDS) return null
      return {
        version: words[0], size: words[1], state: words[2], ringFillFrames: words[3],
        ringCapacityFrames: words[4], renderMicros: words[5],
        underrunFrames: words[6], overrunFrames: words[7], sourceRate: words[8],
        destinationRate: words[9], callbackCount: words[10],
        setupPhase: words[11], unlockOnBrowserMainThread: words[12],
      }
    },
  }
}

export const audioMetricsContract = Object.freeze({ version: METRICS_VERSION, size: METRICS_SIZE })
