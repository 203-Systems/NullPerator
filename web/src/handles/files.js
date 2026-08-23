import { normalizePersistentPath } from './filesystem.js'
import { createDiskZip, planZipRestore, previewZipRestore, ZIP_LIMITS } from '../storage/zip.js'

const dataRoot = '/data'
const nameOf = (path) => path.slice(path.lastIndexOf('/') + 1)
const parentOf = (path) => path.slice(0, path.lastIndexOf('/')) || '/'

function pathFor(path) { return normalizePersistentPath(path) }
function exists(FS, path) { try { FS.stat(path); return true } catch { return false } }
function directory(FS, path) { return FS.isDir(FS.stat(path).mode) }
function validUploadName(name) {
  if (typeof name !== 'string' || !name || name === '.' || name === '..' || name.includes('\0') || name.includes('/') || name.includes('\\')) {
    throw new Error('Unsafe upload filename')
  }
  return name
}

export function createFilesHandle(module, storage, options = {}) {
  const { FS } = module
  if (!FS) throw new Error('WASM module filesystem is unavailable')
  const sync = async (reason) => storage?.flushNow?.(reason)
  const mutate = async (reason, callback) => storage?.runMutation
    ? storage.runMutation(reason, callback)
    : (async () => { const result = await callback(); await sync(reason); return result })()
  const failClosed = (primary, failures) => {
    const combined = new Error(`${primary instanceof Error ? primary.message : String(primary)}; rollback failed: ${failures.map((failure) => failure instanceof Error ? failure.message : String(failure)).join('; ')}`)
    storage?.failClosed?.(combined)
    return combined
  }
  const listDirectoryNow = (path = dataRoot) => {
    path = pathFor(path)
    if (!directory(FS, path)) throw new Error('Not a directory')
    return FS.readdir(path).filter((name) => name !== '.' && name !== '..').map((name) => {
      const child = `${path}/${name}`
      const stat = FS.stat(child)
      return { name, path: child, kind: FS.isDir(stat.mode) ? 'directory' : 'file', size: stat.size }
    }).sort((a, b) => a.kind === b.kind ? a.name.localeCompare(b.name) : a.kind === 'directory' ? -1 : 1)
  }
  const listDirectory = async (path = dataRoot) => listDirectoryNow(path)
  const ensureDirectory = (path, created = []) => {
    const pending = []
    for (let cursor = pathFor(path); cursor !== dataRoot; cursor = parentOf(cursor)) pending.unshift(cursor)
    for (const item of pending) {
      if (!exists(FS, item)) { FS.mkdir(item); created.push(item) }
      else if (!directory(FS, item)) throw new Error(`Path is not a directory: ${item}`)
    }
  }
  const metadataNow = () => {
    const output = []
    const visit = (path) => {
      for (const entry of listDirectoryNow(path)) {
        if (entry.kind === 'directory') { output.push(entry); visit(entry.path) }
        else output.push(entry)
      }
    }
    visit(dataRoot)
    return output
  }
  const metadata = async () => metadataNow()
  const remove = (path) => {
    path = pathFor(path)
    if (path === dataRoot) throw new Error('Cannot delete /data')
    if (directory(FS, path)) {
      for (const name of FS.readdir(path).filter((name) => name !== '.' && name !== '..')) remove(`${path}/${name}`)
      FS.rmdir(path)
    } else FS.unlink(path)
  }
  const backupSubtree = (path) => {
    const entries = []
    const visit = (current) => {
      const stat = FS.stat(current)
      const kind = FS.isDir(stat.mode) ? 'directory' : 'file'
      entries.push({ path: current, kind, size: stat.size })
      if (kind === 'directory') for (const name of FS.readdir(current).filter((name) => name !== '.' && name !== '..')) visit(`${current}/${name}`)
    }
    visit(path)
    if (entries.length > ZIP_LIMITS.maxEntries) throw new Error('Delete rollback exceeds entry limit')
    let total = 0
    for (const entry of entries) {
      if (entry.kind !== 'file') continue
      if (entry.size > ZIP_LIMITS.maxFileBytes) throw new Error('Delete rollback file exceeds size limit')
      total += entry.size
      if (total > ZIP_LIMITS.maxInflatedBytes) throw new Error('Delete rollback exceeds size limit')
    }
    return entries.map((entry) => entry.kind === 'file' ? { ...entry, bytes: new Uint8Array(FS.readFile(entry.path)) } : entry)
  }
  const restoreSubtree = (entries) => {
    const failures = []
    for (const entry of entries.filter((entry) => entry.kind === 'directory').sort((left, right) => left.path.length - right.path.length)) {
      try { if (!exists(FS, entry.path)) ensureDirectory(entry.path) } catch (error) { failures.push(error) }
    }
    for (const entry of entries.filter((entry) => entry.kind === 'file')) {
      try { ensureDirectory(parentOf(entry.path)); FS.writeFile(entry.path, entry.bytes) } catch (error) { failures.push(error) }
    }
    return failures
  }
  const rollbackDirectories = (created) => {
    const failures = []
    for (const path of [...created].reverse()) {
      try {
        if (exists(FS, path) && directory(FS, path) && FS.readdir(path).filter((name) => name !== '.' && name !== '..').length === 0) FS.rmdir(path)
      } catch (error) { failures.push(error) }
    }
    return failures
  }
  const exportEntries = () => {
    const entries = metadataNow()
    if (entries.length > ZIP_LIMITS.maxEntries) throw new Error('Disk export exceeds ZIP entry limit')
    let total = 0
    for (const entry of entries) {
      if (entry.kind !== 'file') continue
      if (entry.size > ZIP_LIMITS.maxFileBytes) throw new Error(`Disk export file exceeds ZIP limit: ${entry.path}`)
      total += entry.size
      if (total > ZIP_LIMITS.maxInflatedBytes) throw new Error('Disk export exceeds ZIP size limit')
    }
    return entries.map((entry) => entry.kind === 'file' ? { ...entry, bytes: new Uint8Array(FS.readFile(entry.path)) } : entry)
  }
  const handle = {
    listDirectory,
    async mkdir(path) {
      return mutate('files-mkdir', async () => {
        path = pathFor(path); if (exists(FS, path)) throw new Error('Destination already exists')
        const created = []
        try { ensureDirectory(parentOf(path), created); FS.mkdir(path) }
        catch (error) { const failures = rollbackDirectories(created); throw failures.length ? failClosed(error, failures) : error }
      })
    },
    async rename(from, to) {
      return mutate('files-rename', async () => {
        from = pathFor(from); to = pathFor(to)
        if (from === dataRoot || !exists(FS, from) || exists(FS, to)) throw new Error('Destination already exists')
        const created = []
        try { ensureDirectory(parentOf(to), created); FS.rename(from, to) }
        catch (error) { const failures = rollbackDirectories(created); throw failures.length ? failClosed(error, failures) : error }
      })
    },
    async delete(path) {
      return mutate('files-delete', async () => {
        path = pathFor(path)
        if (path === dataRoot) throw new Error('Cannot delete /data')
        const backup = backupSubtree(path)
        try { remove(path) }
        catch (error) {
          const failures = restoreSubtree(backup)
          throw failures.length ? failClosed(error, failures) : error
        }
      })
    },
    async uploadFiles(files, destination = dataRoot) {
      return mutate('files-upload', async () => {
        destination = pathFor(destination); if (!directory(FS, destination)) throw new Error('Upload destination is not a directory')
        const staged = []
        const names = new Set(); let total = 0
        for (const file of files) {
          const name = validUploadName(file.name)
          if (!Number.isSafeInteger(file.size) || file.size < 0) throw new Error('Upload file size is invalid')
          if (names.has(name)) throw new Error('Duplicate upload filename')
          names.add(name); total += file.size
          if (file.size > ZIP_LIMITS.maxFileBytes || total > ZIP_LIMITS.maxInflatedBytes) throw new Error('Upload exceeds size limit')
          if (exists(FS, `${destination}/${name}`)) throw new Error('Destination already exists')
          staged.push({ file, name, size: file.size })
        }
        const created = []
        try {
          for (const item of staged) {
            const bytes = new Uint8Array(await item.file.arrayBuffer())
            if (bytes.byteLength !== item.size) throw new Error('Upload file changed while reading')
            const path = `${destination}/${item.name}`
            created.push(path); FS.writeFile(path, bytes)
          }
        } catch (error) {
          const failures = []
          for (const path of created.reverse()) { try { if (exists(FS, path)) FS.unlink(path) } catch (rollbackError) { failures.push(rollbackError) } }
          throw failures.length ? failClosed(error, failures) : error
        }
      })
    },
    readFile(path) { return new Uint8Array(FS.readFile(pathFor(path))) },
    async downloadFile(path) {
      const bytes = new Uint8Array(FS.readFile(pathFor(path)))
      const blob = new Blob([bytes])
      if (options.download) return options.download(blob, nameOf(path))
      const anchor = document.createElement('a'); const url = URL.createObjectURL(blob)
      anchor.href = url; anchor.download = nameOf(path); anchor.click(); setTimeout(() => URL.revokeObjectURL(url), 0)
    },
    // Export remains synchronous all the way to the caller so the panel can
    // click its download anchor in the original trusted click task.
    exportZip() { return new Blob([createDiskZip(exportEntries())], { type: 'application/zip' }) },
    async previewRestore(input) { return previewZipRestore(input, new Map(metadataNow().map(({ path, kind }) => [path, kind]))) },
    async restoreZip(input, policy) {
      return mutate('zip-restore', async () => {
        const current = await metadata()
        const plan = planZipRestore(previewZipRestore(input, new Map(current.map(({ path, kind }) => [path, kind]))), policy)
        for (const path of plan.directories) if (exists(FS, path) && !directory(FS, path)) throw new Error(`Cannot create directory over file: ${path}`)
        const overwritten = plan.files.filter(({ path }) => exists(FS, path))
        for (const file of overwritten) if (directory(FS, file.path)) throw new Error(`Cannot overwrite directory: ${file.path}`)
        let backupBytes = 0
        for (const file of overwritten) { backupBytes += FS.stat(file.path).size; if (backupBytes > ZIP_LIMITS.maxInflatedBytes) throw new Error('Restore rollback backup exceeds size limit') }
        const backups = overwritten.map((file) => ({ path: file.path, bytes: new Uint8Array(FS.readFile(file.path)) }))
        const createdFiles = []
        const createdDirectories = []
        try {
          for (const path of plan.directories) ensureDirectory(path, createdDirectories)
          for (const file of plan.files) {
            ensureDirectory(parentOf(file.path), createdDirectories)
            if (!exists(FS, file.path)) createdFiles.push(file.path)
            FS.writeFile(file.path, file.bytes)
          }
        } catch (error) {
          const failures = []
          for (const path of createdFiles.reverse()) { try { if (exists(FS, path) && !directory(FS, path)) FS.unlink(path) } catch (rollbackError) { failures.push(rollbackError) } }
          for (const backup of backups) { try { FS.writeFile(backup.path, backup.bytes) } catch (rollbackError) { failures.push(rollbackError) } }
          failures.push(...rollbackDirectories(createdDirectories))
          throw failures.length ? failClosed(error, failures) : error
        }
        return plan
      })
    },
  }
  // Names intentionally match the Task 9 boundary while the short aliases
  // keep panel/store call sites readable.
  handle.deletePath = handle.delete
  handle.exportDiskZip = handle.exportZip
  return Object.freeze(handle)
}
