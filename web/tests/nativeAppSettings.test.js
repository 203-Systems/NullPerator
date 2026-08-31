import { describe, expect, it, vi } from 'vitest'
import {
  NATIVE_APP_SETTINGS_KEY,
  createNativeAppSettingsStore,
} from '../src/stores/nativeAppSettings.js'

describe('native app settings', () => {
  it('defaults touch controls to visible', () => {
    const store = createNativeAppSettingsStore({ localStorage: null })
    expect(store.snapshot()).toEqual({ hideControlsWithGamepad: false })
  })

  it('persists the gamepad visibility preference', () => {
    const setItem = vi.fn()
    const store = createNativeAppSettingsStore({
      localStorage: { getItem: () => null, setItem },
    })
    store.update({ hideControlsWithGamepad: true })
    expect(setItem).toHaveBeenCalledWith(
      NATIVE_APP_SETTINGS_KEY,
      JSON.stringify({ hideControlsWithGamepad: true }),
    )
  })
})
