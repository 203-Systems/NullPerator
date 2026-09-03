import { describe, expect, it } from 'vitest'

import { isInternalEntry } from '../src/fileVisibility.js'

const file = (name) => ({ name, kind: 'file' })
const directory = (name) => ({ name, kind: 'directory' })

describe('Files panel visibility policy', () => {
  it.each([
    ['/data', '.config.xml'],
    ['/data', '.current'],
    ['/data/projects', '.load-rollback.dat'],
    ['/data/projects/demo', 'lgptsav.tmp'],
    ['/data/projects/demo/samples', '.ui2-sample-delete.kick.wav.tmp'],
    ['/data/projects/demo/samples', '.kick.w0'],
    ['/data/samples', '.kick.w0'],
    ['/data/samples/drums', '.snare.o3'],
    ['/data/recordings', '.REC01.b0'],
    ['/data/samples', '..w0'],
    ['/data/instruments', 'Bass.tmp'],
    ['/data/instruments', 'Lead.BAK'],
    ['/data/themes', '.night.npt.tmp'],
    ['/data/themes', '.1234567890abcdef.npt.bak'],
    ['/data/themes', '.五五五五五.npt.tmp'],
    ['/data/other', '.picotracker-copy-temp-4142'],
    ['/data/other', '.picotracker-copy-backup-E4B883E69687'],
  ])('hides firmware-owned file %s/%s', (parent, name) => {
    expect(isInternalEntry(parent, file(name))).toBe(true)
  })

  it.each([
    ['/data/projects', '.untitled'],
    ['/data/projects', '.untitled.session-backup'],
    ['/data/projects', '.picotracker-saveas-stage.demo'],
    ['/data/projects', '.picotracker-saveas-backup.五五五五五'],
  ])('hides firmware-owned directory %s/%s', (parent, name) => {
    expect(isInternalEntry(parent, directory(name))).toBe(true)
  })

  it.each([
    ['/data', '.samples'],
    ['/data', '.notes'],
    ['/data/projects/demo', '.config.xml'],
    ['/data/projects', '.untitled'],
    ['/data/projects', '.picotracker-saveas-stage.demo'],
    ['/data/projects', '.picotracker-saveas-stage.'],
    ['/data/projects', '.picotracker-saveas-stage.this-name-is-17bb'],
    ['/data/projects/demo/samples', '.user.wav'],
    ['/data/projects/demo/samples', '.ui2-sample-delete..tmp'],
    ['/data/projects/demo/samples', '.ui2-sample-delete...tmp'],
    ['/data/projects/demo/samples', '.ui2-sample-delete...picotracker-copy-temp-4142.tmp'],
    ['/data/projects/demo/samples', '.ui2-sample-delete.123456789012345678901.wav.tmp'],
    ['/data/other', '.kick.w0'],
    ['/data/recordings/archive', '.REC01.b0'],
    ['/data/samples', '.123456789012345678901.w0'],
    ['/data/instruments', '.private.tmp'],
    ['/data/instruments', 'this-name-is-21-chars.tmp'],
    ['/data/instruments', '七七七七七七七.tmp'],
    ['/data/other', 'Bass.tmp'],
    ['/data/themes', '.personal.npt'],
    ['/data/themes', '.notes.tmp'],
    ['/data/themes', '.1234567890abcdefg.npt.tmp'],
    ['/data/themes', '.六六六六六六.npt.tmp'],
    ['/data/themes', '.night.npt.TMP'],
    ['/data/other', '.picotracker-copy-temp-'],
    ['/data/other', '.picotracker-copy-temp-A'],
    ['/data/other', '.picotracker-copy-temp-ABC'],
    ['/data/other', '.picotracker-copy-temp-ab'],
    ['/data/other', '.picotracker-copy-backup-G0'],
  ])('keeps user-owned or malformed file %s/%s visible', (parent, name) => {
    expect(isInternalEntry(parent, file(name))).toBe(false)
  })

  it.each([
    ['/data', '.config.xml'],
    ['/data/projects', '.load-rollback.dat'],
    ['/data/projects', '.picotracker-saveas-stage.'],
    ['/data/projects', '.picotracker-saveas-stage.this-name-is-17bb'],
    ['/data/projects/demo/samples', '.ui2-sample-delete.kick.wav.tmp'],
    ['/data/instruments', 'Bass.tmp'],
    ['/data/themes', '.night.npt.tmp'],
    ['/data/other', '.picotracker-copy-temp-4142'],
  ])('keeps wrong-kind or malformed directory %s/%s visible', (parent, name) => {
    expect(isInternalEntry(parent, directory(name))).toBe(false)
  })

  it('fails open for incomplete entry metadata', () => {
    expect(isInternalEntry('/data', null)).toBe(false)
    expect(isInternalEntry('/data', { name: '.config.xml' })).toBe(false)
    expect(isInternalEntry('/data', { name: '.config.xml', kind: 'unknown' })).toBe(false)
  })
})
