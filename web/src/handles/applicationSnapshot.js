const APPLICATION_SNAPSHOT_VERSION = 2
const APPLICATION_SNAPSHOT_BYTES = 56
const APPLICATION_SNAPSHOT_WORDS = APPLICATION_SNAPSHOT_BYTES / 4
const APPLICATION_PROJECT_NAME_CAPACITY = 16
const utf8Decoder = new TextDecoder()

export function readApplicationSnapshot(module) {
  const pointer = module?.__picoTrackerApplicationSnapshot?.data ?? 0
  const heap = module?.HEAPU32
  if (!pointer || !heap) throw new Error('WASM application snapshot is unavailable')
  const base = pointer >>> 2

  for (let attempt = 0; attempt < 1_000; attempt += 1) {
    const before = Atomics.load(heap, base) >>> 0
    if (before & 1) continue
    const words = new Uint32Array(APPLICATION_SNAPSHOT_WORDS)
    for (let index = 1; index < APPLICATION_SNAPSHOT_WORDS; index += 1) {
      words[index] = Atomics.load(heap, base + index) >>> 0
    }
    const after = Atomics.load(heap, base) >>> 0
    if (before !== after || (after & 1)) continue
    if (words[1] !== APPLICATION_SNAPSHOT_VERSION || words[2] !== APPLICATION_SNAPSHOT_BYTES) {
      throw new Error('WASM application snapshot is incompatible')
    }

    const nameLength = words[7]
    if (nameLength > APPLICATION_PROJECT_NAME_CAPACITY) {
      throw new Error('WASM application snapshot project name is invalid')
    }
    const nameBytes = new Uint8Array(nameLength)
    for (let index = 0; index < nameLength; index += 1) {
      nameBytes[index] = (words[8 + (index >>> 2)] >>> ((index & 3) * 8)) & 0xFF
    }
    return Object.freeze({
      sequence: after,
      projectName: utf8Decoder.decode(nameBytes),
      tempo: words[3],
      sampleCount: words[4],
      playerRunning: words[5] !== 0,
      masterLevel: words[6],
      playingTrackMask: words[13],
    })
  }
  throw new Error('WASM application snapshot remained busy')
}
