export const SYNC_LIMITS = Object.freeze({
  maxEntries: 16_384,
  maxFileBytes: 64 * 1024 * 1024,
  maxTotalBytes: 512 * 1024 * 1024,
  chunkBytes: 256 * 1024,
})

const encoder = new TextEncoder()

export function validateHostRelativePath(path) {
  if (typeof path !== 'string' || !path || path.includes('\0') || path.includes('\\') || path.startsWith('/') || /^(?:[A-Za-z]:|\/\/)/.test(path)) {
    throw new Error('Unsafe host-relative path')
  }
  const parts = path.split('/')
  if (parts.some((part) => !part || part === '.' || part === '..')) throw new Error('Unsafe host-relative path')
  return parts.join('/')
}

function assertEntry(entry) {
  const path = validateHostRelativePath(entry?.path)
  if (!['file', 'directory'].includes(entry?.kind)) throw new Error(`Invalid manifest kind: ${path}`)
  if (!Number.isSafeInteger(entry?.size) || entry.size < 0) throw new Error(`Invalid manifest size: ${path}`)
  if (entry.kind === 'file' && (!entry.hash || typeof entry.hash !== 'string')) throw new Error(`Missing content hash: ${path}`)
  return Object.freeze({ path, kind: entry.kind, size: entry.size, hash: entry.kind === 'file' ? entry.hash : null })
}

export function createManifest(entries = []) {
  if (entries.length > SYNC_LIMITS.maxEntries) throw new Error('Host folder exceeds entry limit')
  const manifest = new Map()
  let total = 0
  for (const candidate of entries) {
    const entry = assertEntry(candidate)
    if (manifest.has(entry.path)) throw new Error(`Duplicate manifest path: ${entry.path}`)
    if (entry.kind === 'file') {
      if (entry.size > SYNC_LIMITS.maxFileBytes) throw new Error(`Host file exceeds size limit: ${entry.path}`)
      total += entry.size
      if (total > SYNC_LIMITS.maxTotalBytes) throw new Error('Host folder exceeds total size limit')
    }
    manifest.set(entry.path, entry)
  }
  for (const entry of manifest.values()) {
    let parent = entry.path.slice(0, entry.path.lastIndexOf('/'))
    while (parent) {
      if (manifest.get(parent)?.kind === 'file') throw new Error(`File ancestor in manifest: ${parent}`)
      const slash = parent.lastIndexOf('/')
      parent = slash < 0 ? '' : parent.slice(0, slash)
    }
  }
  return new Map([...manifest.entries()].sort(([left], [right]) => left.localeCompare(right)))
}

function sameEntry(left, right) {
  if (!left || !right || left.kind !== right.kind) return false
  return left.kind === 'directory' || (left.size === right.size && left.hash === right.hash)
}

function changed(base, current) {
  if (!base && !current) return false
  return !sameEntry(base, current)
}

function describeChange(path, source, target) {
  return Object.freeze({
    path,
    source: source ?? null,
    target: target ?? null,
    type: source ? 'write' : 'delete',
  })
}

function within(candidate, root) {
  return candidate === root || candidate.startsWith(`${root}/`)
}

function destructiveAncestor(operation, descendant) {
  return operation.path !== descendant.path
    && within(descendant.path, operation.path)
    && (operation.type === 'delete' || operation.source?.kind === 'file')
}

function structuralConflicts(push, pull, base, browser, host) {
  const roots = new Set()
  for (const browserChange of push) {
    for (const hostChange of pull) {
      if (destructiveAncestor(browserChange, hostChange)) roots.add(browserChange.path)
      if (destructiveAncestor(hostChange, browserChange)) roots.add(hostChange.path)
    }
  }
  return [...roots].sort().map((path) => Object.freeze({
    path,
    base: base.get(path) ?? null,
    browser: browser.get(path) ?? null,
    host: host.get(path) ?? null,
    reason: 'ancestor-delete-vs-descendant-change',
  }))
}

export function compareManifests(base = createManifest(), browser = createManifest(), host = createManifest()) {
  const paths = [...new Set([...base.keys(), ...browser.keys(), ...host.keys()])].sort()
  const result = { push: [], pull: [], converged: [], conflicts: [] }
  for (const path of paths) {
    const prior = base.get(path)
    const browserEntry = browser.get(path)
    const hostEntry = host.get(path)
    const browserChanged = changed(prior, browserEntry)
    const hostChanged = changed(prior, hostEntry)
    if (!browserChanged && !hostChanged) continue
    if (browserChanged && hostChanged) {
      if (sameEntry(browserEntry, hostEntry)) result.converged.push(describeChange(path, browserEntry, hostEntry))
      else {
        const deletion = !browserEntry || !hostEntry
        result.conflicts.push(Object.freeze({ path, base: prior ?? null, browser: browserEntry ?? null, host: hostEntry ?? null, reason: deletion ? 'delete-vs-modify' : 'both-modified' }))
      }
    } else if (browserChanged) result.push.push(describeChange(path, browserEntry, hostEntry))
    else result.pull.push(describeChange(path, hostEntry, browserEntry))
  }
  result.conflicts.push(...structuralConflicts(result.push, result.pull, base, browser, host))
  return Object.freeze(Object.fromEntries(Object.entries(result).map(([key, entries]) => [key, Object.freeze(entries)])))
}

// FNV-1a 64 is implemented incrementally so the browser never needs to
// materialize a host file just to hash it. The size remains part of equality.
export function createIncrementalHasher() {
  let hash = 0xcbf29ce484222325n
  const prime = 0x100000001b3n
  const mask = 0xffffffffffffffffn
  return Object.freeze({
    update(bytes) {
      for (const byte of bytes) hash = (hash ^ BigInt(byte)) * prime & mask
    },
    digest() { return hash.toString(16).padStart(16, '0') },
  })
}

export async function hashBytesIncrementally(bytes, options = {}) {
  const source = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes)
  const chunkBytes = options.chunkBytes ?? SYNC_LIMITS.chunkBytes
  if (!Number.isSafeInteger(chunkBytes) || chunkBytes <= 0) throw new Error('Invalid hash chunk size')
  const hasher = createIncrementalHasher()
  for (let offset = 0; offset < source.byteLength; offset += chunkBytes) {
    const chunk = source.subarray(offset, Math.min(source.byteLength, offset + chunkBytes))
    hasher.update(chunk)
    options.onChunk?.(chunk.byteLength)
    await Promise.resolve()
  }
  return hasher.digest()
}

export async function hashFileIncrementally(file, options = {}) {
  if (!file || !Number.isSafeInteger(file.size) || file.size < 0) throw new Error('Invalid host file')
  if (file.size > (options.maxFileBytes ?? SYNC_LIMITS.maxFileBytes)) throw new Error('Host file exceeds size limit')
  const chunkBytes = options.chunkBytes ?? SYNC_LIMITS.chunkBytes
  if (!Number.isSafeInteger(chunkBytes) || chunkBytes <= 0) throw new Error('Invalid hash chunk size')
  const hasher = createIncrementalHasher()
  for (let offset = 0; offset < file.size; offset += chunkBytes) {
    const bytes = new Uint8Array(await file.slice(offset, Math.min(file.size, offset + chunkBytes)).arrayBuffer())
    hasher.update(bytes)
    options.onChunk?.(bytes.byteLength)
  }
  return hasher.digest()
}

export const manifestPathBytes = (path) => encoder.encode(validateHostRelativePath(path))
