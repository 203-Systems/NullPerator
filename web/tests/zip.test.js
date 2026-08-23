import { describe, expect, it } from 'vitest'
import { zipSync, strToU8 } from 'fflate'

import {
  ZIP_LIMITS,
  createDiskZip,
  previewZipRestore,
  planZipRestore,
} from '../src/storage/zip.js'

function makeZip(files) {
  return zipSync(Object.fromEntries(Object.entries(files).map(([name, value]) => [name, strToU8(value)])))
}

const viewOf = (bytes) => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
const read32 = (view, offset) => view.getUint32(offset, true)
const write16 = (view, offset, value) => view.setUint16(offset, value, true)
const write32 = (view, offset, value) => view.setUint32(offset, value, true)
function layout(bytes) {
  const view = viewOf(bytes)
  let end = -1
  for (let offset = bytes.length - 22; offset >= 0; offset -= 1) if (read32(view, offset) === 0x06054b50) { end = offset; break }
  const central = read32(view, end + 16)
  return { view, end, central, local: read32(view, central + 42) }
}

function withDescriptor(bytes) {
  const { view, end, central, local } = layout(bytes)
  const compressed = read32(view, central + 20)
  const uncompressed = read32(view, central + 24)
  const crc = read32(view, central + 16)
  const nameLength = view.getUint16(local + 26, true)
  const extraLength = view.getUint16(local + 28, true)
  const dataEnd = local + 30 + nameLength + extraLength + compressed
  const result = new Uint8Array(bytes.length + 16)
  result.set(bytes.subarray(0, dataEnd))
  const target = viewOf(result)
  write32(target, dataEnd, 0x08074b50); write32(target, dataEnd + 4, crc); write32(target, dataEnd + 8, compressed); write32(target, dataEnd + 12, uncompressed)
  result.set(bytes.subarray(dataEnd, end), dataEnd + 16)
  const newEnd = end + 16; const newCentral = central + 16
  result.set(bytes.subarray(end), newEnd)
  const rewritten = viewOf(result)
  write16(rewritten, local + 6, view.getUint16(local + 6, true) | 8)
  write16(rewritten, newCentral + 8, view.getUint16(central + 8, true) | 8)
  write32(rewritten, newEnd + 16, newCentral)
  return result
}
function descriptorOffset(bytes) {
  const { view, central, local } = layout(bytes)
  return local + 30 + view.getUint16(local + 26, true) + view.getUint16(local + 28, true) + read32(view, central + 20)
}

describe('virtual disk ZIP safety', () => {
  it('rejects traversal, absolute, drive, UNC, backslash, and NUL entry names before disk mutation', () => {
    for (const name of ['../outside', '/absolute', 'C:/drive', '//server/share', 'dir\\file', 'bad\0name']) {
      expect(() => previewZipRestore(makeZip({ [name]: 'bad' }), new Set())).toThrow(/unsafe ZIP entry/i)
    }
  })

  it('enforces archive, entry, and inflated-byte limits', () => {
    expect(() => previewZipRestore(new Uint8Array(ZIP_LIMITS.maxCompressedBytes + 1), new Set())).toThrow(/compressed/i)
    const tooMany = Object.fromEntries(Array.from({ length: ZIP_LIMITS.maxEntries + 1 }, (_, index) => [`f${index}`, 'x']))
    expect(() => previewZipRestore(makeZip(tooMany), new Set())).toThrow(/entries/i)
    expect(() => previewZipRestore(makeZip({ huge: 'x'.repeat(ZIP_LIMITS.maxFileBytes + 1) }), new Set())).toThrow(/size|limit/i)
  })

  it('previews conflicts without mutating and requires an explicit deterministic policy', () => {
    const archive = makeZip({ 'projects/demo.dat': 'new', 'samples/kick.wav': 'kick' })
    const preview = previewZipRestore(archive, new Set(['/data/projects/demo.dat']))

    expect(preview.conflicts.map(({ path }) => path)).toEqual(['/data/projects/demo.dat'])
    expect(() => planZipRestore(preview, 'ask')).toThrow(/policy/i)
    expect(planZipRestore(preview, 'overwrite').files.map(({ path }) => path)).toEqual([
      '/data/projects/demo.dat',
      '/data/samples/kick.wav',
    ])
    expect(planZipRestore(preview, 'keep-both').files.map(({ path }) => path)).toEqual([
      '/data/projects/demo (2).dat',
      '/data/samples/kick.wav',
    ])
  })

  it('preserves explicit directories while generating their parent creation plan', () => {
    const preview = previewZipRestore(zipSync({ 'projects/': new Uint8Array(), 'projects/demo.dat': strToU8('PT') }), new Set())
    const plan = planZipRestore(preview, 'overwrite')
    expect(plan.directories).toEqual(['/data', '/data/projects'])
    expect(plan.files[0].path).toBe('/data/projects/demo.dat')
  })

  it('checks inflated output against declared lengths and skips occupied keep-both suffixes', () => {
    const archive = makeZip({ 'demo.dat': 'data' })
    const corrupt = new Uint8Array(archive)
    for (let index = 0; index + 46 < corrupt.length; index += 1) {
      if (corrupt[index] === 0x50 && corrupt[index + 1] === 0x4b && corrupt[index + 2] === 1 && corrupt[index + 3] === 2) { corrupt[index + 24] = 99; break }
    }
    expect(() => previewZipRestore(corrupt, new Set())).toThrow(/limit|content|mismatch/i)
    const preview = previewZipRestore(archive, new Set(['/data/demo.dat', '/data/demo (2).dat', '/data/demo (3).dat']))
    expect(planZipRestore(preview, 'keep-both').files[0].path).toBe('/data/demo (4).dat')
  })

  it('validates local headers, data descriptors, CRCs, and internal file ancestors before decoding', () => {
    const base = makeZip({ 'safe.dat': 'safe' })
    const fakeLocalSize = new Uint8Array(base); const localLayout = layout(fakeLocalSize)
    write32(localLayout.view, localLayout.local + 18, 100)
    expect(() => previewZipRestore(fakeLocalSize, new Set())).toThrow(/local size/i)

    const descriptor = withDescriptor(base); const descriptorLayout = layout(descriptor)
    write32(descriptorLayout.view, descriptorOffset(descriptor) + 8, 99)
    expect(() => previewZipRestore(descriptor, new Set())).toThrow(/descriptor/i)

    const badCrc = new Uint8Array(base); const crcLayout = layout(badCrc)
    const wrong = read32(crcLayout.view, crcLayout.central + 16) ^ 1
    write32(crcLayout.view, crcLayout.central + 16, wrong); write32(crcLayout.view, crcLayout.local + 14, wrong)
    expect(() => previewZipRestore(badCrc, new Set())).toThrow(/CRC/i)
    expect(() => previewZipRestore(makeZip({ file: 'x', 'file/child': 'y' }), new Set())).toThrow(/ancestor/i)
  })

  it('round-trips every data path through export without filtering test-like names', () => {
    const archive = createDiskZip([{ path: '/data/idbfs-e2e-user-file.dat', kind: 'file', bytes: strToU8('user') }])
    expect(previewZipRestore(archive, new Set()).files.map(({ path }) => path)).toEqual(['/data/idbfs-e2e-user-file.dat'])
  })

  it('reports existing file/directory kind conflicts in the preview before planning writes', () => {
    const preview = previewZipRestore(makeZip({ name: 'x', 'other/child': 'y' }), new Map([
      ['/data/name', 'directory'],
      ['/data/other', 'file'],
    ]))
    expect(preview.conflicts).toEqual(expect.arrayContaining([
      expect.objectContaining({ path: '/data/name', reason: 'kind', existingKind: 'directory' }),
      expect.objectContaining({ path: '/data/other/child', reason: 'file-ancestor', existingKind: 'file' }),
    ]))
    expect(() => planZipRestore(preview, 'overwrite')).toThrow(/conflict/i)
  })
})
