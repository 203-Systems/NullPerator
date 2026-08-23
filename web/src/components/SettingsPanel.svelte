<script>
  import { onDestroy } from 'svelte'
  import { AUDIO_BUFFER_OPTIONS, DISPLAY_SCALE_OPTIONS } from '../stores/settings.js'
  import { TRACE_CATEGORIES } from '../trace/registry.js'

  export let settings
  export let trace = null
  export let audio = null
  export let runtimeState = 'idle'
  export let onRestart = () => {}

  let snapshot = settings.snapshot()
  let audioHandle = null
  let audioSnapshot = { metrics: null }
  let detachAudio = () => {}
  let feedback = ''
  const unsubscribe = settings.subscribe((next) => { snapshot = next })
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
  onDestroy(() => { unsubscribe(); detachAudio() })
</script>

<section class="settings-panel" aria-labelledby="settings-heading">
  <div class="section-heading">
    <div><p class="eyebrow">Local workbench preferences</p><h1 id="settings-heading">Settings</h1></div>
    <span class="phase-badge">Schema v{snapshot.version}</span>
  </div>
  <div class="settings-grid">
    <fieldset><legend>Display</legend>
      <label>Device scale<select value={snapshot.displayScale} onchange={(event) => update({ displayScale: event.currentTarget.value })}>
        {#each DISPLAY_SCALE_OPTIONS as scale}<option value={scale}>{scale === 'fit' ? 'Fit workspace' : `${scale}×`}</option>{/each}
      </select></label>
    </fieldset>
    <fieldset><legend>Audio</legend>
      <label>Target buffer<select value={snapshot.audioBufferFrames} onchange={(event) => updateAudio({ audioBufferFrames: Number(event.currentTarget.value) })}>
        {#each AUDIO_BUFFER_OPTIONS as frames}<option value={frames}>{frames} frames</option>{/each}
      </select></label>
      <label>Output volume <output>{snapshot.outputVolume}%</output><input aria-label="Output volume" type="range" min="0" max="100" value={snapshot.outputVolume} oninput={(event) => updateAudio({ outputVolume: Number(event.currentTarget.value) })} /></label>
      <label class="check"><input type="checkbox" checked={snapshot.lowLatencyAudio} onchange={(event) => update({ lowLatencyAudio: event.currentTarget.checked })} />Enable AudioWorklet on next reload</label>
      {#if audioSnapshot.metrics}<dl class="audio-metrics" aria-label="Audio processing metrics" data-processing-deadline-misses={audioSnapshot.metrics.callbackDeadlineMisses}>
        <div><dt>Producer render</dt><dd>{micros(audioSnapshot.metrics.renderMicros)}</dd></div>
        <div><dt>Callback current / max</dt><dd>{micros(audioSnapshot.metrics.callbackMicros)} / {micros(audioSnapshot.metrics.callbackMaxMicros)}</dd></div>
        <div><dt>Processing deadline</dt><dd>{micros(audioSnapshot.metrics.callbackDeadlineMicros)}</dd></div>
        <div><dt>Processing deadline misses</dt><dd>{audioSnapshot.metrics.callbackDeadlineMisses.toLocaleString()}</dd></div>
      </dl>{/if}
      <button type="button" disabled={runtimeState === 'booting' || runtimeState === 'stopping'} onclick={onRestart}>Reload/restart for audio mode</button>
    </fieldset>
    <fieldset><legend>Default trace categories</legend>
      <div class="checks">{#each categories as category}<label class="check"><input type="checkbox" checked={(snapshot.traceMask & category.bit) !== 0} onchange={(event) => toggleTrace(category.bit, event.currentTarget.checked)} />{category.name}</label>{/each}</div>
    </fieldset>
  </div>
  <div class="settings-actions"><button type="button" onclick={() => { const next = settings.reset(); audio?.configure?.(next); feedback = 'Defaults restored and live audio settings applied.' }}>Restore defaults</button><p role="status">{feedback}</p></div>
</section>

<style>
  .settings-panel { width: min(100%, 1000px); margin: 0 auto; } .settings-grid { display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: 14px; }
  fieldset { min-width: 0; padding: 16px; border: 1px solid var(--border); border-radius: 10px; background: var(--surface); } legend { padding: 0 5px; font-size: 12px; font-weight: 700; }
  label { display: grid; gap: 6px; margin-bottom: 12px; color: var(--muted); font-size: 11px; } select,input,button { min-width: 0; padding: 8px; border: 1px solid var(--border); border-radius: 7px; color: #e7e7ea; background: var(--surface-raised); }
  button:not(:disabled) { cursor: pointer; }
  .check { display: flex; align-items: center; gap: 7px; text-transform: capitalize; } .check input { min-width: auto; } .checks { display: grid; grid-template-columns: repeat(2,1fr); }
  .audio-metrics { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:7px; margin:2px 0 12px; } .audio-metrics div { min-width:0; padding:8px; border:1px solid var(--border); border-radius:6px; background:rgba(255,255,255,.025); } .audio-metrics dt { color:var(--muted); font-size:10px; } .audio-metrics dd { margin:4px 0 0; font:11px ui-monospace,monospace; overflow-wrap:anywhere; }
  .settings-actions { display: flex; align-items: center; gap: 12px; margin-top: 14px; } .settings-actions p { margin: 0; color: var(--muted); font-size: 11px; }
  @media (max-width: 760px) { .settings-grid { grid-template-columns: 1fr; } }
</style>
