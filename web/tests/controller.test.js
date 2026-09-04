import { describe, expect, it, vi } from 'vitest'

import { createControllerStore } from '../src/stores/controller.js'

function createHarness() {
  const windowListeners = new Map()
  const documentListeners = new Map()
  const frames = []
  let gamepads = []
  const target = {
    addEventListener: (name, listener) => windowListeners.set(name, listener),
    removeEventListener: (name, listener) => {
      if (windowListeners.get(name) === listener) windowListeners.delete(name)
    },
  }
  const document = {
    visibilityState: 'visible',
    addEventListener: (name, listener) => documentListeners.set(name, listener),
    removeEventListener: (name, listener) => {
      if (documentListeners.get(name) === listener) documentListeners.delete(name)
    },
  }
  const controller = createControllerStore({
    target,
    document,
    navigator: { getGamepads: () => gamepads },
    requestAnimationFrame: (callback) => { frames.push(callback); return frames.length },
    cancelAnimationFrame: vi.fn(),
  })
  const input = { press: vi.fn(), release: vi.fn() }
  return {
    controller,
    document,
    input,
    setGamepads: (next) => { gamepads = next },
    emit: (name, detail) => windowListeners.get(name)?.({ detail }),
    emitDocument: (name) => documentListeners.get(name)?.(),
    pendingFrames: () => frames.length,
    runFrame: () => frames.shift()?.(),
  }
}

function gamepad({ buttons = {}, axes = [0, 0], mapping = 'standard' } = {}) {
  const values = Array.from({ length: 16 }, () => ({ pressed: false, value: 0 }))
  for (const [index, value] of Object.entries(buttons)) {
    values[Number(index)] = { pressed: Boolean(value), value: value ? 1 : 0 }
  }
  return { index: 2, id: 'Test Controller', connected: true, mapping, buttons: values, axes }
}

describe('web game controller', () => {
  it('publishes connection state and maps the standard eight controls', () => {
    const harness = createHarness()
    harness.setGamepads([gamepad({ buttons: { 0: true, 9: true, 14: true } })])
    const detach = harness.controller.attachInput(harness.input)
    harness.runFrame()

    expect(harness.controller.snapshot()).toMatchObject({
      supported: true,
      connected: true,
      count: 1,
      names: ['Test Controller'],
      usable: true,
    })
    expect(harness.input.press.mock.calls.map(([action]) => action).sort()).toEqual(['enter', 'left', 'play'])

    harness.setGamepads([])
    harness.emit('gamepaddisconnected')
    harness.runFrame()
    expect(harness.input.release.mock.calls.map(([action]) => action).sort()).toEqual(['enter', 'left', 'play'])
    expect(harness.controller.snapshot().connected).toBe(false)
    detach()
  })

  it('uses hysteresis for analogue directions and releases only controller sources', () => {
    const harness = createHarness()
    harness.setGamepads([gamepad({ axes: [-0.6, 0] })])
    const detach = harness.controller.attachInput(harness.input)
    harness.runFrame()
    expect(harness.input.press).toHaveBeenCalledWith('left', 'gamepad:2:a0-')

    harness.setGamepads([gamepad({ axes: [-0.4, 0] })])
    harness.runFrame()
    expect(harness.input.release).not.toHaveBeenCalled()

    harness.setGamepads([gamepad({ axes: [-0.3, 0] })])
    harness.runFrame()
    expect(harness.input.release).toHaveBeenCalledWith('left', 'gamepad:2:a0-')
    detach()
    expect(harness.input.release).toHaveBeenCalledTimes(1)
  })

  it('does not dispatch non-standard mappings and releases on visibility loss', () => {
    const harness = createHarness()
    harness.setGamepads([gamepad({ buttons: { 0: true }, mapping: '' })])
    const detach = harness.controller.attachInput(harness.input)
    harness.runFrame()
    expect(harness.controller.snapshot()).toMatchObject({ connected: true, usable: false })
    expect(harness.input.press).not.toHaveBeenCalled()

    harness.setGamepads([gamepad({ buttons: { 0: true } })])
    harness.runFrame()
    expect(harness.input.press).toHaveBeenCalledWith('enter', 'gamepad:2:b0')
    harness.document.visibilityState = 'hidden'
    harness.emitDocument('visibilitychange')
    expect(harness.input.release).toHaveBeenCalledWith('enter', 'gamepad:2:b0')
    detach()
  })

  it('pauses while the tracker is inactive and wakes when it becomes active', () => {
    const harness = createHarness()
    let active = false
    harness.setGamepads([gamepad({ buttons: { 0: true } })])
    const detach = harness.controller.attachInput(harness.input, { isActive: () => active })
    harness.runFrame()
    expect(harness.input.press).not.toHaveBeenCalled()
    expect(harness.pendingFrames()).toBe(0)

    active = true
    detach.wake()
    harness.runFrame()
    expect(harness.input.press).toHaveBeenCalledWith('enter', 'gamepad:2:b0')

    active = false
    harness.runFrame()
    expect(harness.input.release).toHaveBeenCalledWith('enter', 'gamepad:2:b0')
    expect(harness.pendingFrames()).toBe(0)
    detach()
  })

  it('releases on window blur and resumes only after focus returns', () => {
    const harness = createHarness()
    harness.setGamepads([gamepad({ buttons: { 0: true } })])
    const detach = harness.controller.attachInput(harness.input)
    harness.runFrame()
    expect(harness.input.press).toHaveBeenCalledWith('enter', 'gamepad:2:b0')

    harness.emit('blur')
    expect(harness.input.release).toHaveBeenCalledWith('enter', 'gamepad:2:b0')
    harness.runFrame()
    expect(harness.input.press).toHaveBeenCalledTimes(1)
    expect(harness.pendingFrames()).toBe(0)

    harness.emit('focus')
    harness.runFrame()
    expect(harness.input.press).toHaveBeenCalledTimes(2)
    detach()
  })
})
