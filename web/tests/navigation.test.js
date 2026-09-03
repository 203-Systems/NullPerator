import { describe, expect, it } from 'vitest'

import {
  DEVELOPER_SECTIONS,
  USER_SECTIONS,
  isDeveloperSection,
  sectionLabel,
  visibleSections,
} from '../src/navigation.js'

describe('workbench navigation', () => {
  it('keeps every user tool available without developer tools', () => {
    expect(visibleSections(false)).toEqual(USER_SECTIONS)
    expect(visibleSections(false)).toEqual(['Device', 'Files', 'MIDI', 'Settings', 'About'])
  })

  it('only adds diagnostics when developer tools are enabled', () => {
    expect(visibleSections(true)).toEqual(['Device', 'Files', 'MIDI', ...DEVELOPER_SECTIONS, 'Settings', 'About'])
    expect(DEVELOPER_SECTIONS.every(isDeveloperSection)).toBe(true)
    expect(USER_SECTIONS.some(isDeveloperSection)).toBe(false)
  })

  it('keeps internal route IDs out of user-facing navigation labels', () => {
    expect(sectionLabel('Device')).toBe('Tracker')
    expect(sectionLabel('Files')).toBe('Files')
  })
})
