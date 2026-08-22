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

  it('ignores DOM repeats, handles chords, and prevents only consumed keys', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge, {
      keyMap: {
        ...DEFAULT_KEY_MAP,
        enter: { action: DEFAULT_KEY_MAP.enter.action, bindings: [['ControlLeft', 'KeyA']] },
        nav: { action: DEFAULT_KEY_MAP.nav.action, bindings: [['KeyN']] },
      },
    })
    const controlDown = { code: 'ControlLeft', repeat: false, preventDefault: vi.fn() }
    const aDown = { code: 'KeyA', repeat: false, preventDefault: vi.fn() }
    const repeatedA = { code: 'KeyA', repeat: true, preventDefault: vi.fn() }
    const aUp = { code: 'KeyA', preventDefault: vi.fn() }
    const unrelated = { code: 'KeyZ', repeat: false, preventDefault: vi.fn() }

    input.handleKeyDown(controlDown)
    input.handleKeyDown(aDown)
    input.handleKeyDown(repeatedA)
    input.handleKeyUp(aUp)
    input.handleKeyDown(unrelated)

    expect(bridge.pressAction).toHaveBeenCalledTimes(1)
    expect(bridge.releaseAction).toHaveBeenCalledWith(DEFAULT_KEY_MAP.enter.action)
    expect(aDown.preventDefault).toHaveBeenCalledOnce()
    expect(repeatedA.preventDefault).toHaveBeenCalledOnce()
    expect(unrelated.preventDefault).not.toHaveBeenCalled()
  })

  it('persists a remapped key map and restores it for the next device', () => {
    const storage = new Map()
    const localStorage = {
      getItem: (key) => storage.get(key) ?? null,
      setItem: (key, value) => storage.set(key, value),
    }
    const map = {
      ...DEFAULT_KEY_MAP,
      play: { action: DEFAULT_KEY_MAP.play.action, bindings: [['KeyP']] },
    }

    createInputStore(createBridge(), { localStorage }).setKeyMap(map)
    const restored = createInputStore(createBridge(), { localStorage })

    expect(restored.getKeyMap().play.bindings).toEqual([['KeyP']])
  })
})
