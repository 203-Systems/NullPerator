<script>
  export let conflict = null
  export let resolve = async () => {}

  const metadata = (entry) => entry
    ? `${entry.kind}, ${entry.size} bytes${entry.hash ? `, ${entry.hash}` : ''}`
    : 'deleted'
</script>

{#if conflict}
  <dialog class="host-conflict" open aria-labelledby="host-conflict-heading">
    <h2 id="host-conflict-heading">Host-folder conflict</h2>
    <p><code>{conflict.path}</code> changed in both the browser disk and host folder.</p>
    <dl>
      <dt>Base</dt><dd>{metadata(conflict.base)}</dd>
      <dt>Browser disk</dt><dd>{metadata(conflict.browser)}</dd>
      <dt>Host folder</dt><dd>{metadata(conflict.host)}</dd>
    </dl>
    <div class="file-actions">
      <button type="button" onclick={() => resolve('keep-browser')}>Keep browser version</button>
      <button type="button" onclick={() => resolve('keep-host')}>Keep host version</button>
      <button type="button" onclick={() => resolve('keep-both')}>Keep both</button>
    </div>
  </dialog>
{/if}
