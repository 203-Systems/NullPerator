import { compareManifests, createManifest } from './syncManifest.js'

const directions = new Set(['pull', 'push', 'bidirectional'])
const policies = new Set(['keep-browser', 'keep-host', 'keep-both'])

function errorMessage(error) { return error instanceof Error ? error.message : String(error) }

function sameCurrent(left, right) {
  if (!left || !right || left.kind !== right.kind) return false
  return left.kind === 'directory' || (left.size === right.size && left.hash === right.hash)
}

function ordered(operations) {
  const directoryReplacements = operations.filter((operation) =>
    operation.type === 'write' && operation.source?.kind === 'file' && operation.target?.kind === 'directory')
  return operations.filter((operation) => !(
    operation.type === 'delete' && directoryReplacements.some((replacement) =>
      operation.path.startsWith(`${replacement.path}/`))))
    .sort((left, right) => {
    if (left.type !== right.type) return left.type === 'delete' ? 1 : -1
    if (left.type === 'delete') return right.path.length - left.path.length || left.path.localeCompare(right.path)
    if (left.source?.kind !== right.source?.kind) return left.source?.kind === 'directory' ? -1 : 1
    return left.path.localeCompare(right.path)
    })
}

function sibling(path, label, occupied) {
  const slash = path.lastIndexOf('/')
  const parent = slash < 0 ? '' : path.slice(0, slash + 1)
  const name = path.slice(slash + 1)
  const dot = name.lastIndexOf('.')
  const stem = dot > 0 ? name.slice(0, dot) : name
  const extension = dot > 0 ? name.slice(dot) : ''
  let candidate = `${parent}${stem} (${label})${extension}`
  for (let number = 2; occupied.has(candidate); number += 1) candidate = `${parent}${stem} (${label} ${number})${extension}`
  occupied.add(candidate)
  return candidate
}

export function createSyncCoordinator({ browser, host, loadBase, saveBase, unmount } = {}) {
  if (!browser?.manifest || !browser?.apply || !host?.manifest || !host?.apply || !loadBase || !saveBase) throw new Error('Sync coordinator endpoints are required')
  const listeners = new Set()
  let queue = Promise.resolve()
  let snapshot = Object.freeze({ state: 'mounted', direction: null, progress: null, conflicts: [], error: null, lastSuccessfulSync: null })
  let pending = null

  const publish = (next) => {
    snapshot = Object.freeze({ ...snapshot, ...next })
    for (const listener of listeners) listener(snapshot)
  }
  const enqueue = (work) => {
    const result = queue.then(work, work)
    queue = result.catch(() => {})
    return result
  }
  const apply = async (target, source, operations) => {
    if (!operations.length) return
    publish({ progress: { phase: 'apply', completed: 0, total: operations.length } })
    await target.apply(ordered(operations), source, (completed) => publish({ progress: { phase: 'apply', completed, total: operations.length } }))
  }
  const within = (candidate, root) => candidate === root || candidate.startsWith(`${root}/`)
  const overlaps = (left, right) => within(left, right) || within(right, left)
  const resolutionRoot = (path, losingOperations) => {
    let root = path
    for (const operation of losingOperations) {
      if (within(path, operation.path) && operation.path.length < root.length) root = operation.path
    }
    return root
  }
  const reconcileSubtree = (sourceManifest, targetManifest, rootPath) => {
    const paths = [...new Set([...sourceManifest.keys(), ...targetManifest.keys()])]
      .filter((path) => within(path, rootPath))
      .sort()
    const operations = []
    for (const path of paths) {
      const source = sourceManifest.get(path)
      const target = targetManifest.get(path)
      if (!sameCurrent(source, target)) operations.push({
        path,
        source: source ?? null,
        target: target ?? null,
        type: source ? 'write' : 'delete',
      })
    }
    return operations
  }
  const preserveVariant = async (sourceEndpoint, sourceManifest, rootPath, siblingPath) => {
    const operations = []
    for (const [sourcePath, entry] of sourceManifest) {
      if (!within(sourcePath, rootPath)) continue
      operations.push({
        path: `${siblingPath}${sourcePath.slice(rootPath.length)}`,
        source: entry,
        target: null,
        type: 'write',
      })
    }
    await apply(browser, sourceEndpoint, operations)
    await apply(host, sourceEndpoint, operations)
  }
  const advanceBase = async () => {
    const browserManifest = await browser.manifest((progress) => publish({ progress: { phase: 'verify-browser', ...progress, total: null } }))
    const hostManifest = await host.manifest((progress) => publish({ progress: { phase: 'verify-host', ...progress, total: null } }))
    const comparison = compareManifests(createManifest(), browserManifest, hostManifest)
    if (comparison.push.length || comparison.pull.length || comparison.conflicts.length) throw new Error('Host folder sync did not converge after apply')
    await saveBase(browserManifest)
    return browserManifest
  }
  const plan = (direction, diff, browserManifest, hostManifest) => {
    if (direction === 'push' || direction === 'pull') {
      const target = direction === 'push' ? 'toHost' : 'toBrowser'
      const sourceManifest = direction === 'push' ? browserManifest : hostManifest
      const targetManifest = direction === 'push' ? hostManifest : browserManifest
      const operations = []
      for (const path of [...new Set([...sourceManifest.keys(), ...targetManifest.keys()])].sort()) {
        const source = sourceManifest.get(path)
        const current = targetManifest.get(path)
        if (!sameCurrent(source, current)) operations.push({ path, source: source ?? null, target: current ?? null, type: source ? 'write' : 'delete' })
      }
      return target === 'toHost' ? { toHost: operations, toBrowser: [] } : { toHost: [], toBrowser: operations }
    }
    return { toHost: diff.push, toBrowser: diff.pull }
  }

  async function sync(direction) {
    if (!directions.has(direction)) throw new Error('Sync direction must be pull, push, or bidirectional')
    return enqueue(async () => {
      publish({ state: 'syncing', direction, conflicts: [], error: null, progress: null })
      try {
        const base = await loadBase()
        const browserManifest = await browser.manifest((progress) => publish({ progress: { phase: 'scan-browser', ...progress, total: null } }))
        const hostManifest = await host.manifest((progress) => publish({ progress: { phase: 'scan-host', ...progress, total: null } }))
        const diff = compareManifests(base, browserManifest, hostManifest)
        if (direction === 'bidirectional' && diff.conflicts.length) {
          pending = {
            base,
            browserManifest,
            hostManifest,
            conflicts: [...diff.conflicts],
            toHost: [...diff.push],
            toBrowser: [...diff.pull],
            occupied: new Set([...browserManifest.keys(), ...hostManifest.keys()]),
          }
          publish({ state: 'conflict', conflicts: diff.conflicts, progress: null })
          throw new Error(`Host folder sync has ${diff.conflicts.length} conflict(s)`)
        }
        const operations = plan(direction, diff, browserManifest, hostManifest)
        await apply(host, browser, operations.toHost)
        await apply(browser, host, operations.toBrowser)
        await advanceBase()
        pending = null
        publish({ state: 'mounted', progress: null, lastSuccessfulSync: new Date().toISOString() })
      } catch (error) {
        if (snapshot.state !== 'conflict') publish({ state: 'failed', error: errorMessage(error), progress: null })
        throw error
      }
    })
  }

  async function resolve(path, policy) {
    if (!policies.has(policy)) throw new Error('Conflict policy is invalid')
    return enqueue(async () => {
      const conflict = pending?.conflicts.find((candidate) => candidate.path === path)
      if (!conflict) throw new Error('Unknown host-folder conflict')
      publish({ state: 'syncing', error: null, progress: null })
      try {
        if (policy === 'keep-browser') {
          const rootPath = resolutionRoot(path, pending.toBrowser)
          await apply(host, browser, reconcileSubtree(pending.browserManifest, pending.hostManifest, rootPath))
          pending.conflicts = pending.conflicts.filter((candidate) => !within(candidate.path, rootPath))
          pending.toHost = pending.toHost.filter((operation) => !overlaps(operation.path, rootPath))
          pending.toBrowser = pending.toBrowser.filter((operation) => !overlaps(operation.path, rootPath))
        }
        else if (policy === 'keep-host') {
          const rootPath = resolutionRoot(path, pending.toHost)
          await apply(browser, host, reconcileSubtree(pending.hostManifest, pending.browserManifest, rootPath))
          pending.conflicts = pending.conflicts.filter((candidate) => !within(candidate.path, rootPath))
          pending.toHost = pending.toHost.filter((operation) => !overlaps(operation.path, rootPath))
          pending.toBrowser = pending.toBrowser.filter((operation) => !overlaps(operation.path, rootPath))
        }
        else {
          if (conflict.browser) {
            const browserSibling = sibling(path, 'browser', pending.occupied)
            await preserveVariant(browser, pending.browserManifest, path, browserSibling)
          }
          if (conflict.host) {
            const hostSibling = sibling(path, 'host', pending.occupied)
            await preserveVariant(host, pending.hostManifest, path, hostSibling)
          }
          await apply(browser, host, [{ path, source: null, target: conflict.browser, type: 'delete' }])
          await apply(host, browser, [{ path, source: null, target: conflict.host, type: 'delete' }])
          pending.conflicts = pending.conflicts.filter((candidate) => !within(candidate.path, path))
          pending.toHost = pending.toHost.filter((operation) => !overlaps(operation.path, path))
          pending.toBrowser = pending.toBrowser.filter((operation) => !overlaps(operation.path, path))
        }
        pending.conflicts = pending.conflicts.filter((candidate) => candidate.path !== path)
        if (pending.conflicts.length) {
          publish({ state: 'conflict', conflicts: pending.conflicts, progress: null })
          return
        }
        await apply(host, browser, pending.toHost)
        await apply(browser, host, pending.toBrowser)
        await advanceBase()
        pending = null
        publish({ state: 'mounted', conflicts: [], progress: null, lastSuccessfulSync: new Date().toISOString() })
      } catch (error) {
        publish({ state: 'failed', error: errorMessage(error), progress: null })
        throw error
      }
    })
  }

  return Object.freeze({
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    snapshot: () => snapshot,
    waitForIdle: () => queue,
    syncHostFolder: sync,
    resolveConflict: resolve,
    unmountHostFolder() {
      return enqueue(async () => {
        await unmount?.()
        pending = null
        publish({ state: 'unmounted', direction: null, progress: null, conflicts: [] })
      })
    },
  })
}
