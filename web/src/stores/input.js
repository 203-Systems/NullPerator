export const ACTIONS = Object.freeze({
  left: 0,
  down: 1,
  right: 2,
  up: 3,
  alt: 4,
  edit: 5,
  enter: 6,
  start: 7,
  select: 9,
  power: 10,
})

function entry(action, ...bindings) {
  return Object.freeze({ action, bindings: Object.freeze(bindings.map((binding) => Object.freeze(binding))) })
}

// Operator's primary eight-key layout keeps every common PicoTracker chord
// under one hand pair: WASD navigation, J/K face buttons, and X/C modifiers.
// Arrow keys remain aliases for users coming from the historical SDL build.
export const DEFAULT_KEY_MAP = Object.freeze({
  left: entry(ACTIONS.left, ['KeyA'], ['ArrowLeft']),
  down: entry(ACTIONS.down, ['KeyS'], ['ArrowDown']),
  right: entry(ACTIONS.right, ['KeyD'], ['ArrowRight']),
  up: entry(ACTIONS.up, ['KeyW'], ['ArrowUp']),
  alt: entry(ACTIONS.alt, ['KeyX']),
  edit: entry(ACTIONS.edit, ['KeyJ']),
  enter: entry(ACTIONS.enter, ['KeyK']),
  // START is one physical action. Firmware UI2 owns tap/hold/chord semantics;
  // the browser must not synthesize separate NAV and PLAY actions.
  start: entry(ACTIONS.start, ['KeyC']),
  select: entry(ACTIONS.select),
  power: entry(ACTIONS.power),
})

// Node's fixed browser layout. It deliberately is not user-remappable.
export const KeyMap = DEFAULT_KEY_MAP

function actionName(action) {
  if (typeof action === 'string' && Object.hasOwn(ACTIONS, action)) return action
  return Object.entries(ACTIONS).find(([, value]) => value === action)?.[0] ?? null
}

export function createInputStore(bridge) {
  const keyMap = DEFAULT_KEY_MAP
  const listeners = new Set()
  const heldSources = new Map()
  const heldKeys = new Set()
  const activeBindings = new Set()
  let detach = null
  const publish = () => {
    const snapshot = Object.freeze([...heldSources.keys()])
    for (const listener of listeners) listener(snapshot)
  }

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
    if (!wasHeld) { bridge?.pressAction?.(actionId(name)); publish() }
    return true
  }

  function release(action, source = 'direct') {
    const name = actionName(action)
    if (name === null) return false
    const sources = heldSources.get(name)
    if (!sources?.has(source)) return false
    sources.delete(source)
    if (sources.size === 0) {
      heldSources.delete(name)
      bridge?.releaseAction?.(actionId(name))
      publish()
    }
    return true
  }

  function releaseAll() {
    heldSources.clear()
    heldKeys.clear()
    activeBindings.clear()
    bridge?.releaseAllActions?.()
    publish()
  }

  function pressStart(source = 'start') {
    return press('start', source)
  }

  function releaseStart(source = 'start') {
    return release('start', source)
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
    let attached = true
    const onKeyDown = (event) => attached && isActive(event) && handleKeyDown(event)
    const onKeyUp = (event) => attached && isActive(event) && handleKeyUp(event)
    const onBlur = () => { if (attached) releaseAll() }
    const onPageHide = () => { if (attached) releaseAll() }
    const onVisibilityChange = () => {
      if (attached && document?.visibilityState !== 'visible') releaseAll()
    }
    target?.addEventListener?.('keydown', onKeyDown)
    target?.addEventListener?.('keyup', onKeyUp)
    target?.addEventListener?.('blur', onBlur)
    target?.addEventListener?.('pagehide', onPageHide)
    document?.addEventListener?.('visibilitychange', onVisibilityChange)
    detach = () => {
      if (!attached) return
      attached = false
      target?.removeEventListener?.('keydown', onKeyDown)
      target?.removeEventListener?.('keyup', onKeyUp)
      target?.removeEventListener?.('blur', onBlur)
      target?.removeEventListener?.('pagehide', onPageHide)
      document?.removeEventListener?.('visibilitychange', onVisibilityChange)
      releaseAll()
      detach = null
    }
    return detach
  }

  return Object.freeze({
    subscribe(listener) { listeners.add(listener); listener(Object.freeze([...heldSources.keys()])); return () => listeners.delete(listener) },
    press,
    release,
    pressStart,
    releaseStart,
    releaseAll,
    handleKeyDown,
    handleKeyUp,
    attach,
    getHeldActions: () => [...heldSources.keys()],
  })
}
