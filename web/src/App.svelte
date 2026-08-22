<script>
  import { onMount } from 'svelte'

  import { restartRuntime, runtimeStore } from './stores/runtime.js'

  const sections = ['Device', 'Files', 'MIDI', 'Logs', 'Trace', 'Settings', 'About']
  let activeSection = 'Device'
  let runtime = runtimeStore.getSnapshot()

  onMount(() => {
    const unsubscribe = runtimeStore.subscribe((snapshot) => (runtime = snapshot))
    runtimeStore.start().catch(() => {})
    return () => {
      unsubscribe()
      runtimeStore.stop().catch(() => {})
    }
  })
</script>

<svelte:head>
  <meta
    name="description"
    content="PicoTracker WebAssembly development and performance workbench"
  />
</svelte:head>

<div class="workbench">
  <header class="top-bar">
    <div class="brand">
      <span class="brand-mark" aria-hidden="true">PT</span>
      <div>
        <strong>PicoTracker</strong>
        <span>WASM Workbench</span>
      </div>
    </div>
    <div class="runtime-state" data-runtime-state={runtime.state} title={runtime.error ?? undefined}>
      <span class="status-dot"></span>
      <span>Runtime {runtime.state}</span>
      <button type="button" onclick={() => restartRuntime().catch(() => {})}>Restart</button>
    </div>
  </header>

  <div class="workbench-body">
    <nav class="left-nav" aria-label="Workbench sections">
      {#each sections as section}
        <button
          type="button"
          class:active={activeSection === section}
          aria-current={activeSection === section ? 'page' : undefined}
          onclick={() => (activeSection = section)}
        >
          <span class="nav-glyph" aria-hidden="true">{section.slice(0, 1)}</span>
          <span>{section}</span>
        </button>
      {/each}
    </nav>

    <main class="workspace">
      {#if activeSection === 'Device'}
        <section class="device-stage" aria-labelledby="device-heading">
          <div class="section-heading">
            <div>
              <p class="eyebrow">Simulator</p>
              <h1 id="device-heading">PicoTracker Device</h1>
            </div>
            <span class="phase-badge">Runtime lifecycle</span>
          </div>

          <div class="device-placeholder">
            <div class="display-placeholder" role="img" aria-label="PicoTracker display placeholder">
              <span>240 × 240</span>
              <small>{runtime.error ?? 'WASM canvas arrives in Task 4'}</small>
            </div>
          </div>
        </section>
      {:else}
        <section class="tool-placeholder" aria-live="polite">
          <p class="eyebrow">Workbench</p>
          <h1>{activeSection}</h1>
          <p>This panel will connect as its WASM subsystem is implemented.</p>
        </section>
      {/if}
    </main>

    <aside class="status-rail" aria-label="Subsystem status">
      <h2>Status</h2>
      {#each ['WASM', 'Audio', 'Storage', 'MIDI'] as subsystem}
        <div class="status-card">
          <span>{subsystem}</span>
          <strong>{subsystem === 'WASM' ? runtime.state : 'Pending'}</strong>
        </div>
      {/each}
    </aside>
  </div>
</div>
