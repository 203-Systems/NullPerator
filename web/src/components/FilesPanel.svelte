<script>
  import { onDestroy } from 'svelte'

  import ConflictDialog from './ConflictDialog.svelte'
  import { createFilesStore } from '../stores/files.js'

  export let files = null
  export let storage = null
  export let hostFolder = null
  export let disabled = false

  let fileStore = null
  let fileStoreHandle = null
  let unsubscribe = () => {}
  let storageHandle = null
  let unsubscribeStorage = () => {}
  let storageSnapshot = null
  let hostFolderHandle = null
  let unsubscribeHostFolder = () => {}
  let hostSnapshot = { state: 'unmounted', conflicts: [], progress: null, error: null, name: null }
  let snapshot = { path: '/data', entries: [], loading: false, error: null }
  let uploadInput
  let restoreInput
  let pendingRestore = null
  let feedback = ''
  $: hostBusy = hostSnapshot?.state === 'syncing' || hostSnapshot?.state === 'conflict'
  $: mutatingDisabled = disabled || !fileStore || snapshot.loading || storageSnapshot?.state === 'syncing' || storageSnapshot?.syncing || storageSnapshot?.mutating || hostBusy

  $: if (files && files !== fileStoreHandle) {
    unsubscribe()
    fileStore = createFilesStore(files)
    fileStoreHandle = files
    unsubscribe = fileStore.subscribe((next) => (snapshot = next))
    fileStore.refresh().catch(() => {})
  } else if (!files && fileStore) {
    unsubscribe(); fileStore = null; fileStoreHandle = null
    snapshot = { path: '/data', entries: [], loading: false, error: 'Virtual disk is unavailable' }
  }

  $: if (storage !== storageHandle) {
    unsubscribeStorage()
    storageHandle = storage
    storageSnapshot = storage?.snapshot?.() ?? null
    unsubscribeStorage = typeof storage?.subscribe === 'function'
      ? storage.subscribe((next) => (storageSnapshot = next))
      : () => {}
  }

  $: if (hostFolder !== hostFolderHandle) {
    unsubscribeHostFolder()
    hostFolderHandle = hostFolder
    hostSnapshot = hostFolder?.snapshot?.() ?? { state: 'unmounted', conflicts: [], progress: null, error: null, name: null }
    unsubscribeHostFolder = typeof hostFolder?.subscribe === 'function'
      ? hostFolder.subscribe((next) => (hostSnapshot = next))
      : () => {}
  }

  onDestroy(() => { unsubscribe(); unsubscribeStorage(); unsubscribeHostFolder() })

  const parent = (path) => path === '/data' ? '/data' : path.slice(0, path.lastIndexOf('/')) || '/data'
  const run = (work) => {
    try {
      const result = work()
      if (result && typeof result.catch === 'function') result.catch((error) => { feedback = error instanceof Error ? error.message : String(error) })
      return result
    } catch (error) {
      feedback = error instanceof Error ? error.message : String(error)
      return undefined
    }
  }
  const chooseFolder = () => {
    const name = globalThis.prompt?.('Folder name')?.trim()
    if (name) run(() => fileStore.mkdir(name))
  }
  const rename = (entry) => {
    const name = globalThis.prompt?.('New name', entry.name)?.trim()
    if (name && name !== entry.name) run(() => fileStore.rename(entry.path, name))
  }
  const remove = (entry) => {
    if (globalThis.confirm?.(`Delete ${entry.name}?`) !== false) run(() => fileStore.delete(entry.path))
  }
  const upload = (list) => {
    const selected = Array.from(list ?? [])
    if (!mutatingDisabled && selected.length) run(() => fileStore.upload(selected))
  }
  const selectRestore = async (list) => {
    feedback = ''
    const selected = list?.[0]
    if (!selected) return
    try { pendingRestore = { file: selected, preview: await fileStore.previewRestore(new Uint8Array(await selected.arrayBuffer())) } }
    catch (error) { feedback = error instanceof Error ? error.message : String(error) }
  }
  const restore = (policy) => run(async () => {
    await fileStore.restore(new Uint8Array(await pendingRestore.file.arrayBuffer()), policy)
    feedback = `Restored ${pendingRestore.preview.files.length} files`
    pendingRestore = null
  })
  const exportZip = () => run(() => {
    const blob = fileStore.exportZip()
    const url = URL.createObjectURL(blob)
    const anchor = document.createElement('a'); anchor.href = url; anchor.download = 'picotracker-data.zip'; anchor.click()
    setTimeout(() => URL.revokeObjectURL(url), 0)
  })
  const retryPersistence = () => run(async () => {
    feedback = ''
    if (typeof storage?.flushNow !== 'function') throw new Error('Persistent storage is unavailable')
    await storage.flushNow('manual-retry')
  })
  const hostAction = (work) => run(async () => {
    feedback = ''
    await work()
    await fileStore?.refresh?.()
  })
  const resolveHostConflict = (policy) => {
    const conflict = hostSnapshot?.conflicts?.[0]
    if (conflict) hostAction(() => hostFolder.resolveConflict(conflict.path, policy))
  }
</script>

<section class="files-panel" aria-labelledby="files-heading" ondragover={(event) => { if (!mutatingDisabled) event.preventDefault() }} ondrop={(event) => { event.preventDefault(); upload(event.dataTransfer?.files) }}>
  <div class="section-heading">
    <div><p class="eyebrow">Persistent virtual disk</p><h1 id="files-heading">Files</h1></div>
    <div class="file-actions">
      <button type="button" disabled={mutatingDisabled} onclick={chooseFolder}>New folder</button>
      <button type="button" disabled={mutatingDisabled} onclick={() => uploadInput?.click()}>Upload files</button>
      <button type="button" disabled={mutatingDisabled} onclick={exportZip}>Export ZIP</button>
      <button type="button" disabled={mutatingDisabled} onclick={() => restoreInput?.click()}>Restore ZIP</button>
    </div>
  </div>
  <section class="host-folder" aria-label="Host folder mirror" data-host-folder-state={hostSnapshot?.state ?? 'unmounted'}>
    <p><strong>Host-folder mirror</strong> — explicit Pull, Push, and Sync; this is not a direct mount.</p>
    {#if hostSnapshot?.state === 'unsupported'}
      <p role="status">{hostSnapshot.error ?? 'Host folder access is unsupported in this browser.'}</p>
    {:else if hostSnapshot?.state === 'unmounted' || (hostSnapshot?.state === 'failed' && hostSnapshot?.permission !== 'granted')}
      {#if hostSnapshot?.error}<p class="file-feedback" role="status">Host-folder mirror failed: {hostSnapshot.error}</p>{/if}
      <button type="button" disabled={mutatingDisabled || !hostFolder} onclick={() => hostAction(() => hostFolder.mountHostFolder())}>{hostSnapshot?.state === 'failed' ? 'Retry mount' : 'Mount folder'}</button>
    {:else if hostSnapshot?.state === 'prompt' || hostSnapshot?.state === 'denied'}
      <p role="status">Host folder permission is {hostSnapshot.state}. Reconnect to choose it again.</p>
      <button type="button" disabled={mutatingDisabled || !hostFolder} onclick={() => hostAction(() => hostFolder.mountHostFolder())}>Reconnect folder</button>
      <button type="button" disabled={mutatingDisabled || !hostFolder} onclick={() => hostAction(() => hostFolder.unmountHostFolder())}>Unmount folder</button>
    {:else}
      <p role="status">{hostSnapshot.name ? `Mirroring ${hostSnapshot.name}` : 'Host folder connected.'}</p>
      <div class="file-actions">
        <button type="button" disabled={mutatingDisabled} onclick={() => hostAction(() => hostFolder.syncHostFolder('pull'))}>Pull</button>
        <button type="button" disabled={mutatingDisabled} onclick={() => hostAction(() => hostFolder.syncHostFolder('push'))}>Push</button>
        <button type="button" disabled={mutatingDisabled} onclick={() => hostAction(() => hostFolder.syncHostFolder('bidirectional'))}>Sync</button>
        <button type="button" disabled={mutatingDisabled} onclick={() => hostAction(() => hostFolder.unmountHostFolder())}>Unmount folder</button>
      </div>
      {#if hostSnapshot?.progress}
        <p role="status">
          {#if hostSnapshot.progress.total === null}
            {hostSnapshot.progress.phase?.includes('host') ? 'Scanning host folder' : 'Scanning browser disk'}: {hostSnapshot.progress.entries ?? 0} entries, {hostSnapshot.progress.bytes ?? 0} bytes…
          {:else}
            Syncing {hostSnapshot.progress.completed}/{hostSnapshot.progress.total}…
          {/if}
        </p>
      {/if}
      {#if hostSnapshot?.lastSuccessfulSync}
        <p role="status">Last successful sync: {hostSnapshot.lastSuccessfulSync}</p>
      {/if}
      {#if hostSnapshot?.error}
        <p class="file-feedback" role="status">Host-folder mirror failed: {hostSnapshot.error}</p>
      {/if}
    {/if}
  </section>
  <input bind:this={uploadInput} class="sr-only" type="file" multiple disabled={mutatingDisabled} onchange={(event) => { upload(event.currentTarget.files); event.currentTarget.value = '' }} />
  <input bind:this={restoreInput} class="sr-only" type="file" accept=".zip,application/zip" disabled={mutatingDisabled} onchange={(event) => { selectRestore(event.currentTarget.files); event.currentTarget.value = '' }} />

  <nav class="breadcrumbs" aria-label="Files breadcrumb">
    <button type="button" disabled={!fileStore || snapshot.path === '/data'} onclick={() => run(() => fileStore.navigate(parent(snapshot.path)))}>/data</button>
    {#each snapshot.path.slice('/data'.length).split('/').filter(Boolean) as part, index}
      <span>/</span><button type="button" onclick={() => run(() => fileStore.navigate(`/data/${snapshot.path.slice('/data'.length).split('/').filter(Boolean).slice(0, index + 1).join('/')}`))}>{part}</button>
    {/each}
  </nav>

  {#if pendingRestore}
    <div class="restore-preview" role="status">
      <strong>Restore preview</strong>: {pendingRestore.preview.files.length} files, {pendingRestore.preview.conflicts.length} conflicts.
      <button type="button" disabled={mutatingDisabled} onclick={() => restore('overwrite')}>Overwrite conflicts</button>
      <button type="button" disabled={mutatingDisabled} onclick={() => restore('keep-both')}>Keep both</button>
      <button type="button" onclick={() => (pendingRestore = null)}>Cancel</button>
    </div>
  {/if}
  <ConflictDialog conflict={hostSnapshot?.conflicts?.[0] ?? null} resolve={resolveHostConflict} />
  {#if feedback || snapshot.error}
    <p class="file-feedback" role="status">{feedback || snapshot.error}</p>
  {/if}
  <div class="file-sync" role={storageSnapshot?.state === 'failed' ? 'alert' : 'status'} data-storage-state={storageSnapshot?.state ?? 'unknown'} data-storage-dirty={storageSnapshot?.dirty ? 'true' : 'false'}>
    <span>{storageSnapshot?.state === 'failed' ? `Persistence failed: ${storageSnapshot.error}` : storageSnapshot?.mutating ? 'Applying file changes…' : storageSnapshot?.syncing || storageSnapshot?.dirty ? 'Saving changes…' : 'Changes persist in this browser.'}</span>
    {#if storageSnapshot?.state === 'failed'}
      <button type="button" disabled={!storage || storageSnapshot?.syncing || storageSnapshot?.mutating} onclick={retryPersistence}>Retry save</button>
    {/if}
  </div>
  <table class="file-list">
    <thead><tr><th>Name</th><th>Kind</th><th>Size</th><th>Actions</th></tr></thead>
    <tbody>
      {#if snapshot.loading}<tr><td colspan="4">Loading files…</td></tr>
      {:else if snapshot.entries.length === 0}<tr><td colspan="4">This folder is empty. Drop files here to upload.</td></tr>
      {:else}
        {#each snapshot.entries as entry (entry.path)}
          <tr>
            <td>{#if entry.kind === 'directory'}<button type="button" class="file-name" onclick={() => run(() => fileStore.navigate(entry.path))}>{entry.name}</button>{:else}{entry.name}{/if}</td>
            <td>{entry.kind}</td><td>{entry.kind === 'file' ? entry.size : '—'}</td>
            <td><button type="button" disabled={mutatingDisabled} onclick={() => rename(entry)}>Rename</button>{#if entry.kind === 'file'}<button type="button" disabled={mutatingDisabled} onclick={() => run(() => fileStore.download(entry.path))}>Download</button>{/if}<button type="button" disabled={mutatingDisabled} onclick={() => remove(entry)}>Delete</button></td>
          </tr>
        {/each}
      {/if}
    </tbody>
  </table>
</section>
