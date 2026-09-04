<script>
  import { onDestroy, onMount } from 'svelte'
  import { GameConsole, Maximize, Minimize, TouchInteraction } from 'carbon-icons-svelte'
  import { version as packageVersion } from '../../package.json'
  import { controllerStore } from '../stores/controller.js'
  import { AUDIO_BUFFER_OPTIONS, DISPLAY_SCALE_OPTIONS } from '../stores/settings.js'
  import { TRACE_CATEGORIES } from '../trace/registry.js'
  import ToggleSwitch from './ToggleSwitch.svelte'

  export let settings
  export let trace = null
  export let audio = null
  export let runtimeState = 'idle'
  export let buildMetadata = null
  export let developerMode = false
  export let developerModeLocked = false
  export let onDeveloperModeChange = () => {}
  export let onRestart = () => {}

  let snapshot = settings.snapshot()
  let audioHandle = null
  let audioSnapshot = { metrics: null }
  let detachAudio = () => {}
  let feedback = ''
  let controller = controllerStore.snapshot()
  let fullscreenActive = false
  let fullscreenSupported = false
  const productVersion = packageVersion.replace(/\.0$/, '')
  const metadataValue = (candidate) => candidate ?? 'unavailable'
  const unsubscribe = settings.subscribe((next) => { snapshot = next })
  const detachController = controllerStore.subscribe((next) => { controller = next })
  const categories = Object.entries(TRACE_CATEGORIES).map(([bit, name]) => ({ bit: Number(bit), name }))
  const update = (patch) => { settings.update(patch); feedback = 'Settings saved locally.' }
  const updateAudio = (patch) => {
    const next = settings.update(patch)
    audio?.configure?.(next)
    feedback = 'Audio setting applied and saved.'
  }
  const toggleTrace = (bit, checked) => {
    const traceMask = checked ? snapshot.traceMask | bit : snapshot.traceMask & ~bit
    update({ traceMask })
    if (trace?.snapshot?.().state !== 'capturing') trace?.setMask?.(traceMask)
  }
  $: if (audio !== audioHandle) {
    detachAudio()
    audioHandle = audio
    audioSnapshot = audio?.snapshot?.() ?? { metrics: null }
    detachAudio = audio?.subscribe?.((next) => (audioSnapshot = next)) ?? (() => {})
  }
  const micros = (value) => `${Number(value ?? 0).toLocaleString()} µs`
  const fullscreenTarget = () => document.querySelector('.dashboard') ?? document.documentElement
  async function toggleFullscreen() {
    try {
      if (document.fullscreenElement) await document.exitFullscreen()
      else await fullscreenTarget().requestFullscreen()
    } catch {
      feedback = 'Fullscreen could not be opened in this browser.'
    }
  }
  $: controllerName = controller.names[0] ?? 'Game controller'
  $: controllerStatus = controller.connected
    ? `Connected · ${controllerName}${controller.count > 1 ? ` +${controller.count - 1}` : ''}`
    : (controller.supported ? 'Not connected' : 'Unavailable')
  onMount(() => {
    const synchronizeFullscreen = () => {
      fullscreenActive = Boolean(document.fullscreenElement)
      fullscreenSupported = document.fullscreenEnabled !== false
        && typeof fullscreenTarget()?.requestFullscreen === 'function'
        && typeof document.exitFullscreen === 'function'
    }
    document.addEventListener('fullscreenchange', synchronizeFullscreen)
    synchronizeFullscreen()
    return () => document.removeEventListener('fullscreenchange', synchronizeFullscreen)
  })
  onDestroy(() => { unsubscribe(); detachAudio(); detachController() })
</script>

<section class="settings-panel" aria-labelledby="settings-heading">
  <div class="section-heading">
    <div><p class="eyebrow">Your preferences</p><h1 id="settings-heading">Settings</h1></div>
    {#if developerMode}<span class="phase-badge">Developer tools on</span>{/if}
  </div>
  <div class="settings-grid">
    <fieldset><legend>Display</legend>
      <label>Device scale<select value={snapshot.displayScale} onchange={(event) => update({ displayScale: event.currentTarget.value })}>
        {#each DISPLAY_SCALE_OPTIONS as scale}<option value={scale}>{scale === 'fit' ? 'Fit workspace' : `${scale}×`}</option>{/each}
      </select></label>
    </fieldset>
    <fieldset><legend>Player</legend>
      <div class="player-list">
        <div class="player-row controller-row">
          <GameConsole size={20}/>
          <span class="row-copy"><strong>Controller</strong><small>{controller.connected ? (controller.usable ? 'Ready for tracker input' : 'Standard mapping unavailable') : 'Connect over USB or Bluetooth, then press a button'}</small></span>
          <span class:connected={controller.connected} class:limited={controller.connected && !controller.usable}
            class="controller-state" title={controllerStatus} aria-live="polite"><i></i><span>{controllerStatus}</span></span>
        </div>
        <div class="player-row">
          <TouchInteraction size={20}/>
          <span class="row-copy"><strong>Virtual controls</strong><small>Show controls next to the tracker</small></span>
          <ToggleSwitch checked={snapshot.showVirtualControls} label="Show virtual controls"
            onChange={(showVirtualControls) => update({ showVirtualControls })}/>
        </div>
        <button type="button" class="player-row fullscreen-row" disabled={!fullscreenSupported}
          aria-label={fullscreenActive ? 'Exit fullscreen' : 'Enter fullscreen'} onclick={toggleFullscreen}>
          {#if fullscreenActive}<Minimize size={20}/>{:else}<Maximize size={20}/>{/if}
          <span class="row-copy"><strong>{fullscreenActive ? 'Exit fullscreen' : (fullscreenSupported ? 'Enter fullscreen' : 'Fullscreen unavailable')}</strong><small>Use the entire display for NullPerator</small></span>
        </button>
      </div>
    </fieldset>
    <fieldset><legend>Audio</legend>
      <label>Output volume <output>{snapshot.outputVolume}%</output><input aria-label="Output volume" type="range" min="0" max="100" value={snapshot.outputVolume} oninput={(event) => updateAudio({ outputVolume: Number(event.currentTarget.value) })} /></label>
      <p class="field-note">Volume changes apply immediately and stay on this device.</p>
    </fieldset>
    <fieldset class="developer-switch-field"><legend>Advanced</legend>
      <div class="developer-setting">
        <span><strong>Developer tools</strong><small>{developerModeLocked ? 'Enabled by this diagnostic URL.' : 'Add runtime logs, performance trace and detailed engine settings.'}</small></span>
        <ToggleSwitch checked={developerMode} disabled={developerModeLocked} label="Developer tools"
          developerToggle onChange={onDeveloperModeChange} />
      </div>
    </fieldset>
    <fieldset><legend>Troubleshooting</legend>
      <p class="field-note">Restart the audio engine and interface without deleting your projects or files.</p>
      <button type="button" disabled={runtimeState === 'booting' || runtimeState === 'stopping'} onclick={onRestart}>Restart NullPerator</button>
    </fieldset>
    {#if developerMode}
      <fieldset><legend>Audio engine</legend>
        <label>Target buffer<select value={snapshot.audioBufferFrames} onchange={(event) => updateAudio({ audioBufferFrames: Number(event.currentTarget.value) })}>
          {#each AUDIO_BUFFER_OPTIONS as frames}<option value={frames}>{frames} frames</option>{/each}
        </select></label>
        <label class="check"><input type="checkbox" checked={snapshot.lowLatencyAudio} onchange={(event) => update({ lowLatencyAudio: event.currentTarget.checked })} />Enable AudioWorklet on next reload</label>
        {#if audioSnapshot.metrics}<dl class="audio-metrics" aria-label="Audio processing metrics" data-processing-deadline-misses={audioSnapshot.metrics.callbackDeadlineMisses}>
          <div><dt>Producer render</dt><dd>{micros(audioSnapshot.metrics.renderMicros)}</dd></div>
          <div><dt>Callback current / max</dt><dd>{micros(audioSnapshot.metrics.callbackMicros)} / {micros(audioSnapshot.metrics.callbackMaxMicros)}</dd></div>
          <div><dt>Processing deadline</dt><dd>{micros(audioSnapshot.metrics.callbackDeadlineMicros)}</dd></div>
          <div><dt>Processing deadline misses</dt><dd>{audioSnapshot.metrics.callbackDeadlineMisses.toLocaleString()}</dd></div>
        </dl>{/if}
      </fieldset>
      <fieldset><legend>Default trace categories</legend>
        <div class="checks">{#each categories as category}<label class="check"><input type="checkbox" checked={(snapshot.traceMask & category.bit) !== 0} onchange={(event) => toggleTrace(category.bit, event.currentTarget.checked)} />{category.name}</label>{/each}</div>
      </fieldset>
    {/if}
    <fieldset class="product-details"><legend>NullPerator</legend>
      <div class="product-summary">
        <span><small>Version</small><strong>{productVersion}</strong></span>
        <nav class="product-links" aria-label="NullPerator links">
          <a href="https://github.com/203-Systems/NullPerator" target="_blank" rel="noreferrer">GitHub repository</a>
          <a href="/THIRD_PARTY_NOTICES.md" target="_blank" rel="noreferrer">Third-party notices</a>
        </nav>
      </div>
      {#if developerMode}<dl class="build-details" aria-label="Developer build details">
        <div><dt>Commit</dt><dd>{metadataValue(buildMetadata?.commit)}{buildMetadata?.dirty ? ' (dirty)' : ''}</dd></div>
        <div><dt>Built</dt><dd>{metadataValue(buildMetadata?.builtAt)}</dd></div>
        <div><dt>Runtime</dt><dd>WebAssembly · pthreads · AudioWorklet</dd></div>
        <div><dt>Toolchain</dt><dd>{metadataValue(buildMetadata?.emscripten)}</dd></div>
      </dl>{/if}
    </fieldset>
  </div>
  <div class="settings-actions"><button type="button" onclick={() => { const next = settings.reset(); audio?.configure?.(next); feedback = 'Defaults restored and live audio settings applied.' }}>Restore defaults</button><p role="status">{feedback}</p></div>
</section>

<style>
  .settings-panel { width: min(100%, 1000px); margin: 0 auto; } .settings-grid { display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: 12px; }
  fieldset { min-width: 0; padding: 16px; border: 1px solid var(--border); border-radius: var(--radius-card); background: var(--surface); } legend { padding: 0 5px; font-size: .8rem; font-weight: 700; }
  label { display: grid; gap: 6px; margin-bottom: 12px; color: var(--muted); font-size: .73rem; } select,input:not([type='checkbox']):not([type='range']),button { min-width: 0; min-height:var(--control-height); padding: 0 9px; border: 1px solid var(--border); border-radius: var(--radius-control); color: var(--text); background: var(--bg-2); }
  button:not(:disabled) { cursor: pointer; }
  output { color:var(--text-accent); font:500 .73rem/1 var(--mono); }
  .field-note { margin:0 0 12px; color:var(--muted); font-size:.73rem; line-height:1.5; }
  .player-list { margin:-5px 0; }
  .player-row { display:grid; min-width:0; min-height:52px; grid-template-columns:20px minmax(0,1fr) auto; align-items:center; gap:10px; color:var(--muted); }
  .player-row + .player-row { border-top:1px solid var(--border); }
  .player-row :global(svg) { color:var(--muted-solid); }
  .row-copy { display:grid; min-width:0; gap:2px; }
  .row-copy strong { color:var(--text); font-size:.8rem; font-weight:600; }
  .row-copy small { overflow:hidden; color:var(--muted); font-size:.67rem; line-height:1.35; text-overflow:ellipsis; white-space:nowrap; }
  .controller-state { display:flex; min-width:0; max-width:190px; align-items:center; gap:6px; color:var(--muted); font:500 .67rem/1 var(--mono); }
  .controller-state i { width:7px; height:7px; flex:0 0 auto; border-radius:50%; background:var(--disabled); }
  .controller-state>span { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
  .controller-state.connected { color:var(--success-text); }
  .controller-state.connected i { background:var(--success-dot); }
  .controller-state.limited { color:var(--warning); }
  .controller-state.limited i { background:var(--warning); }
  button.player-row { width:100%; min-height:52px; padding:0; border:0; border-radius:0; text-align:left; background:transparent; }
  button.player-row:not(:disabled):hover { color:var(--text); background:var(--surface-subtle); }
  button.player-row:disabled { opacity:.58; }
  .fullscreen-row .row-copy { grid-column:2/4; }
  .developer-setting { display:flex; min-height:48px; align-items:center; justify-content:space-between; gap:16px; }
  .developer-setting>span { display:grid; gap:5px; }
  .developer-setting strong { color:var(--text); font-size:.8rem; }
  .developer-setting small { color:var(--muted); font-size:.7rem; line-height:1.4; }
  .check { display: flex; min-height:44px; align-items: center; gap: 7px; text-transform: capitalize; } .check input { min-width: auto; min-height:auto; } .checks { display: grid; grid-template-columns: repeat(2,1fr); }
  .audio-metrics { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:7px; margin:2px 0 12px; } .audio-metrics div { min-width:0; padding:8px; border:1px solid var(--border); border-radius:var(--radius-control); background:var(--surface-subtle); } .audio-metrics dt { color:var(--muted); font-size:.67rem; } .audio-metrics dd { margin:4px 0 0; font:.73rem var(--mono); overflow-wrap:anywhere; }
  .product-details { grid-column:1/-1; }
  .product-summary { display:flex; align-items:center; justify-content:space-between; gap:16px; }
  .product-summary>span { display:grid; gap:3px; }
  .product-summary small { color:var(--muted); font-size:.67rem; text-transform:uppercase; letter-spacing:.08em; }
  .product-summary strong { font:600 .87rem/1 var(--mono); }
  .product-links { display:flex; flex-wrap:wrap; justify-content:flex-end; gap:8px; }
  .product-links a { display:inline-flex; min-height:var(--control-height); align-items:center; padding:0 9px; border:1px solid var(--border); border-radius:var(--radius-control); color:var(--accent); background:var(--bg-2); font-size:.73rem; text-decoration:none; transition:color 120ms,border-color 120ms,background 120ms; }
  .product-links a:hover,.product-links a:focus-visible { color:var(--text-strong); border-color:var(--accent-border); background:var(--accent-soft); }
  .build-details { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:0 16px; margin:14px 0 0; padding-top:8px; border-top:1px solid var(--border); }
  .build-details div { display:grid; min-width:0; grid-template-columns:72px minmax(0,1fr); gap:8px; padding:6px 0; }
  .build-details dt { color:var(--muted); font-size:.67rem; }
  .build-details dd { margin:0; overflow-wrap:anywhere; font:.67rem/1.4 var(--mono); }
  .settings-actions { display: flex; align-items: center; gap: 12px; margin-top: 14px; } .settings-actions p { margin: 0; color: var(--muted); font-size: .73rem; }
  @media (max-width: 760px) { .settings-grid { grid-template-columns: 1fr; } .product-details { grid-column:auto; } }
  @media (max-width: 520px) { .product-summary { align-items:flex-start; flex-direction:column; } .product-links { width:100%; justify-content:flex-start; } .build-details { grid-template-columns:1fr; } }
</style>
