<script>
  import { onMount, tick } from 'svelte'

  import DevicePanel from './components/DevicePanel.svelte'
  import FilesPanel from './components/FilesPanel.svelte'
  import { runtimeStore } from './stores/runtime.js'

  const sections = ['Device', 'Files', 'MIDI', 'Logs', 'Trace', 'Settings', 'About']
  let activeSection = 'Device'
  let runtime = runtimeStore.getSnapshot()
  let audio = runtime.audio?.snapshot?.() ?? { state: 'unavailable', metrics: null, capability: null }
  let canvasGeneration = 0
  let detachAudio = () => {}

  async function restart() {
    await runtimeStore.stop()
    canvasGeneration += 1
    await tick()
    await runtimeStore.start()
  }

  async function stopRuntime() {
    await runtimeStore.stop()
  }

  function reloadAudioMode(enabled) {
    const url = new URL(globalThis.location.href)
    if (enabled) url.searchParams.set('audio', 'worklet')
    else url.searchParams.delete('audio')
    globalThis.location.assign(url)
  }

  onMount(() => {
    const unsubscribe = runtimeStore.subscribe((snapshot) => {
      runtime = snapshot
      detachAudio()
      if (snapshot.audio) detachAudio = snapshot.audio.subscribe((next) => (audio = next))
      else audio = { state: 'unavailable', metrics: null, capability: null }
    })
    runtimeStore.start().catch(() => {})
    return () => {
      unsubscribe()
      detachAudio()
      runtimeStore.stop().catch(() => {})
    }
  })
</script>

<svelte:head>
  <link rel="icon" href="data:," />
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
      <button type="button" onclick={() => restart().catch(() => {})}>Restart</button>
      <button type="button" onclick={() => stopRuntime().catch(() => {})}>Stop runtime</button>
    </div>
    <div class="runtime-state" data-audio-state={audio.state} aria-label={`Audio ${audio.state}`} title={audio.error ?? undefined}>
      <span class="status-dot"></span>
      <span>Audio {audio.state}</span>
      {#if audio.capability?.mode === 'disabled'}
        <button type="button" onclick={() => reloadAudioMode(true)}>Enable low-latency audio (reload)</button>
      {:else if audio.state === 'failed'}
        <button type="button" onclick={() => reloadAudioMode(false)}>Reload without audio</button>
      {:else if audio.state === 'locked' || audio.state === 'suspended'}
        <button type="button" onclick={() => runtime.audio?.unlockAudio().catch(() => {})}>Unlock audio</button>
      {/if}
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
      <section class="device-stage" aria-labelledby="device-heading" hidden={activeSection !== 'Device'}>
          <div class="section-heading">
            <div>
              <p class="eyebrow">Simulator</p>
              <h1 id="device-heading">PicoTracker Device</h1>
            </div>
            <span class="phase-badge">Runtime lifecycle</span>
          </div>

          {#key canvasGeneration}
            <DevicePanel {runtime} />
          {/key}
      </section>
      {#if activeSection === 'Files'}
        <FilesPanel files={runtime.files} storage={runtime.storage} hostFolder={runtime.hostFolder} disabled={runtime.state !== 'ready'} />
      {:else if activeSection !== 'Device'}
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
          <strong>{subsystem === 'WASM' ? runtime.state : subsystem === 'Audio' ? audio.state : 'Pending'}</strong>
        </div>
      {/each}
    </aside>
    <div class="audio-diagnostics" hidden aria-hidden="true" data-audio-capability={audio.capability ? (audio.capability.available ? 'available' : 'unavailable') : 'unknown'} data-audio-capability-reason={audio.capability?.reason ?? ''} data-audio-worklet-callbacks={audio.metrics?.callbackCount ?? 0} data-audio-underruns={audio.metrics?.underrunFrames ?? 0} data-audio-setup-phase={audio.metrics?.setupPhase ?? 0} data-audio-unlock-main-thread={audio.metrics?.unlockOnBrowserMainThread ?? 0}></div>
  </div>
</div>
