import { describe, expect, it, vi } from 'vitest'

import { createFilesHandle } from '../src/handles/files.js'
import { zipSync, strToU8 } from 'fflate'
import { createDiskZip, planZipRestore } from '../src/storage/zip.js'

function createMemoryFs() {
  const directories = new Set(['/data'])
  const files = new Map()
  const sizes = new Map()
  const reads = []
  let failWrites = false
  let failMkdir = false
  let failRename = false
  let failSecondUnlink = false
  let unlinkCalls = 0
  let failRollbackWrite = false
  const parent = (path) => path.slice(0, path.lastIndexOf('/')) || '/'
  const ensureParent = (path) => {
    if (!directories.has(parent(path))) throw new Error(`missing parent ${parent(path)}`)
  }
  const readdir = (path) => ['.', '..', ...[...directories, ...files.keys()]
    .filter((entry) => parent(entry) === path)
    .map((entry) => entry.slice(entry.lastIndexOf('/') + 1))]
  return {
    directories,
    files,
    sizes,
    reads,
    FS: {
      isDir: (mode) => mode === 0o040000,
      readdir,
      stat(path) {
        if (directories.has(path)) return { mode: 0o040000, size: 0 }
        if (files.has(path)) return { mode: 0o100000, size: sizes.get(path) ?? files.get(path).length }
        throw new Error('ENOENT')
      },
      mkdir(path) { ensureParent(path); if (failMkdir && path.endsWith('/bad')) throw new Error('simulated mkdir failure'); directories.add(path) },
      writeFile(path, bytes) { ensureParent(path); if ((failWrites && path.endsWith('/second.dat')) || (failRollbackWrite && path.endsWith('/one.dat'))) throw new Error('simulated write failure'); files.set(path, new Uint8Array(bytes)); sizes.delete(path) },
      readFile(path) { if (!files.has(path)) throw new Error('ENOENT'); reads.push(path); return files.get(path) },
      rename(from, to) {
        ensureParent(to)
        if (failRename) throw new Error('simulated rename failure')
        if (files.has(from)) { files.set(to, files.get(from)); files.delete(from); return }
        if (directories.has(from)) { directories.add(to); directories.delete(from); return }
        throw new Error('ENOENT')
      },
      unlink(path) { unlinkCalls += 1; if (failSecondUnlink && unlinkCalls === 2) throw new Error('simulated unlink failure'); if (!files.delete(path)) throw new Error('ENOENT') },
      rmdir(path) {
        if (readdir(path).length > 2) throw new Error('ENOTEMPTY')
        if (!directories.delete(path)) throw new Error('ENOENT')
      },
    },
    failWritesOnSecondArchiveWrite() { failWrites = true },
    failMkdirAtTarget() { failMkdir = true },
    failRename() { failRename = true },
    failSecondUnlink() { failSecondUnlink = true },
    failRollbackWrite() { failRollbackWrite = true },
  }
}

describe('virtual disk file handle', () => {
  it('lists directories first and refuses implicit overwrite or paths outside /data', async () => {
    const memory = createMemoryFs()
    memory.directories.add('/data/projects')
    memory.files.set('/data/readme.txt', new Uint8Array([1]))
    const handle = createFilesHandle(memory, { flushNow: vi.fn() })

    await expect(handle.listDirectory('/data')).resolves.toEqual([
      { name: 'projects', path: '/data/projects', kind: 'directory', size: 0 },
      { name: 'readme.txt', path: '/data/readme.txt', kind: 'file', size: 1 },
    ])
    await expect(handle.rename('/data/readme.txt', '/data/readme.txt')).rejects.toThrow(/exists/i)
    await expect(handle.mkdir('/data/../../escape')).rejects.toThrow(/outside/i)
  })

  it('batches multi-file upload into one persistence flush', async () => {
    const memory = createMemoryFs()
    const storage = { flushNow: vi.fn(async () => {}) }
    const handle = createFilesHandle(memory, storage)

    await handle.uploadFiles([
      { name: 'one.wav', size: 1, arrayBuffer: async () => new Uint8Array([1]).buffer },
      { name: 'two.dat', size: 1, arrayBuffer: async () => new Uint8Array([2]).buffer },
    ], '/data')

    expect(Array.from(memory.files.get('/data/one.wav'))).toEqual([1])
    expect(Array.from(memory.files.get('/data/two.dat'))).toEqual([2])
    expect(storage.flushNow).toHaveBeenCalledTimes(1)
  })

  it('routes every Files mutation through the storage-exclusive barrier, never a direct flush', async () => {
    const memory = createMemoryFs()
    memory.files.set('/data/source.dat', new Uint8Array([1]))
    memory.files.set('/data/remove.dat', new Uint8Array([2]))
    const runMutation = vi.fn(async (_reason, callback) => callback())
    const storage = { runMutation, flushNow: vi.fn(() => { throw new Error('Files bypassed the mutation barrier') }) }
    const handle = createFilesHandle(memory, storage)

    await handle.mkdir('/data/folder')
    await handle.rename('/data/source.dat', '/data/renamed.dat')
    await handle.uploadFiles([{ name: 'upload.dat', size: 1, arrayBuffer: async () => new Uint8Array([3]).buffer }])
    await handle.delete('/data/remove.dat')
    await handle.restoreZip(zipSync({ 'restored.dat': strToU8('restored') }), 'overwrite')

    expect(runMutation.mock.calls.map(([reason]) => reason)).toEqual([
      'files-mkdir', 'files-rename', 'files-upload', 'files-delete', 'zip-restore',
    ])
    expect(storage.flushNow).not.toHaveBeenCalled()
  })

  it('stages a ZIP before mutation and rolls back only work created by a failed apply', async () => {
    const memory = createMemoryFs()
    memory.files.set('/data/keep.dat', new Uint8Array([7]))
    const storage = { flushNow: vi.fn(async () => {}) }
    const handle = createFilesHandle(memory, storage)
    const archive = zipSync({ 'restore/first.dat': strToU8('one'), 'restore/second.dat': strToU8('two') })
    memory.failWritesOnSecondArchiveWrite()

    await expect(handle.restoreZip(archive, 'overwrite')).rejects.toThrow('simulated write failure')
    expect(Array.from(memory.files.get('/data/keep.dat'))).toEqual([7])
    expect(memory.files.has('/data/restore/first.dat')).toBe(false)
    expect(memory.directories.has('/data/restore')).toBe(false)
    expect(storage.flushNow).not.toHaveBeenCalled()
  })

  it('never reads unrelated disk content for preview/restore and only backs up affected overwrite targets', async () => {
    const memory = createMemoryFs()
    memory.files.set('/data/huge-unrelated.dat', new Uint8Array([1]))
    memory.sizes.set('/data/huge-unrelated.dat', 200 * 1024 * 1024)
    memory.files.set('/data/replace.dat', new Uint8Array([7]))
    const handle = createFilesHandle(memory, { flushNow: vi.fn(async () => {}) })
    const archive = zipSync({ 'replace.dat': strToU8('new'), 'second.dat': strToU8('two') })
    memory.failWritesOnSecondArchiveWrite()

    await expect(handle.previewRestore(archive)).resolves.toMatchObject({ conflicts: [{ path: '/data/replace.dat' }] })
    expect(memory.reads).toEqual([])
    await expect(handle.restoreZip(archive, 'overwrite')).rejects.toThrow('simulated write failure')
    expect(memory.reads).toEqual(['/data/replace.dat'])
    expect(Array.from(memory.files.get('/data/replace.dat'))).toEqual([7])
  })

  it('preflights export before reading oversized files and rolls back failed upload batches', async () => {
    const memory = createMemoryFs()
    memory.files.set('/data/too-large.dat', new Uint8Array([1]))
    memory.sizes.set('/data/too-large.dat', 33 * 1024 * 1024)
    const handle = createFilesHandle(memory, { flushNow: vi.fn(async () => {}) })
    expect(() => handle.exportDiskZip()).toThrow(/limit/i)
    expect(memory.reads).toEqual([])

    memory.files.delete('/data/too-large.dat'); memory.sizes.delete('/data/too-large.dat')
    memory.failWritesOnSecondArchiveWrite()
    await expect(handle.uploadFiles([
      { name: 'first.dat', size: 1, arrayBuffer: async () => new Uint8Array([1]).buffer },
      { name: 'second.dat', size: 1, arrayBuffer: async () => new Uint8Array([2]).buffer },
    ])).rejects.toThrow('simulated write failure')
    expect(memory.files.has('/data/first.dat')).toBe(false)
    await expect(handle.uploadFiles([{ name: '../unsafe', size: 0, arrayBuffer: async () => new ArrayBuffer(0) }])).rejects.toThrow(/unsafe/i)
    await expect(handle.uploadFiles([{ name: 'same.dat', size: 0, arrayBuffer: async () => new ArrayBuffer(0) }, { name: 'same.dat', size: 0, arrayBuffer: async () => new ArrayBuffer(0) }])).rejects.toThrow(/duplicate/i)
  })

  it('rejects directory/file restore conflicts before mutation in both directions', async () => {
    const fileMemory = createMemoryFs(); fileMemory.files.set('/data/name', new Uint8Array([1]))
    const directoryMemory = createMemoryFs(); directoryMemory.directories.add('/data/name')
    await expect(createFilesHandle(fileMemory, {}).restoreZip(zipSync({ 'name/child.dat': strToU8('x') }), 'overwrite')).rejects.toThrow(/conflict/i)
    await expect(createFilesHandle(directoryMemory, {}).restoreZip(zipSync({ name: strToU8('x') }), 'overwrite')).rejects.toThrow(/conflict/i)
    expect(fileMemory.files.has('/data/name')).toBe(true)
    expect(directoryMemory.directories.has('/data/name')).toBe(true)
  })

  it('removes only newly-created parents when mkdir or rename itself fails', async () => {
    const memory = createMemoryFs()
    memory.files.set('/data/source.dat', new Uint8Array([1]))
    const handle = createFilesHandle(memory, { flushNow: vi.fn(async () => {}) })
    memory.failMkdirAtTarget()
    await expect(handle.mkdir('/data/new/bad')).rejects.toThrow('simulated mkdir failure')
    expect(memory.directories.has('/data/new')).toBe(false)
    memory.failRename()
    await expect(handle.rename('/data/source.dat', '/data/other/target.dat')).rejects.toThrow('simulated rename failure')
    expect(memory.directories.has('/data/other')).toBe(false)
    expect(memory.files.has('/data/source.dat')).toBe(true)
  })

  it('preflights File names and sizes before calling arrayBuffer, then verifies the actual byte count', async () => {
    const memory = createMemoryFs()
    const handle = createFilesHandle(memory, { flushNow: vi.fn(async () => {}) })
    const oversizedRead = vi.fn(async () => new ArrayBuffer(0))
    await expect(handle.uploadFiles([{ name: 'large.dat', size: 33 * 1024 * 1024, arrayBuffer: oversizedRead }])).rejects.toThrow(/size limit/i)
    expect(oversizedRead).not.toHaveBeenCalled()
    const changedRead = vi.fn(async () => new Uint8Array([1]).buffer)
    await expect(handle.uploadFiles([{ name: 'changed.dat', size: 2, arrayBuffer: changedRead }])).rejects.toThrow(/changed while reading/i)
    expect(memory.files.has('/data/changed.dat')).toBe(false)
  })

  it('returns an export Blob synchronously so a UI click retains its trusted download gesture', () => {
    const memory = createMemoryFs()
    memory.files.set('/data/export.dat', new Uint8Array([1, 2, 3]))
    const handle = createFilesHandle(memory, {})

    const archive = handle.exportZip()

    expect(archive).toBeInstanceOf(Blob)
    expect(typeof archive.then).toBe('undefined')
  })

  it('restores a bounded deleted subtree when its second unlink fails without flushing', async () => {
    const memory = createMemoryFs()
    memory.directories.add('/data/delete')
    memory.files.set('/data/delete/one.dat', new Uint8Array([1]))
    memory.files.set('/data/delete/two.dat', new Uint8Array([2]))
    const storage = { flushNow: vi.fn(async () => {}) }
    const handle = createFilesHandle(memory, storage)
    memory.failSecondUnlink()
    await expect(handle.delete('/data/delete')).rejects.toThrow('simulated unlink failure')
    expect(Array.from(memory.files.get('/data/delete/one.dat'))).toEqual([1])
    expect(Array.from(memory.files.get('/data/delete/two.dat'))).toEqual([2])
    expect(storage.flushNow).not.toHaveBeenCalled()
  })

  it('fails storage closed if a delete rollback cannot restore its backup', async () => {
    const memory = createMemoryFs()
    memory.directories.add('/data/delete')
    memory.files.set('/data/delete/one.dat', new Uint8Array([1]))
    memory.files.set('/data/delete/two.dat', new Uint8Array([2]))
    let closed = null
    const storage = {
      failClosed: vi.fn((error) => { closed = error }),
      flushNow: vi.fn(() => closed ? Promise.reject(closed) : Promise.resolve()),
    }
    const handle = createFilesHandle(memory, storage)
    memory.failSecondUnlink(); memory.failRollbackWrite()
    await expect(handle.delete('/data/delete')).rejects.toThrow(/rollback failed/i)
    expect(storage.failClosed).toHaveBeenCalledTimes(1)
    await expect(storage.flushNow('later')).rejects.toBe(closed)
  })

  it('merges matching directories while applying file conflict policies during ZIP round-trips', async () => {
    const memory = createMemoryFs()
    memory.directories.add('/data/projects')
    memory.files.set('/data/projects/demo.dat', strToU8('old'))
    const handle = createFilesHandle(memory, { flushNow: vi.fn(async () => {}) })
    const archive = createDiskZip([
      { path: '/data/projects', kind: 'directory' },
      { path: '/data/projects/demo.dat', kind: 'file', bytes: strToU8('new') },
    ])
    const preview = await handle.previewRestore(archive)
    expect(preview.conflicts).toEqual([expect.objectContaining({ path: '/data/projects/demo.dat', reason: 'replace' })])
    expect(planZipRestore(preview, 'overwrite').directories).toContain('/data/projects')
    await handle.restoreZip(archive, 'overwrite')
    expect(new TextDecoder().decode(memory.files.get('/data/projects/demo.dat'))).toBe('new')
    await handle.restoreZip(archive, 'keep-both')
    expect(new TextDecoder().decode(memory.files.get('/data/projects/demo (2).dat'))).toBe('new')
  })
})
