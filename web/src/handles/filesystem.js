const mountPoint = '/data'

export function normalizePersistentPath(path) {
  if (typeof path !== 'string' || !path.startsWith('/')) {
    throw new TypeError('Persistent filesystem paths must be absolute')
  }
  if (path.includes('\\')) {
    throw new TypeError('Persistent filesystem paths must not use backslash separators')
  }
  const parts = []
  for (const part of path.split('/')) {
    if (!part || part === '.') continue
    if (part === '..') {
      if (parts.length <= 1) throw new RangeError('Path is outside /data')
      parts.pop()
      continue
    }
    parts.push(part)
  }
  const normalized = `/${parts.join('/')}`
  if (normalized !== mountPoint && !normalized.startsWith(`${mountPoint}/`)) {
    throw new RangeError('Path is outside /data')
  }
  return normalized
}
