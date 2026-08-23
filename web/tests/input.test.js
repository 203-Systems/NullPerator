import { describe, expect, it, vi } from 'vitest'

import { DEFAULT_KEY_MAP, START_HOLD_MS, createInputStore } from '../src/stores/input.js'

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
    expect(DEFAULT_KEY_MAP.enter.bindings).toEqual([['KeyJ']])
    expect(DEFAULT_KEY_MAP.edit.bindings).toEqual([['KeyK']])
    expect(DEFAULT_KEY_MAP.alt.bindings).toEqual([['KeyX']])
    expect(DEFAULT_KEY_MAP.play.bindings).toEqual([])
    expect(DEFAULT_KEY_MAP.nav.bindings).toEqual([])
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

  it('matches Node START semantics: tap PLAY, hold or chord NAV', () => {
    vi.useFakeTimers()
    try {
      const tapBridge = createBridge()
      const tap = createInputStore(tapBridge)
      tap.pressStart('test')
      expect(tapBridge.pressAction).toHaveBeenLastCalledWith(DEFAULT_KEY_MAP.nav.action)
      tap.releaseStart('test')
      expect(tapBridge.releaseAction.mock.calls).toEqual([
        [DEFAULT_KEY_MAP.nav.action], [DEFAULT_KEY_MAP.play.action],
      ])

      const holdBridge = createBridge()
      const hold = createInputStore(holdBridge)
      hold.pressStart('test')
      vi.advanceTimersByTime(START_HOLD_MS)
      hold.releaseStart('test')
      expect(holdBridge.pressAction.mock.calls).toEqual([[DEFAULT_KEY_MAP.nav.action]])
      expect(holdBridge.releaseAction.mock.calls).toEqual([[DEFAULT_KEY_MAP.nav.action]])

      const chordBridge = createBridge()
      const chord = createInputStore(chordBridge)
      chord.pressStart('test')
      chord.press('alt', 'test-alt')
      chord.releaseStart('test')
      expect(chordBridge.pressAction.mock.calls).toEqual([
        [DEFAULT_KEY_MAP.nav.action], [DEFAULT_KEY_MAP.alt.action],
      ])
    } finally {
      vi.useRealTimers()
    }
  })

  it('uses an exact 500 ms START boundary and never invents PLAY during cleanup', () => {
    vi.useFakeTimers()
    try {
      const beforeBoundary = createBridge()
      const tap = createInputStore(beforeBoundary)
      tap.pressStart('tap')
      vi.advanceTimersByTime(START_HOLD_MS - 1)
      tap.releaseStart('tap')
      expect(beforeBoundary.pressAction.mock.calls).toEqual([
        [DEFAULT_KEY_MAP.nav.action],
        [DEFAULT_KEY_MAP.play.action],
      ])

      const atBoundary = createBridge()
      const hold = createInputStore(atBoundary)
      hold.pressStart('hold')
      vi.advanceTimersByTime(START_HOLD_MS)
      hold.releaseStart('hold')
      expect(atBoundary.pressAction.mock.calls).toEqual([[DEFAULT_KEY_MAP.nav.action]])

      const cleanupBridge = createBridge()
      const cleanup = createInputStore(cleanupBridge)
      cleanup.pressStart('blurred')
      cleanup.releaseAll()
      vi.advanceTimersByTime(START_HOLD_MS)
      expect(cleanupBridge.pressAction.mock.calls).toEqual([[DEFAULT_KEY_MAP.nav.action]])
      expect(cleanupBridge.releaseAllActions).toHaveBeenCalledOnce()
    } finally {
      vi.useRealTimers()
    }
  })

  it('uses key order to preserve NAV+ALT while making ALT+PLAY reachable', () => {
    const beforeBridge = createBridge()
    const before = createInputStore(beforeBridge)
    before.press('alt', 'held-first')
    before.pressStart('start-second')
    expect(beforeBridge.pressAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.alt.action],
      [DEFAULT_KEY_MAP.play.action],
    ])
    before.releaseStart('start-second')
    expect(beforeBridge.releaseAction.mock.calls).toEqual([[DEFAULT_KEY_MAP.play.action]])

    const afterBridge = createBridge()
    const after = createInputStore(afterBridge)
    after.pressStart('start-first')
    after.press('alt', 'held-second')
    after.releaseStart('start-first')
    expect(afterBridge.pressAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.nav.action],
      [DEFAULT_KEY_MAP.alt.action],
    ])

    const cancelledBridge = createBridge()
    const cancelled = createInputStore(cancelledBridge)
    cancelled.press('alt', 'held-first')
    cancelled.pressStart('start-second')
    cancelled.release('alt', 'held-first')
    cancelled.releaseStart('start-second')
    expect(cancelledBridge.releaseAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.play.action],
      [DEFAULT_KEY_MAP.alt.action],
    ])
  })

  it('never exposes the removed ALT+PLAY+EDIT chord', () => {
    const addedAfterStartBridge = createBridge()
    const addedAfterStart = createInputStore(addedAfterStartBridge)
    addedAfterStart.press('alt', 'alt')
    addedAfterStart.pressStart('start')
    addedAfterStart.press('edit', 'edit')
    expect(addedAfterStartBridge.releaseAction).toHaveBeenCalledWith(DEFAULT_KEY_MAP.play.action)
    expect(addedAfterStart.getHeldActions()).toEqual(['alt', 'edit'])

    const alreadyHeldBridge = createBridge()
    const alreadyHeld = createInputStore(alreadyHeldBridge)
    alreadyHeld.press('alt', 'alt')
    alreadyHeld.press('edit', 'edit')
    alreadyHeld.pressStart('start')
    expect(alreadyHeldBridge.pressAction.mock.calls).toEqual([
      [DEFAULT_KEY_MAP.alt.action],
      [DEFAULT_KEY_MAP.edit.action],
      [DEFAULT_KEY_MAP.nav.action],
    ])
    expect(alreadyHeld.getHeldActions()).not.toContain('play')
  })

  it('ignores DOM repeats, handles simultaneous fixed keys, and prevents only consumed keys', () => {
    const bridge = createBridge()
    const input = createInputStore(bridge)
    const altDown = { code: 'KeyX', repeat: false, preventDefault: vi.fn() }
    const enterDown = { code: 'KeyJ', repeat: false, preventDefault: vi.fn() }
    const repeatedEnter = { code: 'KeyJ', repeat: true, preventDefault: vi.fn() }
    const enterUp = { code: 'KeyJ', preventDefault: vi.fn() }
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
