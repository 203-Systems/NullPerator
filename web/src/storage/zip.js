import { Inflate, Zip, ZipDeflate } from 'fflate'

export const ZIP_LIMITS = Object.freeze({
  maxEntries: 4096,
  maxFileBytes: 32 * 1024 * 1024,
  maxInflatedBytes: 128 * 1024 * 1024,
  maxCompressedBytes: 64 * 1024 * 1024,
})

const decoder = new TextDecoder()
const viewOf = (bytes) => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
const u16 = (view, offset) => view.getUint16(offset, true)
const u32 = (view, offset) => view.getUint32(offset, true)
const equalBytes = (left, right) => left.length === right.length && left.every((value, index) => value === right[index])
const crcTable = (() => {
  const table = new Uint32Array(256)
  for (let index = 0; index < table.length; index += 1) {
    let value = index
    for (let bit = 0; bit < 8; bit += 1) value = (value & 1) ? (0xedb88320 ^ (value >>> 1)) : (value >>> 1)
    table[index] = value >>> 0
  }
  return table
})()

function crc32(bytes, current = 0xffffffff) {
  let value = current
  for (const byte of bytes) value = crcTable[(value ^ byte) & 0xff] ^ (value >>> 8)
  return value >>> 0
}
function finalCrc(current) { return (current ^ 0xffffffff) >>> 0 }
function unsafe(reason) { throw new Error(`Unsafe ZIP entry: ${reason}`) }
function invalid(reason) { throw new Error(`Invalid ZIP archive: ${reason}`) }

function normaliseEntryName(name) {
  if (!name || name.includes('\0') || name.includes('\\') || name.startsWith('/') || /^([A-Za-z]:|\/\/)/.test(name)) unsafe(name || 'empty name')
  const directory = name.endsWith('/')
  const parts = name.split('/')
  if (directory) parts.pop()
  if (parts.length === 0 || parts.some((part) => !part || part === '.' || part === '..')) unsafe(name)
  const relativePath = parts.join('/')
  return { relativePath, path: `/data/${relativePath}`, kind: directory ? 'directory' : 'file' }
}

function parseCentralDirectory(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input)
  if (bytes.byteLength > ZIP_LIMITS.maxCompressedBytes) throw new Error('ZIP compressed size exceeds limit')
  const view = viewOf(bytes)
  let end = -1
  for (let offset = bytes.length - 22; offset >= Math.max(0, bytes.length - 22 - 0xffff); offset -= 1) {
    if (u32(view, offset) === 0x06054b50) { end = offset; break }
  }
  if (end < 0 || end + 22 > bytes.length || end + 22 + u16(view, end + 20) !== bytes.length) invalid('missing end record')
  const disk = u16(view, end + 4)
  const directoryDisk = u16(view, end + 6)
  const entriesOnDisk = u16(view, end + 8)
  const entryCount = u16(view, end + 10)
  const centralSize = u32(view, end + 12)
  const centralOffset = u32(view, end + 16)
  if (disk || directoryDisk || entriesOnDisk !== entryCount || entryCount === 0xffff || centralSize === 0xffffffff || centralOffset === 0xffffffff) invalid('unsupported multi-disk or Zip64 layout')
  if (entryCount > ZIP_LIMITS.maxEntries) throw new Error('ZIP entries exceeds limit')
  if (centralOffset + centralSize > end) invalid('central directory bounds')
  const entries = []
  let cursor = centralOffset
  let declaredTotal = 0
  for (let index = 0; index < entryCount; index += 1) {
    if (cursor + 46 > centralOffset + centralSize || u32(view, cursor) !== 0x02014b50) invalid('central entry')
    const madeBy = u16(view, cursor + 4)
    const flags = u16(view, cursor + 8)
    const method = u16(view, cursor + 10)
    const crc = u32(view, cursor + 16)
    const compressed = u32(view, cursor + 20)
    const uncompressed = u32(view, cursor + 24)
    const nameLength = u16(view, cursor + 28)
    const extraLength = u16(view, cursor + 30)
    const commentLength = u16(view, cursor + 32)
    const attributes = u32(view, cursor + 38)
    const localOffset = u32(view, cursor + 42)
    const next = cursor + 46 + nameLength + extraLength + commentLength
    if (next > centralOffset + centralSize) invalid('central entry bounds')
    if ((flags & ~0x0808) !== 0 || (flags & 1) || (method !== 0 && method !== 8)) invalid('unsupported entry encoding')
    if (compressed > ZIP_LIMITS.maxCompressedBytes || uncompressed > ZIP_LIMITS.maxFileBytes || (method === 0 && compressed !== uncompressed)) invalid('entry size')
    declaredTotal += uncompressed
    if (declaredTotal > ZIP_LIMITS.maxInflatedBytes) throw new Error('ZIP inflated bytes exceeds limit')
    const rawName = bytes.slice(cursor + 46, cursor + 46 + nameLength)
    const mode = (attributes >>> 16) & 0xffff
    const type = mode & 0o170000
    if ((madeBy >>> 8) === 3 && type && type !== 0o040000 && type !== 0o100000) unsafe('unsupported Unix file type')
    entries.push({ ...normaliseEntryName(decoder.decode(rawName)), flags, method, crc, compressed, uncompressed, localOffset, rawName })
    cursor = next
  }
  if (cursor !== centralOffset + centralSize) invalid('central directory length')
  const byPath = new Map()
  for (const entry of entries) {
    if (byPath.has(entry.path)) unsafe(`duplicate ${entry.relativePath}`)
    byPath.set(entry.path, entry)
  }
  for (const entry of entries) {
    let parent = entry.path.slice(0, entry.path.lastIndexOf('/'))
    while (parent && parent !== '/data') {
      const ancestor = byPath.get(parent)
      if (ancestor?.kind === 'file') unsafe(`file ancestor ${ancestor.relativePath}`)
      parent = parent.slice(0, parent.lastIndexOf('/'))
    }
  }
  return { bytes, view, entries, centralOffset }
}

function localRecord(archive, entry) {
  const { bytes, view, centralOffset } = archive
  const offset = entry.localOffset
  if (offset + 30 > centralOffset || u32(view, offset) !== 0x04034b50) invalid('local header')
  const flags = u16(view, offset + 6)
  const method = u16(view, offset + 8)
  const localCrc = u32(view, offset + 14)
  const localCompressed = u32(view, offset + 18)
  const localUncompressed = u32(view, offset + 22)
  const nameLength = u16(view, offset + 26)
  const extraLength = u16(view, offset + 28)
  const dataStart = offset + 30 + nameLength + extraLength
  const dataEnd = dataStart + entry.compressed
  if (flags !== entry.flags || method !== entry.method || dataStart > centralOffset || dataEnd > centralOffset || !equalBytes(bytes.subarray(offset + 30, offset + 30 + nameLength), entry.rawName)) invalid('local header mismatch')
  let end = dataEnd
  if (flags & 8) {
    if (end + 12 > centralOffset) invalid('data descriptor bounds')
    if (u32(view, end) === 0x08074b50) end += 4
    if (end + 12 > centralOffset || u32(view, end) !== entry.crc || u32(view, end + 4) !== entry.compressed || u32(view, end + 8) !== entry.uncompressed) invalid('data descriptor mismatch')
    end += 12
  } else if (localCrc !== entry.crc || localCompressed !== entry.compressed || localUncompressed !== entry.uncompressed) {
    invalid('local size or CRC mismatch')
  }
  return { ...entry, data: bytes.subarray(dataStart, dataEnd), spanStart: offset, spanEnd: end }
}

function decodeEntry(entry, remainingTotal) {
  const chunks = []
  let actual = 0
  let crc = 0xffffffff
  const consume = (chunk) => {
    actual += chunk.byteLength
    if (actual > entry.uncompressed || actual > ZIP_LIMITS.maxFileBytes || actual > remainingTotal) invalid('inflated entry exceeds declared bounds')
    crc = crc32(chunk, crc)
    chunks.push(chunk)
  }
  if (entry.method === 0) consume(entry.data)
  else {
    const inflater = new Inflate((chunk) => consume(chunk))
    for (let offset = 0; offset < entry.data.length; offset += 32 * 1024) inflater.push(entry.data.subarray(offset, Math.min(entry.data.length, offset + 32 * 1024)), offset + 32 * 1024 >= entry.data.length)
    if (entry.data.length === 0) inflater.push(entry.data, true)
  }
  if (actual !== entry.uncompressed || finalCrc(crc) !== entry.crc) invalid('inflated size or CRC mismatch')
  const bytes = new Uint8Array(actual)
  let offset = 0
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length }
  return bytes
}

function parseArchive(input) {
  const archive = parseCentralDirectory(input)
  const records = archive.entries.map((entry) => localRecord(archive, entry)).sort((a, b) => a.spanStart - b.spanStart)
  for (let index = 1; index < records.length; index += 1) if (records[index - 1].spanEnd > records[index].spanStart) invalid('overlapping local records')
  let remaining = ZIP_LIMITS.maxInflatedBytes
  return records.map((record) => {
    const bytes = decodeEntry(record, remaining)
    remaining -= bytes.byteLength
    return { path: record.path, relativePath: record.relativePath, kind: record.kind, bytes }
  })
}

function existingMap(existingPaths) {
  if (existingPaths instanceof Map) return new Map(existingPaths)
  return new Map([...existingPaths].map((path) => [path, 'unknown']))
}

export function previewZipRestore(input, existingPaths = new Set()) {
  const entries = parseArchive(input)
  const existing = existingMap(existingPaths)
  const conflicts = []
  for (const entry of entries) {
    const exact = existing.get(entry.path)
    if (exact && !(exact === 'directory' && entry.kind === 'directory')) conflicts.push({ path: entry.path, kind: entry.kind, existingKind: exact, reason: exact === 'unknown' || exact === entry.kind ? 'replace' : 'kind' })
    for (const [path, kind] of existing) {
      if (kind === 'file' && entry.path.startsWith(`${path}/`)) conflicts.push({ path: entry.path, kind: entry.kind, existingKind: kind, reason: 'file-ancestor' })
      if (entry.kind === 'file' && path.startsWith(`${entry.path}/`)) conflicts.push({ path: entry.path, kind: entry.kind, existingKind: kind, reason: 'file-ancestor' })
    }
  }
  return Object.freeze({ entries, files: entries.filter((entry) => entry.kind === 'file'), directories: entries.filter((entry) => entry.kind === 'directory'), existing, conflicts })
}

function nextName(path, occupied) {
  const slash = path.lastIndexOf('/')
  const parent = path.slice(0, slash + 1)
  const name = path.slice(slash + 1)
  const dot = name.lastIndexOf('.')
  const stem = dot > 0 ? name.slice(0, dot) : name
  const extension = dot > 0 ? name.slice(dot) : ''
  for (let number = 2; ; number += 1) { const candidate = `${parent}${stem} (${number})${extension}`; if (!occupied.has(candidate)) return candidate }
}

export function planZipRestore(preview, policy) {
  if (!['overwrite', 'keep-both'].includes(policy)) throw new Error('A ZIP conflict policy is required')
  if (preview.conflicts.some((conflict) => conflict.reason !== 'replace' || conflict.kind !== 'file')) throw new Error('ZIP path conflicts with an existing file or directory')
  const occupied = new Set()
  // Reserve original archive names and implicit parent directories so a
  // renamed conflict cannot steal another incoming entry's destination.
  const reservePath = (path) => {
    while (path && path !== '/data') {
      occupied.add(path)
      path = path.slice(0, path.lastIndexOf('/'))
    }
  }
  for (const path of preview.existing.keys()) reservePath(path)
  for (const entry of preview.entries) reservePath(entry.path)
  const files = preview.files.map((file) => {
    let path = file.path
    if (preview.existing.has(path) && policy === 'keep-both') path = nextName(path, occupied)
    occupied.add(path)
    return { ...file, path }
  })
  const directories = new Set(['/data'])
  for (const entry of [...preview.directories, ...files]) {
    let parent = entry.kind === 'directory' ? entry.path : entry.path.slice(0, entry.path.lastIndexOf('/'))
    while (parent && parent !== '/data') { directories.add(parent); parent = parent.slice(0, parent.lastIndexOf('/')) }
  }
  return Object.freeze({ files, directories: [...directories].sort((a, b) => a.length - b.length || a.localeCompare(b)), policy })
}

export function createDiskZip(entries) {
  if (entries.length > ZIP_LIMITS.maxEntries) throw new Error('Disk export exceeds ZIP entry limit')
  const chunks = []
  let compressedBytes = 0
  // zipSync flattens filenames through an ordinary object internally, which
  // treats __proto__ as a property setter. Add named streams directly instead.
  const writer = new Zip((error, chunk) => {
    if (error) throw error
    compressedBytes += chunk.byteLength
    if (compressedBytes > ZIP_LIMITS.maxCompressedBytes) throw new Error('Disk export exceeds ZIP compressed limit')
    chunks.push(chunk)
  })
  let total = 0
  for (const entry of entries) {
    if (!entry.path.startsWith('/data/')) throw new Error('Disk export contains a path outside /data')
    if (entry.kind === 'file') {
      if (!(entry.bytes instanceof Uint8Array) || entry.bytes.byteLength > ZIP_LIMITS.maxFileBytes) throw new Error('Disk export file exceeds ZIP limit')
      total += entry.bytes.byteLength
      if (total > ZIP_LIMITS.maxInflatedBytes) throw new Error('Disk export exceeds ZIP size limit')
    }
    const relative = entry.path.slice('/data/'.length)
    const file = new ZipDeflate(entry.kind === 'directory' ? `${relative}/` : relative)
    writer.add(file)
    file.push(entry.kind === 'directory' ? new Uint8Array() : entry.bytes, true)
  }
  writer.end()
  const archive = new Uint8Array(compressedBytes)
  let offset = 0
  for (const chunk of chunks) {
    archive.set(chunk, offset)
    offset += chunk.byteLength
  }
  return archive
}
