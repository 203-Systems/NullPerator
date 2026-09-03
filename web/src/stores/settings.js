import { TRACE_ALL_MASK } from '../trace/registry.js'

export const SETTINGS_VERSION = 4
export const SETTINGS_STORAGE_KEY = 'picotracker.wasm.settings.v4'
export const LEGACY_SETTINGS_STORAGE_KEYS = Object.freeze([
  'picotracker.wasm.settings.v3',
  'picotracker.wasm.settings.v2',
  'picotracker.wasm.settings.v1',
])
export const AUDIO_BUFFER_OPTIONS = Object.freeze([512, 1024, 2048, 4096, 8192])
export const DISPLAY_SCALE_OPTIONS = Object.freeze(['fit', '1', '1.5', '2', '3', '4'])

const finite = (value, fallback) => Number.isFinite(Number(value)) ? Number(value) : fallback
const closest = (options, value, fallback) => options.reduce((best, option) =>
  Math.abs(option - value) < Math.abs(best - value) ? option : best, fallback)
export const DEFAULT_SETTINGS = Object.freeze({
  version: SETTINGS_VERSION,
  displayScale: 'fit',
  audioBufferFrames: 4096,
  outputVolume: 100,
  traceMask: TRACE_ALL_MASK,
  lowLatencyAudio: true,
  // Developer tools are an explicit, additive preference. Viewport changes
  // only affect layout and must never change which capabilities a user sees.
  developerMode: false,
})

export function migrateSettings(candidate) {
  const source = candidate && typeof candidate === 'object' ? candidate : {}
  const displayScale = DISPLAY_SCALE_OPTIONS.includes(String(source.displayScale))
    ? String(source.displayScale) : DEFAULT_SETTINGS.displayScale
  const requestedFrames = finite(source.audioBufferFrames, DEFAULT_SETTINGS.audioBufferFrames)
  const audioBufferFrames = closest(AUDIO_BUFFER_OPTIONS, requestedFrames, DEFAULT_SETTINGS.audioBufferFrames)
  const outputVolume = Math.round(Math.min(100, Math.max(0,
    finite(source.outputVolume ?? source.volume, DEFAULT_SETTINGS.outputVolume))))
  const traceMask = (finite(source.traceMask, DEFAULT_SETTINGS.traceMask) >>> 0) & TRACE_ALL_MASK
  return {
    version: SETTINGS_VERSION,
    displayScale,
    audioBufferFrames,
    outputVolume,
    traceMask,
    lowLatencyAudio: source.version >= 3
      ? Boolean(source.lowLatencyAudio)
      : DEFAULT_SETTINGS.lowLatencyAudio,
    developerMode: source.version >= SETTINGS_VERSION && typeof source.developerMode === 'boolean'
      ? source.developerMode
      : DEFAULT_SETTINGS.developerMode,
  }
}

function cloneSettings(value) { return { ...value } }

function load(storage) {
  let candidate = null
  try {
    const stored = storage?.getItem?.(SETTINGS_STORAGE_KEY)
      ?? LEGACY_SETTINGS_STORAGE_KEYS.map((key) => storage?.getItem?.(key)).find((value) => value !== null)
    candidate = JSON.parse(stored ?? 'null')
  } catch { /* use defaults */ }
  return migrateSettings(candidate)
}

export function createSettingsStore(options = {}) {
  const storage = options.localStorage ?? globalThis.localStorage
  const listeners = new Set()
  let value = migrateSettings(options.initial ?? load(storage))
  let snapshot

  const persist = () => {
    try { storage?.setItem?.(SETTINGS_STORAGE_KEY, JSON.stringify(value)) }
    catch { /* private/embedded contexts may reject localStorage writes */ }
  }
  const publish = () => {
    snapshot = Object.freeze({ ...value })
    for (const listener of listeners) listener(snapshot)
  }
  publish()

  return Object.freeze({
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    snapshot: () => snapshot,
    update(patch) {
      const next = typeof patch === 'function' ? patch(cloneSettings(value)) : patch
      value = migrateSettings({ ...value, ...(next ?? {}) })
      persist(); publish()
      return snapshot
    },
    reset() {
      value = migrateSettings(DEFAULT_SETTINGS)
      persist(); publish()
      return snapshot
    },
  })
}

export const settingsStore = createSettingsStore()
