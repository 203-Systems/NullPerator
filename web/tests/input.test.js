import { describe, expect, it, vi } from 'vitest'

import { DEFAULT_KEY_MAP, createInputStore } from '../src/stores/input.js'

function createBridge() {
  return {
    pressAction: vi.fn(),
    releaseAction: vi.fn(),
    releaseAllActions: vi.fn(),
  }
}

describe('tracker input state', () => {
  it('exposes only the fixed Node eight-key browser layout', () => {
    expect(DEFAULT_KEY_MAP.left.bindings).toContainEqual(['KeyA'])
    expect(DEFAULT_KEY_MAP.down.bindings).toContainEqual(['KeyS'])
    expect(DEFAULT_KEY_MAP.right.bindings).toContainEqual(['KeyD'])
    expect(DEFAULT_KEY_MAP.up.bindings).toContainEqual(['KeyW'])
    expect(DEFAULT_KEY_MAP.enter.bindings).toEqual([['KeyK']])
    expect(DEFAULT_KEY_MAP.edit.bindings).toEqual([['KeyJ']])
    expect(DEFAULT_KEY_MAP.alt.bindings).toEqual([['KeyX']])
    expect(DEFAULT_KEY_MAP.start.bindings).toEqual([['KeyC']])
    expect(DEFAULT_KEY_MAP.select.bindings).toEqual([])
    expect(DEFAULT_KEY_MAP.power.bindings).toEqual([])
  })

  it('releases every held action when focus is lost', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)

    input.press('enter', 'keyboard:KeyA')
    input.press('up', 'keyboard:ArrowUp')
    input.releaseAll()

    expect(bridge.pressAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.enter.action],
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

    input.press('enter', 'keyboard:KeyK')
    stalePageHide()
    expect(input.getHeldActions()).toEqual([])
    expect(bridge.releaseAllActions).toHaveBeenCalledTimes(1)

    input.press('edit', 'keyboard:KeyJ')
    detach()
    expect(listeners.has('pagehide')).toBe(false)
    expect(bridge.releaseAllActions).toHaveBeenCalledTimes(2)
    stalePageHide()
    detach()
    expect(bridge.releaseAllActions).toHaveBeenCalledTimes(2)
  })

  it('keeps an action held until every simultaneous source releases it', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)

    input.press('enter', 'keyboard:KeyA')
    input.press('enter', 'pointer:11')
    input.release('enter', 'keyboard:KeyA')

    expect(bridge.releaseAction).not.toHaveBeenCalled()
    input.release('enter', 'pointer:11')
    expect(bridge.releaseAction).toHaveBeenCalledWith(DEFAULT_KEY_MAP.enter.action)
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

    expect(snapshots).toEqual([[], ['up'], ['up', 'enter'], ['enter'], []])
  })

  it('sends one physical START action and leaves tap/hold semantics to firmware', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)

    input.pressStart('test')
    input.press('alt', 'test-alt')
    input.release('alt', 'test-alt')
    input.releaseStart('test')

    expect(bridge.pressAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.start.action],
      [DEFAULT_KEY_MAP.alt.action],
    ])
    expect(bridge.releaseAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.alt.action],
      [DEFAULT_KEY_MAP.start.action],
    ])
  })

  it('ignores DOM repeats, handles simultaneous fixed keys, and prevents only consumed keys', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)
    const altDown = { code: 'KeyX', repeat: false, preventDefault: vi.fn() }
    const enterDown = { code: 'KeyK', repeat: false, preventDefault: vi.fn() }
    const repeatedEnter = { code: 'KeyK', repeat: true, preventDefault: vi.fn() }
    const enterUp = { code: 'KeyK', preventDefault: vi.fn() }
    const unrelated = { code: 'KeyZ', repeat: false, preventDefault: vi.fn() }

    input.handleKeyDown(altDown)
    input.handleKeyDown(enterDown)
    input.handleKeyDown(repeatedEnter)
    input.handleKeyUp(enterUp)
    input.handleKeyDown(unrelated)

    expect(bridge.pressAction.mock.calls).toEqual([[DEFAULT_KEY_MAP.alt.action], [DEFAULT_KEY_MAP.enter.action]])
    expect(bridge.releaseAction).toHaveBeenCalledWith(DEFAULT_KEY_MAP.enter.action)
    expect(enterDown.preventDefault).toHaveBeenCalledOnce()
    expect(repeatedEnter.preventDefault).toHaveBeenCalledOnce()
    expect(unrelated.preventDefault).not.toHaveBeenCalled()
  })

})
