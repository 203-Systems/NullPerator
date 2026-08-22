export const ACTIONS = Object.freeze({
  left: 0,
  down: 1,
  right: 2,
  up: 3,
  alt: 4,
  edit: 5,
  enter: 6,
  nav: 7,
  play: 8,
  select: 9,
  power: 10,
})

const KEY_MAP_STORAGE_KEY = 'picotracker.wasm.key-map.v1'

function entry(action, ...bindings) {
  return Object.freeze({ action, bindings: Object.freeze(bindings.map((binding) => Object.freeze(binding))) })
}

// Restored from the historical SDL desktop mapping. Select and Power were
// added after that adapter, so their unclaimed browser defaults are explicit.
export const DEFAULT_KEY_MAP = Object.freeze({
  left: entry(ACTIONS.left, ['ArrowLeft']),
  down: entry(ACTIONS.down, ['ArrowDown']),
  right: entry(ACTIONS.right, ['ArrowRight']),
  up: entry(ACTIONS.up, ['ArrowUp']),
  alt: entry(ACTIONS.alt, ['ControlRight']),
  edit: entry(ACTIONS.edit, ['KeyS']),
  enter: entry(ACTIONS.enter, ['KeyA']),
  nav: entry(ACTIONS.nav, ['ControlLeft']),
  play: entry(ACTIONS.play, ['Space']),
  select: entry(ACTIONS.select, ['KeyX']),
  power: entry(ACTIONS.power, ['KeyP']),
})

// The default persisted schema; createInputStore owns the live, remappable copy.
export const KeyMap = DEFAULT_KEY_MAP

function cloneMap(map) {
  return Object.fromEntries(
    Object.entries(map).map(([name, value]) => [
      name,
      { action: value.action, bindings: value.bindings.map((binding) => [...binding]) },
    ]),
  )
}

function normalizeMap(candidate) {
  const map = cloneMap(DEFAULT_KEY_MAP)
  if (!candidate || typeof candidate !== 'object') return map
  for (const [name, fallback] of Object.entries(DEFAULT_KEY_MAP)) {
    const value = candidate[name]
    if (!value || value.action !== fallback.action || !Array.isArray(value.bindings)) continue
    const bindings = value.bindings
      .filter((binding) => Array.isArray(binding) && binding.length > 0)
      .map((binding) => binding.filter((code) => typeof code === 'string' && code.length > 0))
      .filter((binding) => binding.length > 0)
    if (bindings.length > 0) map[name] = { action: fallback.action, bindings }
  }
  return map
}

function loadMap(storage) {
  try {
    return normalizeMap(JSON.parse(storage?.getItem?.(KEY_MAP_STORAGE_KEY) ?? 'null'))
  } catch {
    return cloneMap(DEFAULT_KEY_MAP)
  }
}

function actionName(action) {
  if (typeof action === 'string' && Object.hasOwn(ACTIONS, action)) return action
  return Object.entries(ACTIONS).find(([, value]) => value === action)?.[0] ?? null
}

export function createInputStore(bridge, options = {}) {
  const storage = options.localStorage ?? globalThis.localStorage
  let keyMap = normalizeMap(options.keyMap ?? loadMap(storage))
  const heldSources = new Map()
  const heldKeys = new Set()
  const activeBindings = new Set()
  let detach = null

  function actionId(action) {
    const name = actionName(action)
    return name === null ? null : keyMap[name].action
  }

  function press(action, source = 'direct') {
    const name = actionName(action)
    if (name === null) return false
    const sources = heldSources.get(name) ?? new Set()
    if (sources.has(source)) return true
    const wasHeld = sources.size > 0
    sources.add(source)
    heldSources.set(name, sources)
    if (!wasHeld) bridge?.pressAction?.(actionId(name))
    return true
  }

  function release(action, source = 'direct') {
    const name = actionName(action)
    if (name === null) return false
    const sources = heldSources.get(name)
    if (!sources?.delete(source)) return false
    if (sources.size === 0) {
      heldSources.delete(name)
      bridge?.releaseAction?.(actionId(name))
    }
    return true
  }

  function releaseAll() {
    heldSources.clear()
    heldKeys.clear()
    activeBindings.clear()
    bridge?.releaseAllActions?.()
  }

  function bindingsForCode(code) {
    return Object.entries(keyMap).flatMap(([name, value]) =>
      value.bindings.map((binding, index) => ({ name, binding, source: `keyboard:${name}:${index}` })),
    ).filter(({ binding }) => binding.includes(code))
  }

  function synchronizeBindings() {
    for (const [name, value] of Object.entries(keyMap)) {
      value.bindings.forEach((binding, index) => {
        const source = `keyboard:${name}:${index}`
        const active = activeBindings.has(source)
        const complete = binding.every((code) => heldKeys.has(code))
        if (complete && !active) {
          activeBindings.add(source)
          press(name, source)
        } else if (!complete && active) {
          activeBindings.delete(source)
          release(name, source)
        }
      })
    }
  }

  function handleKeyDown(event) {
    const consumed = bindingsForCode(event.code).length > 0
    if (consumed) event.preventDefault?.()
    if (event.repeat || !consumed) return consumed
    heldKeys.add(event.code)
    synchronizeBindings()
    return true
  }

  function handleKeyUp(event) {
    const consumed = bindingsForCode(event.code).length > 0
    if (consumed) event.preventDefault?.()
    heldKeys.delete(event.code)
    synchronizeBindings()
    return consumed
  }

  function attach({ target = globalThis.window, document = globalThis.document, isActive = () => true } = {}) {
    detach?.()
    const onKeyDown = (event) => isActive() && handleKeyDown(event)
    const onKeyUp = (event) => isActive() && handleKeyUp(event)
    const onBlur = () => releaseAll()
    const onVisibilityChange = () => {
      if (document?.visibilityState !== 'visible') releaseAll()
    }
    target?.addEventListener?.('keydown', onKeyDown)
    target?.addEventListener?.('keyup', onKeyUp)
    target?.addEventListener?.('blur', onBlur)
    document?.addEventListener?.('visibilitychange', onVisibilityChange)
    detach = () => {
      target?.removeEventListener?.('keydown', onKeyDown)
      target?.removeEventListener?.('keyup', onKeyUp)
      target?.removeEventListener?.('blur', onBlur)
      document?.removeEventListener?.('visibilitychange', onVisibilityChange)
      releaseAll()
      detach = null
    }
    return detach
  }

  return Object.freeze({
    press,
    release,
    releaseAll,
    handleKeyDown,
    handleKeyUp,
    attach,
    getHeldActions: () => [...heldSources.keys()],
    getKeyMap: () => cloneMap(keyMap),
    setKeyMap(nextMap) {
      releaseAll()
      keyMap = normalizeMap(nextMap)
      try {
        storage?.setItem?.(KEY_MAP_STORAGE_KEY, JSON.stringify(keyMap))
      } catch {
        // Storage can be unavailable in private or embedded browsing contexts.
      }
    },
  })
}
