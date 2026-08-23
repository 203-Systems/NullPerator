const root = '/data'

export function createFilesStore(files) {
  const listeners = new Set()
  let snapshot = Object.freeze({ path: root, entries: [], loading: Boolean(files), error: files ? null : 'Virtual disk is unavailable' })
  const publish = (next) => {
    snapshot = Object.freeze({ ...snapshot, ...next })
    for (const listener of listeners) listener(snapshot)
  }
  const refresh = async (path = snapshot.path) => {
    if (!files) return snapshot
    publish({ loading: true, error: null })
    try {
      const entries = await files.listDirectory(path)
      publish({ path, entries, loading: false })
    } catch (error) {
      publish({ loading: false, error: error instanceof Error ? error.message : String(error) })
      throw error
    }
    return snapshot
  }
  const mutate = async (operation) => {
    publish({ loading: true, error: null })
    try { await operation(); return refresh() }
    catch (error) {
      publish({ loading: false, error: error instanceof Error ? error.message : String(error) })
      throw error
    }
  }
  return Object.freeze({
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    getSnapshot: () => snapshot,
    refresh,
    navigate(path) { return refresh(path) },
    mkdir(name) { return mutate(() => files.mkdir(`${snapshot.path}/${name}`)) },
    rename(path, name) { return mutate(() => files.rename(path, `${snapshot.path}/${name}`)) },
    delete(path) { return mutate(() => files.delete(path)) },
    upload(filesToUpload) { return mutate(() => files.uploadFiles(filesToUpload, snapshot.path)) },
    previewRestore(input) { return files.previewRestore(input) },
    restore(input, policy) { return mutate(() => files.restoreZip(input, policy)) },
    download(path) { return files.downloadFile(path) },
    exportZip() { return files.exportZip() },
  })
}
