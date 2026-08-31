<script>
  import { tick } from 'svelte'

  export let conflict = null
  export let resolve = async () => {}
  export let returnFocus = null

  let dialog
  let firstAction
  let activePath = null
  let restoreTarget = null
  let resolving = false

  const metadata = (entry) => entry
    ? `${entry.kind}, ${entry.size} bytes${entry.hash ? `, ${entry.hash}` : ''}`
    : 'deleted'

  async function synchronize(next, target) {
    if (next && target) {
      const pathChanged = activePath !== next.path
      if (activePath === null) restoreTarget = returnFocus ?? document.activeElement
      activePath = next.path
      await tick()
      if (conflict !== next || dialog !== target) return
      if (!target.open) target.showModal()
      if (pathChanged) firstAction?.focus({ preventScroll: true })
      return
    }
    if (next || activePath === null) return
    activePath = null
    const targetToRestore = restoreTarget
    restoreTarget = null
    await tick()
    targetToRestore?.focus?.({ preventScroll: true })
  }

  async function choose(policy) {
    if (resolving) return
    resolving = true
    try { await resolve(policy) }
    finally {
      resolving = false
      await tick()
      if (conflict && dialog?.open) firstAction?.focus({ preventScroll: true })
    }
  }

  $: synchronize(conflict, dialog)
</script>

{#if conflict}
  <dialog bind:this={dialog} class="host-conflict" aria-labelledby="host-conflict-heading" aria-busy={resolving}
    oncancel={(event) => event.preventDefault()}>
    <h2 id="host-conflict-heading">Host-folder conflict</h2>
    <p><code>{conflict.path}</code> changed in both the browser disk and host folder.</p>
    <dl>
      <dt>Base</dt><dd>{metadata(conflict.base)}</dd>
      <dt>Browser disk</dt><dd>{metadata(conflict.browser)}</dd>
      <dt>Host folder</dt><dd>{metadata(conflict.host)}</dd>
    </dl>
    <div class="file-actions">
      <button bind:this={firstAction} type="button" disabled={resolving} onclick={() => choose('keep-browser')}>Keep browser version</button>
      <button type="button" disabled={resolving} onclick={() => choose('keep-host')}>Keep host version</button>
      <button type="button" disabled={resolving} onclick={() => choose('keep-both')}>Keep both</button>
    </div>
  </dialog>
{/if}
