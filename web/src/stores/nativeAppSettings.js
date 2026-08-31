export const NATIVE_APP_SETTINGS_KEY = 'nullperator.native.settings.v1'

const DEFAULTS = Object.freeze({
  hideControlsWithGamepad: false,
})

function normalize(candidate) {
  const source = candidate && typeof candidate === 'object' ? candidate : {}
  return Object.freeze({
    hideControlsWithGamepad: Boolean(source.hideControlsWithGamepad),
  })
}

function load(storage) {
  try {
    return normalize(JSON.parse(storage?.getItem?.(NATIVE_APP_SETTINGS_KEY) ?? 'null'))
  } catch {
    return DEFAULTS
  }
}

export function createNativeAppSettingsStore(options = {}) {
  const storage = options.localStorage ?? globalThis.localStorage
  const listeners = new Set()
  let value = normalize(options.initial ?? load(storage))

  const publish = () => {
    for (const listener of listeners) listener(value)
  }
  const persist = () => {
    try { storage?.setItem?.(NATIVE_APP_SETTINGS_KEY, JSON.stringify(value)) }
    catch { /* Embedded web views may temporarily reject storage writes. */ }
  }

  return Object.freeze({
    subscribe(listener) {
      listeners.add(listener)
      listener(value)
      return () => listeners.delete(listener)
    },
    snapshot: () => value,
    update(patch) {
      const next = typeof patch === 'function' ? patch({ ...value }) : patch
      value = normalize({ ...value, ...(next ?? {}) })
      persist()
      publish()
      return value
    },
  })
}

export const nativeAppSettingsStore = createNativeAppSettingsStore()
