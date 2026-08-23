<script>
  import { onDestroy } from 'svelte'

  import { createFilesStore } from '../stores/files.js'

  export let files = null
  export let storage = null
  export let disabled = false

  let fileStore = null
  let fileStoreHandle = null
  let unsubscribe = () => {}
  let storageHandle = null
  let unsubscribeStorage = () => {}
  let storageSnapshot = null
  let snapshot = { path: '/data', entries: [], loading: false, error: null }
  let uploadInput
  let restoreInput
  let pendingRestore = null
  let feedback = ''
  $: mutatingDisabled = disabled || !fileStore || snapshot.loading || storageSnapshot?.state === 'syncing'

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

  onDestroy(() => { unsubscribe(); unsubscribeStorage() })

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
  const upload = (list) => { if (!mutatingDisabled && list?.length) run(() => fileStore.upload(list)) }
  const selectRestore = async (list) => {
    feedback = ''
    if (!list?.[0]) return
    try { pendingRestore = { file: list[0], preview: await fileStore.previewRestore(new Uint8Array(await list[0].arrayBuffer())) } }
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
</script>

<section class="files-panel" aria-labelledby="files-heading" ondragover={(event) => { if (!mutatingDisabled) event.preventDefault() }} ondrop={(event) => { event.preventDefault(); upload(event.dataTransfer?.files) }}>
  <div class="section-heading">
    <div><p class="eyebrow">Persistent virtual disk</p><h1 id="files-heading">Files</h1></div>
    <div class="file-actions">
      <button type="button" disabled={mutatingDisabled} onclick={chooseFolder}>New folder</button>
      <button type="button" disabled={mutatingDisabled} onclick={() => uploadInput?.click()}>Upload files</button>
      <button type="button" disabled={!fileStore || snapshot.loading} onclick={exportZip}>Export ZIP</button>
      <button type="button" disabled={mutatingDisabled} onclick={() => restoreInput?.click()}>Restore ZIP</button>
    </div>
  </div>
  <input bind:this={uploadInput} class="sr-only" type="file" multiple disabled={mutatingDisabled} onchange={(event) => upload(event.currentTarget.files)} />
  <input bind:this={restoreInput} class="sr-only" type="file" accept=".zip,application/zip" disabled={mutatingDisabled} onchange={(event) => selectRestore(event.currentTarget.files)} />

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
  {#if feedback || snapshot.error}
    <p class="file-feedback" role="status">{feedback || snapshot.error}</p>
  {/if}
  <p class="file-sync" data-storage-state={storageSnapshot?.state ?? 'unknown'}>{storageSnapshot?.state === 'syncing' ? 'Saving changes…' : storageSnapshot?.state === 'failed' ? `Persistence failed: ${storageSnapshot.error}` : 'Changes persist in this browser.'}</p>
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
            <td><button type="button" disabled={mutatingDisabled} onclick={() => rename(entry)}>Rename</button>{#if entry.kind === 'file'}<button type="button" onclick={() => run(() => fileStore.download(entry.path))}>Download</button>{/if}<button type="button" disabled={mutatingDisabled} onclick={() => remove(entry)}>Delete</button></td>
          </tr>
        {/each}
      {/if}
    </tbody>
  </table>
</section>
