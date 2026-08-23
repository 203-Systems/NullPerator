export function parseBuildMetadata(raw) {
  return {
    commit: String(raw?.commit ?? 'unknown'),
    dirty: Boolean(raw?.dirty),
    builtAt: String(raw?.builtAt ?? 'unknown'),
    emscripten: String(raw?.emscripten ?? 'unknown'),
  }
}

export const emptyBuildMetadata = Object.freeze(
  parseBuildMetadata(null),
)
