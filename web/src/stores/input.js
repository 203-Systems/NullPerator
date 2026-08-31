export const ACTIONS = Object.freeze({
  left: 0,
  down: 1,
  right: 2,
  up: 3,
  shift: 4,
  option: 5,
  edit: 6,
  play: 7,
  power: 10,
})

function entry(action, ...bindings) {
  return Object.freeze({ action, bindings: Object.freeze(bindings.map((binding) => Object.freeze(binding))) })
}

// M8 semantic layout over the Node A/B/SELECT/START hardware positions.
export const DEFAULT_KEY_MAP = Object.freeze({
  left: entry(ACTIONS.left, ['KeyA'], ['ArrowLeft']),
  down: entry(ACTIONS.down, ['KeyS'], ['ArrowDown']),
  right: entry(ACTIONS.right, ['KeyD'], ['ArrowRight']),
  up: entry(ACTIONS.up, ['KeyW'], ['ArrowUp']),
  shift: entry(ACTIONS.shift, ['KeyX']),
  option: entry(ACTIONS.option, ['KeyJ']),
  edit: entry(ACTIONS.edit, ['KeyK']),
  play: entry(ACTIONS.play, ['KeyC']),
  power: entry(ACTIONS.power),
})

// Node's fixed browser layout. It deliberately is not user-remappable.
export const KeyMap = DEFAULT_KEY_MAP
const REPEAT_ACTIONS = new Set(['left', 'down', 'right', 'up'])
const REPEAT_DELAY_MS = 500
const REPEAT_PERIOD_MS = 75

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
  const repeatTimers = new Map()
  let detach = null
  const publish = () => {
    const snapshot = Object.freeze([...heldSources.keys()])
    for (const listener of listeners) listener(snapshot)
  }

  function actionId(action) {
    const name = actionName(action)
    return name === null ? null : keyMap[name].action
  }

  function stopRepeat(name) {
    const timers = repeatTimers.get(name)
    if (!timers) return
    clearTimeout(timers.delay)
    if (timers.period !== null) clearInterval(timers.period)
    repeatTimers.delete(name)
  }

  function startRepeat(name) {
    if (!REPEAT_ACTIONS.has(name) || repeatTimers.has(name)) return
    const timers = { delay: null, period: null }
    timers.delay = setTimeout(() => {
      if (!heldSources.has(name)) return stopRepeat(name)
      bridge?.repeatAction?.(actionId(name))
      timers.period = setInterval(() => {
        if (!heldSources.has(name)) return stopRepeat(name)
        bridge?.repeatAction?.(actionId(name))
      }, REPEAT_PERIOD_MS)
    }, REPEAT_DELAY_MS)
    repeatTimers.set(name, timers)
  }

  function press(action, source = 'direct') {
    const name = actionName(action)
    if (name === null) return false
    const sources = heldSources.get(name) ?? new Set()
    if (sources.has(source)) return true
    const wasHeld = sources.size > 0
    sources.add(source)
    heldSources.set(name, sources)
    if (!wasHeld) {
      bridge?.pressAction?.(actionId(name))
      startRepeat(name)
      publish()
    }
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
      stopRepeat(name)
      bridge?.releaseAction?.(actionId(name))
      publish()
    }
    return true
  }

  function releaseAll() {
    for (const name of repeatTimers.keys()) stopRepeat(name)
    heldSources.clear()
    heldKeys.clear()
    activeBindings.clear()
    bridge?.releaseAllActions?.()
    publish()
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
    // A control may move focus while its key is still held (for example when a
    // modal opens). Always process key-up so that an action accepted by the
    // active surface cannot remain latched after that focus transition.
    const onKeyUp = (event) => attached && handleKeyUp(event)
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
    releaseAll,
    handleKeyDown,
    handleKeyUp,
    attach,
    getHeldActions: () => [...heldSources.keys()],
  })
}
