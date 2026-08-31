import { describe, expect, it, vi } from 'vitest'

import { DEFAULT_KEY_MAP, createInputStore } from '../src/stores/input.js'

function createBridge() {
  return {
    pressAction: vi.fn(),
    releaseAction: vi.fn(),
    repeatAction: vi.fn(),
    releaseAllActions: vi.fn(),
  }
}

describe('tracker input state', () => {
  it('exposes only the fixed Node eight-key browser layout', () => {
    expect(DEFAULT_KEY_MAP.left.bindings).toContainEqual(['KeyA'])
    expect(DEFAULT_KEY_MAP.down.bindings).toContainEqual(['KeyS'])
    expect(DEFAULT_KEY_MAP.right.bindings).toContainEqual(['KeyD'])
    expect(DEFAULT_KEY_MAP.up.bindings).toContainEqual(['KeyW'])
    expect(DEFAULT_KEY_MAP.edit.bindings).toEqual([['KeyK']])
    expect(DEFAULT_KEY_MAP.option.bindings).toEqual([['KeyJ']])
    expect(DEFAULT_KEY_MAP.shift.bindings).toEqual([['KeyX']])
    expect(DEFAULT_KEY_MAP.play.bindings).toEqual([['KeyC']])
    expect(DEFAULT_KEY_MAP.power.bindings).toEqual([])
  })

  it('releases every held action when focus is lost', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)

    input.press('edit', 'keyboard:KeyA')
    input.press('up', 'keyboard:ArrowUp')
    input.releaseAll()

    expect(bridge.pressAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.edit.action],
      [DEFAULT_KEY_MAP.up.action],
    ])
    expect(bridge.releaseAllActions).toHaveBeenCalledOnce()
    expect(input.getHeldActions()).toEqual([])
  })

  it('releases held input on pagehide and ignores detached lifecycle callbacks', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)
    const listeners = new Map()
    const target = {
      addEventListener: (name, listener) => listeners.set(name, listener),
      removeEventListener: (name, listener) => {
        if (listeners.get(name) === listener) listeners.delete(name)
      },
    }
    const document = { addEventListener() {}, removeEventListener() {}, visibilityState: 'visible' }
    const detach = input.attach({ target, document })
    const stalePageHide = listeners.get('pagehide')

    input.press('edit', 'keyboard:KeyK')
    stalePageHide()
    expect(input.getHeldActions()).toEqual([])
    expect(bridge.releaseAllActions).toHaveBeenCalledTimes(1)

    input.press('option', 'keyboard:KeyJ')
    detach()
    expect(listeners.has('pagehide')).toBe(false)
    expect(bridge.releaseAllActions).toHaveBeenCalledTimes(2)
    stalePageHide()
    detach()
    expect(bridge.releaseAllActions).toHaveBeenCalledTimes(2)
  })

  it('releases an accepted key after the input surface becomes inactive', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)
    const listeners = new Map()
    let active = true
    const target = {
      addEventListener: (name, listener) => listeners.set(name, listener),
      removeEventListener() {},
    }
    const document = { addEventListener() {}, removeEventListener() {}, visibilityState: 'visible' }
    input.attach({ target, document, isActive: () => active })

    listeners.get('keydown')({ code: 'KeyW', repeat: false, preventDefault() {} })
    active = false
    listeners.get('keyup')({ code: 'KeyW', preventDefault() {} })

    expect(bridge.pressAction).toHaveBeenCalledWith(DEFAULT_KEY_MAP.up.action)
    expect(bridge.releaseAction).toHaveBeenCalledWith(DEFAULT_KEY_MAP.up.action)
    expect(input.getHeldActions()).toEqual([])
  })

  it('keeps an action held until every simultaneous source releases it', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)

    input.press('edit', 'keyboard:KeyA')
    input.press('edit', 'pointer:11')
    input.release('edit', 'keyboard:KeyA')

    expect(bridge.releaseAction).not.toHaveBeenCalled()
    input.release('edit', 'pointer:11')
    expect(bridge.releaseAction).toHaveBeenCalledWith(DEFAULT_KEY_MAP.edit.action)
  })

  it('publishes held actions for virtual control feedback', () => {
    const input = createInputStore(createBridge())
    const snapshots = []
    const unsubscribe = input.subscribe((held) => snapshots.push([...held]))

    input.handleKeyDown({ code: 'KeyW', repeat: false, preventDefault() {} })
    input.handleKeyDown({ code: 'KeyK', repeat: false, preventDefault() {} })
    input.handleKeyUp({ code: 'KeyW', preventDefault() {} })
    input.releaseAll()
    unsubscribe()

    expect(snapshots).toEqual([[], ['up'], ['up', 'edit'], ['edit'], []])
  })

  it('sends immediate M8 PLAY and independent SHIFT actions', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)

    input.press('play', 'test-play')
    input.press('shift', 'test-shift')
    input.release('shift', 'test-shift')
    input.release('play', 'test-play')

    expect(bridge.pressAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.play.action],
      [DEFAULT_KEY_MAP.shift.action],
    ])
    expect(bridge.releaseAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.shift.action],
      [DEFAULT_KEY_MAP.play.action],
    ])
  })

  it('ignores DOM repeats, handles simultaneous fixed keys, and prevents only consumed keys', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)
    const shiftDown = { code: 'KeyX', repeat: false, preventDefault: vi.fn() }
    const editDown = { code: 'KeyK', repeat: false, preventDefault: vi.fn() }
    const repeatedEdit = { code: 'KeyK', repeat: true, preventDefault: vi.fn() }
    const editUp = { code: 'KeyK', preventDefault: vi.fn() }
    const unrelated = { code: 'KeyZ', repeat: false, preventDefault: vi.fn() }

    input.handleKeyDown(shiftDown)
    input.handleKeyDown(editDown)
    input.handleKeyDown(repeatedEdit)
    input.handleKeyUp(editUp)
    input.handleKeyDown(unrelated)

    expect(bridge.pressAction.mock.calls).toEqual([[DEFAULT_KEY_MAP.shift.action], [DEFAULT_KEY_MAP.edit.action]])
    expect(bridge.releaseAction).toHaveBeenCalledWith(DEFAULT_KEY_MAP.edit.action)
    expect(editDown.preventDefault).toHaveBeenCalledOnce()
    expect(repeatedEdit.preventDefault).toHaveBeenCalledOnce()
    expect(unrelated.preventDefault).not.toHaveBeenCalled()
  })

  it('repeats held directions after the Node delay and stops on release', () => {
    vi.useFakeTimers()
    const bridge = createBridge()
    const input = createInputStore(bridge)

    input.press('down', 'test')
    vi.advanceTimersByTime(499)
    expect(bridge.repeatAction).not.toHaveBeenCalled()
    vi.advanceTimersByTime(1 + 75 * 2)
    expect(bridge.repeatAction).toHaveBeenCalledTimes(3)
    expect(bridge.repeatAction).toHaveBeenLastCalledWith(DEFAULT_KEY_MAP.down.action)

    input.release('down', 'test')
    vi.advanceTimersByTime(300)
    expect(bridge.repeatAction).toHaveBeenCalledTimes(3)
    vi.useRealTimers()
  })

})
