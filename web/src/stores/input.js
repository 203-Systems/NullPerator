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

export const START_HOLD_MS = 500

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
  edit: entry(ACTIONS.edit, ['KeyK']),
  enter: entry(ACTIONS.enter, ['KeyJ']),
  // NAV is not a standalone Node control. START provisionally holds it so
  // chords are ordered correctly, then converts a short standalone tap to
  // PLAY on release. No firmware view assigns an action to NAV by itself.
  nav: entry(ACTIONS.nav),
  // C is handled as Node's dual-purpose START button below: tap emits PLAY,
  // hold (or a chord with another key) behaves as NAV.
  play: entry(ACTIONS.play),
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
  const heldSources = new Map()
  const heldKeys = new Set()
  const activeBindings = new Set()
  let startState = null
  let detach = null

  const now = () => globalThis.performance?.now?.() ?? Date.now()

  function actionId(action) {
    const name = actionName(action)
    return name === null ? null : keyMap[name].action
  }

  function press(action, source = 'direct') {
    const name = actionName(action)
    if (name === null) return false
    if (startState && name !== 'nav' && name !== 'play') {
      if (startState.altPlay && name !== 'alt') {
        startState.chordTriggered = true
        release('play', startState.playSource)
      } else if (!startState.altPlay) {
        startState.chordTriggered = true
      }
    }
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
    if (!sources?.has(source)) return false
    if (name === 'alt' && sources.size === 1 && startState?.altPlay) {
      startState.chordTriggered = true
      release('play', startState.playSource)
    }
    sources.delete(source)
    if (sources.size === 0) {
      heldSources.delete(name)
      bridge?.releaseAction?.(actionId(name))
    }
    return true
  }

  function releaseAll() {
    startState = null
    heldSources.clear()
    heldKeys.clear()
    activeBindings.clear()
    bridge?.releaseAllActions?.()
  }

  function pressStart(source = 'start') {
    if (startState) return startState.source === source
    const companionActions = [...heldSources.keys()].filter((name) => name !== 'nav' && name !== 'play')
    const altPlay = companionActions.length === 1 && companionActions[0] === 'alt'
    startState = {
      source,
      pressedAt: now(),
      chordTriggered: companionActions.length > 0 && !altPlay,
      altPlay,
      navSource: `start-nav:${source}`,
      playSource: `start-play:${source}`,
    }
    if (altPlay) press('play', startState.playSource)
    else press('nav', startState.navSource)
    return true
  }

  function releaseStart(source = 'start') {
    if (startState?.source !== source) return false
    const state = startState
    const elapsed = now() - state.pressedAt
    const isShortTap = !state.chordTriggered && elapsed < START_HOLD_MS
    startState = null
    if (state.altPlay) {
      release('play', state.playSource)
      return true
    }
    release('nav', state.navSource)
    if (isShortTap) {
      const pulseSource = `start-play:${source}`
      press('play', pulseSource)
      release('play', pulseSource)
    }
    return true
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
    const isStart = event.code === 'KeyC'
    const consumed = isStart || bindingsForCode(event.code).length > 0
    if (consumed) event.preventDefault?.()
    if (event.repeat || !consumed) return consumed
    if (isStart) return pressStart('keyboard:KeyC')
    heldKeys.add(event.code)
    synchronizeBindings()
    return true
  }

  function handleKeyUp(event) {
    const isStart = event.code === 'KeyC'
    const consumed = isStart || bindingsForCode(event.code).length > 0
    if (consumed) event.preventDefault?.()
    if (isStart) return releaseStart('keyboard:KeyC')
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
