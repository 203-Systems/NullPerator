<script>
  import { onDestroy } from 'svelte'

  export let midi = null
  export let disabled = false
  export let developerMode = false

  let handle = null
  let unsubscribe = () => {}
  let feedback = ''
  let snapshot = {
    state: 'unsupported', error: null, inputs: [], outputs: [],
    selectedInputId: null, selectedOutputId: null,
    inputConnected: false, outputConnected: false,
    droppedInputBytes: 0, droppedNormal: 0, droppedRealtime: 0,
  }

  $: if (midi !== handle) {
    unsubscribe()
    handle = midi
    snapshot = midi?.snapshot?.() ?? { ...snapshot, state: 'unsupported', error: 'MIDI runtime is unavailable.' }
    unsubscribe = midi?.subscribe?.((next) => (snapshot = next)) ?? (() => {})
  }

  const run = async (work) => {
    feedback = ''
    try { await work() }
    catch (error) { feedback = error instanceof Error ? error.message : String(error) }
  }
  const idOrNull = (value) => value || null

  $: phaseLabel = snapshot.state === 'ready'
    ? snapshot.inputConnected || snapshot.outputConnected ? 'Connected' : 'Ready'
    : ({
        unsupported: 'Unavailable',
        requesting: 'Connecting',
        denied: 'Permission needed',
        failed: 'Needs attention',
        idle: 'Not connected',
      })[snapshot.state] ?? 'Not connected'
  $: connectLabel = snapshot.state === 'requesting'
    ? 'Connecting…'
    : snapshot.state === 'denied' || snapshot.state === 'failed'
      ? 'Try MIDI again'
      : 'Connect MIDI'
  $: friendlyError = snapshot.state === 'denied'
    ? 'MIDI access was not granted. Check your browser permission and try again.'
    : snapshot.state === 'failed'
      ? 'MIDI could not connect. Check your devices and try again.'
      : feedback || null

  onDestroy(() => unsubscribe())
</script>

<section class="midi-panel" aria-labelledby="midi-heading" data-midi-state={snapshot.state}>
  <div class="section-heading">
    <div><p class="eyebrow">Input and output</p><h1 id="midi-heading">MIDI</h1></div>
    <span class="phase-badge" title={developerMode ? `Internal state: ${snapshot.state}` : undefined}>{phaseLabel}</span>
  </div>

  <div class="midi-card">
    {#if snapshot.state === 'unsupported'}
      <p role="status">MIDI is unavailable in this browser.</p>
      {#if developerMode && snapshot.error}<p class="diagnostic-detail">{snapshot.error}</p>{/if}
    {:else if snapshot.state !== 'ready'}
      <p>Connect MIDI devices to use them with NullPerator. Your browser will ask for access after you continue.</p>
      <button type="button" disabled={disabled || snapshot.state === 'requesting' || !midi} onclick={() => run(() => midi.requestMidiAccess())}>
        {connectLabel}
      </button>
      {#if developerMode}<p class="diagnostic-detail">System exclusive messages are disabled. Internal state: {snapshot.state}.</p>{/if}
    {:else}
      <div class="midi-grid">
        <label>
          <span>Input → NullPerator</span>
          <select disabled={disabled} value={snapshot.selectedInputId ?? ''} onchange={(event) => run(() => midi.selectMidiInput(idOrNull(event.currentTarget.value)))}>
            <option value="">Off</option>
            {#each snapshot.inputs as port (port.id)}
              <option value={port.id}>{port.manufacturer ? `${port.manufacturer} — ` : ''}{port.name}{port.state === 'connected' ? '' : ' (disconnected)'}</option>
            {/each}
          </select>
          <small>{snapshot.selectedInputId ? snapshot.inputConnected ? 'Connected' : 'Waiting for the selected device to reconnect' : 'No input selected'}</small>
        </label>
        <label>
          <span>NullPerator → Output</span>
          <select disabled={disabled} value={snapshot.selectedOutputId ?? ''} onchange={(event) => run(() => midi.selectMidiOutput(idOrNull(event.currentTarget.value)))}>
            <option value="">Off</option>
            {#each snapshot.outputs as port (port.id)}
              <option value={port.id}>{port.manufacturer ? `${port.manufacturer} — ` : ''}{port.name}{port.state === 'connected' ? '' : ' (disconnected)'}</option>
            {/each}
          </select>
          <small>{snapshot.selectedOutputId ? snapshot.outputConnected ? 'Connected' : 'Waiting for the selected device to reconnect' : 'No output selected'}</small>
        </label>
      </div>
      {#if developerMode}<dl class="midi-metrics" aria-label="MIDI diagnostics">
        <div><dt>Dropped input bytes</dt><dd>{snapshot.droppedInputBytes}</dd></div>
        <div><dt>Dropped output packets</dt><dd>{snapshot.droppedNormal}</dd></div>
        <div><dt>Dropped realtime packets</dt><dd>{snapshot.droppedRealtime}</dd></div>
      </dl>{/if}
    {/if}
    {#if friendlyError}
      <p class="midi-error" role="status">{friendlyError}</p>
      {#if developerMode && snapshot.error && snapshot.error !== friendlyError}<p class="diagnostic-detail">{snapshot.error}</p>{/if}
    {/if}
  </div>
</section>

<style>
  .midi-panel { width: min(100%, 960px); margin: 0 auto; }
  .midi-card { padding: 16px; border: 1px solid var(--border); border-radius: var(--radius-card); background: var(--surface); }
  .midi-card > p { color: var(--muted); }
  .diagnostic-detail { color: var(--muted); font: .73rem/1.5 var(--mono); }
  button, select { min-width:44px; min-height:var(--control-height); padding: 0 9px; border: 1px solid var(--border); border-radius: var(--radius-control); color: var(--text); background: var(--bg-2); }
  button:not(:disabled), select:not(:disabled) { cursor: pointer; }
  .midi-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px; }
  label { display: grid; gap: 7px; color: var(--muted); font-size: .8rem; }
  small { min-height: 1.4em; color: var(--muted); }
  .midi-metrics { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 10px; margin: 22px 0 0; }
  .midi-metrics div { padding: 10px 12px; border: 1px solid var(--border); border-radius: var(--radius-control); background: var(--surface-subtle); }
  dt { color: var(--muted); font-size: .73rem; }
  dd { margin: 5px 0 0; font: 600 .93rem/1 var(--mono); }
  .midi-error { margin-top: 16px; color: var(--danger-text) !important; }
  @media (max-width: 680px) { .midi-grid, .midi-metrics { grid-template-columns: 1fr; } }
</style>
