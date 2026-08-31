import { createManifest, hashFileIncrementally, SYNC_LIMITS, validateHostRelativePath } from './syncManifest.js'
import { createSyncCoordinator } from './syncCoordinator.js'

const metadataKey = 'selected-root'
const baseManifestKey = 'base-manifest'
const ignoredNames = new Set(['.DS_Store'])

function errorMessage(error) { return error instanceof Error ? error.message : String(error) }
function isNotFound(error) { return error?.name === 'NotFoundError' || /NotFoundError/.test(errorMessage(error)) }

function indexedDbMetadata(indexedDb = globalThis.indexedDB, databaseName = 'picotracker-host-folder-v1') {
  if (!indexedDb) throw new Error('IndexedDB is unavailable for host-folder metadata')
  const open = new Promise((resolve, reject) => {
    const request = indexedDb.open(databaseName, 1)
    request.onupgradeneeded = () => request.result.createObjectStore('handles')
    request.onsuccess = () => resolve(request.result)
    request.onerror = () => reject(request.error ?? new Error('Unable to open host-folder metadata'))
  })
  const transaction = async (mode, work) => {
    const database = await open
    return new Promise((resolve, reject) => {
      const active = database.transaction('handles', mode)
      const request = work(active.objectStore('handles'))
      let result
      request.onsuccess = () => { result = request.result }
      active.oncomplete = () => resolve(result)
      active.onerror = () => reject(active.error ?? request.error ?? new Error('Host-folder metadata transaction failed'))
      active.onabort = () => reject(active.error ?? request.error ?? new Error('Host-folder metadata transaction aborted'))
    })
  }
  return Object.freeze({
    get: () => transaction('readonly', (store) => store.get(metadataKey)),
    put: (handle) => transaction('readwrite', (store) => store.put(handle, metadataKey)),
    delete: () => transaction('readwrite', (store) => store.delete(metadataKey)),
    getBase: async () => {
      const value = await transaction('readonly', (store) => store.get(baseManifestKey))
      return createManifest(value ?? [])
    },
    putBase: (manifest) => transaction('readwrite', (store) => store.put([...manifest.values()], baseManifestKey)),
    deleteBase: () => transaction('readwrite', (store) => store.delete(baseManifestKey)),
  })
}

function assertDirectoryHandle(handle) {
  if (!handle || handle.kind !== 'directory' || typeof handle.entries !== 'function') throw new Error('Selected host folder is not a directory')
  return handle
}

async function permission(handle, mode = 'readwrite') {
  if (typeof handle.queryPermission !== 'function') return 'granted'
  return handle.queryPermission({ mode })
}

async function requestPermission(handle) {
  if (typeof handle.requestPermission !== 'function') return permission(handle)
  return handle.requestPermission({ mode: 'readwrite' })
}

async function directoryAt(root, relativePath, create = false) {
  let current = root
  if (!relativePath) return current
  for (const part of validateHostRelativePath(relativePath).split('/')) current = await current.getDirectoryHandle(part, { create })
  return current
}

async function parentAt(root, path, create = false) {
  const checked = validateHostRelativePath(path)
  const slash = checked.lastIndexOf('/')
  return { directory: await directoryAt(root, slash < 0 ? '' : checked.slice(0, slash), create), name: slash < 0 ? checked : checked.slice(slash + 1) }
}

export async function scanHostFolder(root, options = {}) {
  assertDirectoryHandle(root)
  const limits = { ...SYNC_LIMITS, ...options.limits }
  const entries = []
  const seen = new Set()
  let totalBytes = 0
  let hashedBytes = 0
  let entryCount = 0
  const pending = [{ handle: root, prefix: '' }]
  while (pending.length) {
    const { handle, prefix } = pending.pop()
    const children = []
    for await (const [name, child] of handle.entries()) {
      if (ignoredNames.has(name)) continue
      validateHostRelativePath(name)
      if (entryCount + children.length >= limits.maxEntries) throw new Error('Host folder exceeds entry limit')
      children.push([name, child])
    }
    children.sort(([left], [right]) => left.localeCompare(right))
    for (const [name, child] of children) {
      const path = prefix ? `${prefix}/${name}` : name
      validateHostRelativePath(path)
      if (seen.has(path)) throw new Error(`Duplicate host folder path: ${path}`)
      seen.add(path)
      if (++entryCount > limits.maxEntries) throw new Error('Host folder exceeds entry limit')
      if (child.kind === 'directory') {
        entries.push({ path, kind: 'directory', size: 0 })
        pending.push({ handle: child, prefix: path })
        options.onProgress?.({ entries: entryCount, bytes: hashedBytes })
      } else if (child.kind === 'file') {
        const file = await child.getFile()
        if (file.size > limits.maxFileBytes) throw new Error(`Host file exceeds size limit: ${path}`)
        totalBytes += file.size
        if (totalBytes > limits.maxTotalBytes) throw new Error('Host folder exceeds total size limit')
        entries.push({
          path,
          kind: 'file',
          size: file.size,
          hash: await hashFileIncrementally(file, {
            ...options,
            maxFileBytes: limits.maxFileBytes,
            onChunk(size) {
              hashedBytes += size
              options.onChunk?.(size)
              options.onProgress?.({ entries: entryCount, bytes: hashedBytes })
            },
          }),
        })
      } else throw new Error(`Unsupported host folder entry: ${path}`)
    }
  }
  return createManifest(entries)
}

export function createHostFolderEndpoint(root, options = {}) {
  assertDirectoryHandle(root)
  const chunkBytes = options.chunkBytes ?? SYNC_LIMITS.chunkBytes
  if (!Number.isSafeInteger(chunkBytes) || chunkBytes <= 0) throw new Error('Invalid host-folder chunk size')
  const removeIfPresent = async (path) => {
    try {
      const { directory, name } = await parentAt(root, path)
      await directory.removeEntry(name, { recursive: true })
    }
    catch (error) { if (!isNotFound(error)) throw error }
  }
  const handleAt = async (path) => {
    try {
      const { directory, name } = await parentAt(root, path)
      for await (const [candidate, handle] of directory.entries()) if (candidate === name) return handle
      return null
    } catch (error) {
      if (isNotFound(error)) return null
      throw error
    }
  }
  const affectedRoots = async (operations) => {
    const paths = []
    for (const operation of operations) {
      let rootPath = validateHostRelativePath(operation.path)
      while (rootPath.includes('/')) {
        const parent = rootPath.slice(0, rootPath.lastIndexOf('/'))
        if (await handleAt(parent)) break
        rootPath = parent
      }
      paths.push(rootPath)
    }
    const unique = [...new Set(paths)].sort((left, right) => left.length - right.length || left.localeCompare(right))
    return unique.filter((path, index) => !unique.slice(0, index).some((ancestor) => path.startsWith(`${ancestor}/`)))
  }
  const backupRoots = async (roots) => {
    const entries = []
    let totalBytes = 0
    const visit = async (path, handle) => {
      if (entries.length >= (options.limits?.maxEntries ?? SYNC_LIMITS.maxEntries)) throw new Error('Host rollback exceeds entry limit')
      if (handle.kind === 'directory') {
        entries.push({ path, kind: 'directory' })
        const children = []
        for await (const [name, child] of handle.entries()) children.push([name, child])
        children.sort(([left], [right]) => left.localeCompare(right))
        for (const [name, child] of children) await visit(`${path}/${name}`, child)
        return
      }
      const file = await handle.getFile()
      const maxFileBytes = options.limits?.maxFileBytes ?? SYNC_LIMITS.maxFileBytes
      if (file.size > maxFileBytes) throw new Error(`Host rollback file exceeds size limit: ${path}`)
      totalBytes += file.size
      if (totalBytes > (options.limits?.maxTotalBytes ?? SYNC_LIMITS.maxTotalBytes)) throw new Error('Host rollback exceeds total size limit')
      entries.push({ path, kind: 'file', bytes: new Uint8Array(await file.arrayBuffer()) })
    }
    for (const path of roots) {
      const handle = await handleAt(path)
      if (handle) await visit(path, handle)
    }
    return entries
  }
  const restoreRoots = async (roots, entries) => {
    const failures = []
    for (const path of [...roots].reverse()) {
      try { await removeIfPresent(path) } catch (error) { failures.push(error) }
    }
    for (const entry of entries.filter(({ kind }) => kind === 'directory').sort((left, right) => left.path.length - right.path.length)) {
      try { await directoryAt(root, entry.path, true) } catch (error) { failures.push(error) }
    }
    for (const entry of entries.filter(({ kind }) => kind === 'file')) {
      try {
        const { directory, name } = await parentAt(root, entry.path, true)
        const writable = await (await directory.getFileHandle(name, { create: true })).createWritable()
        await writable.write(entry.bytes)
        await writable.close()
      } catch (error) { failures.push(error) }
    }
    return failures
  }
  return Object.freeze({
    manifest: (onProgress) => scanHostFolder(root, { ...options, onProgress }),
    async copyFile(path, sink) {
      const { directory, name } = await parentAt(root, path)
      const file = await (await directory.getFileHandle(name)).getFile()
      if (file.size > (options.limits?.maxFileBytes ?? SYNC_LIMITS.maxFileBytes)) throw new Error(`Host file exceeds size limit: ${path}`)
      for (let offset = 0; offset < file.size; offset += chunkBytes) {
        await sink.write(new Uint8Array(await file.slice(offset, Math.min(file.size, offset + chunkBytes)).arrayBuffer()))
      }
    },
    async apply(operations, source, onProgress = () => {}) {
      const roots = await affectedRoots(operations)
      const backup = await backupRoots(roots)
      let completed = 0
      try {
        for (const operation of operations) {
          const path = validateHostRelativePath(operation.path)
          if (operation.type === 'delete') {
            await removeIfPresent(path)
          } else if (operation.source?.kind === 'directory') {
            if (operation.target && operation.target.kind !== 'directory') await removeIfPresent(path)
            await directoryAt(root, path, true)
          } else if (operation.source?.kind === 'file') {
            if (operation.target && operation.target.kind !== 'file') await removeIfPresent(path)
            const { directory, name } = await parentAt(root, path, true)
            const writable = await (await directory.getFileHandle(name, { create: true })).createWritable()
            try {
              await source.copyFile(operation.source.path, writable)
              await writable.close()
            } catch (error) {
              try { await writable.abort?.() } catch { /* Preserve the original write failure. */ }
              throw error
            }
          } else throw new Error(`Invalid host sync operation: ${path}`)
          onProgress(++completed)
        }
      } catch (error) {
        const failures = await restoreRoots(roots, backup)
        if (failures.length) throw new Error(`${errorMessage(error)}; host rollback failed: ${failures.map(errorMessage).join('; ')}`)
        throw error
      }
    },
  })
}

export function createHostFolderManager(options = {}) {
  let metadata = options.metadata ?? null
  const getMetadata = () => metadata ??= indexedDbMetadata(options.indexedDB, options.databaseName)
  const picker = options.picker ?? globalThis.showDirectoryPicker
  const listeners = new Set()
  let root = null
  let coordinator = options.coordinator ?? null
  let coordinatorRoot = null
  let coordinatorUnsubscribe = () => {}
  let queue = Promise.resolve()
  let snapshot = Object.freeze({ state: typeof picker === 'function' ? 'unmounted' : 'unsupported', permission: 'prompt', error: null, name: null })
  const publish = (next) => {
    snapshot = Object.freeze({ ...snapshot, ...next })
    for (const listener of listeners) listener(snapshot)
  }
  const enqueue = (work) => {
    const result = queue.then(work, work)
    queue = result.catch(() => {})
    return result
  }
  const sameEntry = async (left, right) => {
    if (!left || !right) return false
    if (left === right) return true
    if (typeof left.isSameEntry !== 'function') return false
    try { return await left.isSameEntry(right) } catch { return false }
  }
  const accept = async (handle, request, restore = false) => {
    const candidate = assertDirectoryHandle(handle)
    let current = await permission(candidate)
    if (request && current !== 'granted') current = await requestPermission(candidate)
    if (current !== 'granted') {
      root = candidate
      publish({ state: current === 'denied' ? 'denied' : 'prompt', permission: current, error: null, name: root.name ?? null })
      return snapshot
    }
    const unchanged = await sameEntry(root, candidate)
    // A denied selection becomes the visible root without replacing the
    // active coordinator. Compare a later grant with that coordinator's root,
    // not only with the denied candidate now stored in `root`.
    const rebind = coordinatorRoot
      ? !(await sameEntry(coordinatorRoot, candidate))
      : !unchanged
    if (rebind && coordinatorRoot) {
      coordinatorUnsubscribe(); coordinatorUnsubscribe = () => {}
      coordinator = null
      coordinatorRoot = null
    }
    root = candidate
    if (!restore && rebind) await getMetadata().deleteBase?.()
    await getMetadata().put(root)
    ensureCoordinator()
    publish({ state: 'mounted', permission: current, error: null, name: root.name ?? null })
    return snapshot
  }
  const loadBase = async () => getMetadata().getBase ? getMetadata().getBase() : createManifest()
  const saveBase = async (manifest) => {
    if (getMetadata().putBase) await getMetadata().putBase(manifest)
  }
  const ensureCoordinator = () => {
    if (coordinator || !root || !options.browser) return coordinator
    coordinator = createSyncCoordinator({
      browser: options.browser,
      host: createHostFolderEndpoint(root, options),
      loadBase,
      saveBase,
    })
    coordinatorRoot = root
    coordinatorUnsubscribe()
    coordinatorUnsubscribe = coordinator.subscribe((next) => {
      publish({ state: next.state, error: next.error, conflicts: next.conflicts, progress: next.progress, lastSuccessfulSync: next.lastSuccessfulSync })
    })
    return coordinator
  }
  const requireMountedPermission = async () => {
    if (!root) throw new Error('Host folder sync is unavailable until mounted')
    let current
    try { current = await permission(root) }
    catch (error) {
      publish({ state: 'failed', error: errorMessage(error) })
      throw error
    }
    if (current !== 'granted') {
      publish({ state: current === 'denied' ? 'denied' : 'prompt', permission: current, error: null })
      throw new Error(`Host folder read/write permission is ${current}`)
    }
  }

  return Object.freeze({
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    snapshot: () => snapshot,
    async waitForIdle() {
      await queue
      await coordinator?.waitForIdle?.()
    },
    endpoint: () => root ? createHostFolderEndpoint(root, options) : null,
    attachCoordinator(next) {
      coordinatorUnsubscribe()
      coordinator = next
      coordinatorRoot = null
      coordinatorUnsubscribe = coordinator?.subscribe?.((nextSnapshot) => publish({ state: nextSnapshot.state, error: nextSnapshot.error, conflicts: nextSnapshot.conflicts, progress: nextSnapshot.progress, lastSuccessfulSync: nextSnapshot.lastSuccessfulSync })) ?? (() => {})
    },
    mountHostFolder() {
      return enqueue(async () => {
        if (typeof picker !== 'function') { publish({ state: 'unsupported', error: 'Host folder access is unsupported in this browser.' }); return snapshot }
        try { return await accept(await picker({ mode: 'readwrite' }), true, false) }
        catch (error) { publish({ state: 'failed', error: errorMessage(error) }); throw error }
      })
    },
    restoreHostFolderHandle() {
      return enqueue(async () => {
        try {
          const handle = await getMetadata().get()
          if (!handle) { publish({ state: 'unmounted', permission: 'prompt', error: null, name: null }); return snapshot }
          return await accept(handle, false, true)
        } catch (error) {
          publish({ state: 'failed', error: errorMessage(error) })
          throw error
        }
      })
    },
    syncHostFolder(direction) {
      return enqueue(async () => {
        await requireMountedPermission()
        const active = ensureCoordinator()
        if (!active) throw new Error('Host folder sync is unavailable until mounted')
        return active.syncHostFolder(direction)
      })
    },
    resolveConflict(path, policy) {
      return enqueue(async () => {
        await requireMountedPermission()
        const active = ensureCoordinator()
        if (!active) throw new Error('Host folder sync is unavailable until mounted')
        return active.resolveConflict(path, policy)
      })
    },
    unmountHostFolder() {
      return enqueue(async () => {
        await coordinator?.unmountHostFolder?.()
        coordinatorUnsubscribe(); coordinatorUnsubscribe = () => {}
        coordinator = null
        coordinatorRoot = null
        root = null
        await getMetadata().delete()
        await getMetadata().deleteBase?.()
        publish({ state: 'unmounted', permission: 'prompt', error: null, name: null })
      })
    },
  })
}

export { ignoredNames }
