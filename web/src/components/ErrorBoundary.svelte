<script>
  let { children, label = 'NullPerator panel' } = $props()
  let error = $state(null)
  const reset = () => { error = null }
</script>

<svelte:boundary onerror={(caught) => { error = caught }}>
  {#if error}
    <section class="recovery-card" role="alert" data-boundary-state="failed">
      <p class="eyebrow">Panel recovery</p>
      <h1>{label} failed</h1>
      <p>{error instanceof Error ? error.message : String(error)}</p>
      <button type="button" onclick={reset}>Retry panel</button>
    </section>
  {:else}
    {@render children()}
  {/if}
</svelte:boundary>
