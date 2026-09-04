const EMPTY_STATE = Object.freeze({
  supported: false,
  connected: false,
  count: 0,
  names: Object.freeze([]),
  usable: false,
})

const STANDARD_BUTTONS = Object.freeze([
  Object.freeze({ index: 0, action: 'enter' }),
  Object.freeze({ index: 1, action: 'option' }),
  Object.freeze({ index: 8, action: 'shift' }),
  Object.freeze({ index: 9, action: 'play' }),
  Object.freeze({ index: 12, action: 'up' }),
  Object.freeze({ index: 13, action: 'down' }),
  Object.freeze({ index: 14, action: 'left' }),
  Object.freeze({ index: 15, action: 'right' }),
])

const NATIVE_FACE_BUTTONS = Object.freeze(STANDARD_BUTTONS.slice(0, 2))
const AXIS_PRESS_THRESHOLD = 0.55
const AXIS_RELEASE_THRESHOLD = 0.35

function statesEqual(left, right) {
  return left.supported === right.supported
    && left.connected === right.connected
    && left.count === right.count
    && left.usable === right.usable
    && left.names.length === right.names.length
    && left.names.every((name, index) => name === right.names[index])
}

function freezeState(candidate) {
  return Object.freeze({
    supported: Boolean(candidate.supported),
    connected: Boolean(candidate.connected),
    count: Number(candidate.count) || 0,
    names: Object.freeze([...(candidate.names ?? [])].map((name) => String(name || 'Game controller'))),
    usable: Boolean(candidate.usable),
  })
}

export function createControllerStore(options = {}) {
  const target = options.target ?? globalThis.window
  const document = options.document ?? globalThis.document
  const navigator = options.navigator ?? globalThis.navigator
  const requestFrame = options.requestAnimationFrame
    ?? target?.requestAnimationFrame?.bind(target)
    ?? ((callback) => setTimeout(callback, 16))
  const cancelFrame = options.cancelAnimationFrame
    ?? target?.cancelAnimationFrame?.bind(target)
    ?? clearTimeout
  const listeners = new Set()
  let snapshot = EMPTY_STATE

  function publish(candidate) {
    const next = freezeState(candidate)
    if (statesEqual(snapshot, next)) return
    snapshot = next
    for (const listener of listeners) listener(snapshot)
  }

  function readGamepads() {
    if (typeof navigator?.getGamepads !== 'function') return []
    try {
      return Array.from(navigator.getGamepads() ?? [])
        .filter((gamepad) => gamepad && gamepad.connected !== false)
    } catch {
      return []
    }
  }

  function publishWebState(gamepads = readGamepads()) {
    const supported = typeof navigator?.getGamepads === 'function'
    const usable = gamepads.some((gamepad) => gamepad.mapping === 'standard')
    const unchanged = snapshot.supported === supported
      && snapshot.connected === (gamepads.length > 0)
      && snapshot.count === gamepads.length
      && snapshot.usable === usable
      && snapshot.names.length === gamepads.length
      && gamepads.every((gamepad, index) => snapshot.names[index] === (gamepad.id || 'Game controller'))
    if (unchanged) return gamepads
    publish({
      supported,
      connected: gamepads.length > 0,
      count: gamepads.length,
      names: gamepads.map((gamepad) => gamepad.id || 'Game controller'),
      usable,
    })
    return gamepads
  }

  function publishNativeState(candidate = globalThis.__nullPeratorControllerState) {
    const state = candidate && typeof candidate === 'object' ? candidate : {}
    publish({
      supported: true,
      connected: Boolean(state.connected),
      count: Number(state.count) || 0,
      names: Array.isArray(state.names) ? state.names : [],
      usable: Boolean(state.connected),
    })
  }

  function subscribe(listener) {
    listeners.add(listener)
    listener(snapshot)
    return () => listeners.delete(listener)
  }

  function attachInput(input, { isActive = () => true, nativeHostActive = false } = {}) {
    let attached = true
    let frame = null
    let held = new Map()
    let windowFocused = document?.hasFocus?.() !== false

    function synchronize(next) {
      for (const [source, action] of held) {
        if (!next.has(source)) input?.release(action, source)
      }
      for (const [source, action] of next) {
        if (!held.has(source)) input?.press(action, source)
      }
      held = next
    }

    function axisPressed(value, direction, source) {
      const threshold = held.has(source) ? AXIS_RELEASE_THRESHOLD : AXIS_PRESS_THRESHOLD
      return direction < 0 ? value < -threshold : value > threshold
    }

    function desiredInputs(gamepads) {
      const next = new Map()
      for (const gamepad of gamepads) {
        if (!nativeHostActive && gamepad.mapping !== 'standard') continue
        const prefix = `gamepad:${Number(gamepad.index) || 0}`
        const buttons = nativeHostActive ? NATIVE_FACE_BUTTONS : STANDARD_BUTTONS
        for (const { index, action } of buttons) {
          const button = gamepad.buttons?.[index]
          if (button?.pressed || Number(button?.value) > 0.5) next.set(`${prefix}:b${index}`, action)
        }
        if (nativeHostActive) continue
        const horizontal = Number(gamepad.axes?.[0]) || 0
        const vertical = Number(gamepad.axes?.[1]) || 0
        const left = `${prefix}:a0-`
        const right = `${prefix}:a0+`
        const up = `${prefix}:a1-`
        const down = `${prefix}:a1+`
        if (axisPressed(horizontal, -1, left)) next.set(left, 'left')
        if (axisPressed(horizontal, 1, right)) next.set(right, 'right')
        if (axisPressed(vertical, -1, up)) next.set(up, 'up')
        if (axisPressed(vertical, 1, down)) next.set(down, 'down')
      }
      return next
    }

    function schedule() {
      if (!attached || frame !== null) return
      frame = requestFrame(poll)
    }

    function poll() {
      frame = null
      if (!attached) return
      const gamepads = readGamepads()
      if (!nativeHostActive) publishWebState(gamepads)
      const active = windowFocused && document?.visibilityState === 'visible' && isActive()
      synchronize(active ? desiredInputs(gamepads) : new Map())
      if (active && gamepads.length > 0) schedule()
    }

    function handleGamepadChange() {
      if (!nativeHostActive) publishWebState()
      schedule()
    }

    function handleNativeChange(event) {
      if (!nativeHostActive) return
      publishNativeState(event.detail)
      schedule()
    }

    function handleVisibilityChange() {
      if (document?.visibilityState !== 'visible') synchronize(new Map())
      else schedule()
    }

    function handleBlur() {
      windowFocused = false
      synchronize(new Map())
    }

    function handleFocus() {
      windowFocused = true
      schedule()
    }

    target?.addEventListener?.('gamepadconnected', handleGamepadChange)
    target?.addEventListener?.('gamepaddisconnected', handleGamepadChange)
    target?.addEventListener?.('nullperator-controller-change', handleNativeChange)
    target?.addEventListener?.('blur', handleBlur)
    target?.addEventListener?.('focus', handleFocus)
    document?.addEventListener?.('visibilitychange', handleVisibilityChange)
    if (nativeHostActive) publishNativeState()
    else publishWebState()
    schedule()

    const detach = () => {
      if (!attached) return
      attached = false
      if (frame !== null) cancelFrame(frame)
      target?.removeEventListener?.('gamepadconnected', handleGamepadChange)
      target?.removeEventListener?.('gamepaddisconnected', handleGamepadChange)
      target?.removeEventListener?.('nullperator-controller-change', handleNativeChange)
      target?.removeEventListener?.('blur', handleBlur)
      target?.removeEventListener?.('focus', handleFocus)
      document?.removeEventListener?.('visibilitychange', handleVisibilityChange)
      synchronize(new Map())
    }
    detach.wake = schedule
    return detach
  }

  return Object.freeze({
    snapshot: () => snapshot,
    subscribe,
    attachInput,
  })
}

export const controllerStore = createControllerStore()
