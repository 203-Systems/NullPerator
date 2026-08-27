import { describe, expect, it, vi } from 'vitest'

import {
  DEFAULT_SETTINGS,
  SETTINGS_STORAGE_KEY,
  createSettingsStore,
  migrateSettings,
} from '../src/stores/settings.js'

describe('versioned workbench settings', () => {
  it('drops legacy remappable key maps while retaining audio settings', () => {
    const migrated = migrateSettings({ version: 1, keyMap: { enter: { bindings: [['Enter']] } }, audioBufferFrames: 1024 })
    expect(migrated).toMatchObject({ version: 4, audioBufferFrames: 1024, lowLatencyAudio: true, developerMode: 'auto' })
    expect(migrated).not.toHaveProperty('keyMap')
  })

  it('normalizes unsafe values while retaining explicit disabled trace and mute', () => {
    expect(migrateSettings({
      displayScale: '99', audioBufferFrames: 99999, outputVolume: -4,
      version: 4, traceMask: 0, lowLatencyAudio: 1, developerMode: true,
    })).toMatchObject({
      displayScale: 'fit', audioBufferFrames: 8192, outputVolume: 0,
      traceMask: 0, lowLatencyAudio: true, developerMode: true,
    })
  })

  it('persists updates, publishes isolated snapshots, and resets atomically', () => {
    const written = new Map()
    const storage = {
      getItem: (key) => written.get(key) ?? null,
      setItem: (key, value) => written.set(key, value),
    }
    const settings = createSettingsStore({ localStorage: storage })
    const seen = []
    const unsubscribe = settings.subscribe((snapshot) => seen.push(snapshot.outputVolume))
    const updated = settings.update({ outputVolume: 42, displayScale: '2', developerMode: true })
    expect(() => { updated.outputVolume = 1 }).toThrow()
    expect(settings.snapshot()).toMatchObject({ outputVolume: 42, displayScale: '2', developerMode: true })
    expect(JSON.parse(written.get(SETTINGS_STORAGE_KEY))).toMatchObject({ version: 4, outputVolume: 42, developerMode: true })
    settings.reset()
    expect(settings.snapshot()).toMatchObject({
      outputVolume: DEFAULT_SETTINGS.outputVolume,
      displayScale: DEFAULT_SETTINGS.displayScale,
    })
    expect(seen).toEqual([100, 42, 100])
    unsubscribe()
  })

  it('survives unavailable or corrupt localStorage', () => {
    const storage = {
      getItem: () => '{bad',
      setItem: () => { throw new Error('denied') },
    }
    const settings = createSettingsStore({ localStorage: storage })
    expect(() => settings.update({ audioBufferFrames: 2048 })).not.toThrow()
    expect(settings.snapshot().audioBufferFrames).toBe(2048)
  })
})
