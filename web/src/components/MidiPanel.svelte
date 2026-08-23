<script>
  import { onDestroy } from 'svelte'

  export let midi = null
  export let disabled = false

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

  onDestroy(() => unsubscribe())
</script>

<section class="midi-panel" aria-labelledby="midi-heading" data-midi-state={snapshot.state}>
  <div class="section-heading">
    <div><p class="eyebrow">Browser device routing</p><h1 id="midi-heading">Web MIDI</h1></div>
    <span class="phase-badge">{snapshot.state}</span>
  </div>

  <div class="midi-card">
    {#if snapshot.state === 'unsupported'}
      <p role="status">{snapshot.error ?? 'Web MIDI is unsupported in this browser.'}</p>
    {:else if snapshot.state !== 'ready'}
      <p>Web MIDI permission is requested only after this button is pressed. SysEx remains disabled.</p>
      <button type="button" disabled={disabled || snapshot.state === 'requesting' || !midi} onclick={() => run(() => midi.requestMidiAccess())}>
        {snapshot.state === 'requesting' ? 'Requesting access…' : snapshot.state === 'denied' || snapshot.state === 'failed' ? 'Retry Web MIDI access' : 'Enable Web MIDI'}
      </button>
    {:else}
      <div class="midi-grid">
        <label>
          <span>Input</span>
          <select disabled={disabled} value={snapshot.selectedInputId ?? ''} onchange={(event) => run(() => midi.selectMidiInput(idOrNull(event.currentTarget.value)))}>
            <option value="">Off</option>
            {#each snapshot.inputs as port (port.id)}
              <option value={port.id}>{port.manufacturer ? `${port.manufacturer} — ` : ''}{port.name}{port.state === 'connected' ? '' : ' (disconnected)'}</option>
            {/each}
          </select>
          <small>{snapshot.selectedInputId ? snapshot.inputConnected ? 'Connected' : 'Waiting for the selected device to reconnect' : 'No input selected'}</small>
        </label>
        <label>
          <span>Output</span>
          <select disabled={disabled} value={snapshot.selectedOutputId ?? ''} onchange={(event) => run(() => midi.selectMidiOutput(idOrNull(event.currentTarget.value)))}>
            <option value="">Off</option>
            {#each snapshot.outputs as port (port.id)}
              <option value={port.id}>{port.manufacturer ? `${port.manufacturer} — ` : ''}{port.name}{port.state === 'connected' ? '' : ' (disconnected)'}</option>
            {/each}
          </select>
          <small>{snapshot.selectedOutputId ? snapshot.outputConnected ? 'Connected' : 'Waiting for the selected device to reconnect' : 'No output selected'}</small>
        </label>
      </div>
      <dl class="midi-metrics">
        <div><dt>Dropped input bytes</dt><dd>{snapshot.droppedInputBytes}</dd></div>
        <div><dt>Dropped output packets</dt><dd>{snapshot.droppedNormal}</dd></div>
        <div><dt>Dropped realtime packets</dt><dd>{snapshot.droppedRealtime}</dd></div>
      </dl>
    {/if}
    {#if feedback || snapshot.error}
      <p class="midi-error" role="status">{feedback || snapshot.error}</p>
    {/if}
  </div>
</section>

<style>
  .midi-panel { width: min(100%, 960px); margin: 0 auto; }
  .midi-card { padding: 22px; border: 1px solid var(--border); border-radius: 12px; background: var(--surface); }
  .midi-card > p { color: var(--muted); }
  button, select { padding: 8px 10px; border: 1px solid var(--border); border-radius: 7px; color: #e7e7ea; background: var(--surface-raised); }
  button:not(:disabled), select:not(:disabled) { cursor: pointer; }
  .midi-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 18px; }
  label { display: grid; gap: 7px; color: var(--muted); font-size: 12px; }
  small { min-height: 1.4em; color: var(--muted); }
  .midi-metrics { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 10px; margin: 22px 0 0; }
  .midi-metrics div { padding: 12px; border: 1px solid var(--border); border-radius: 8px; background: rgba(255,255,255,.02); }
  dt { color: var(--muted); font-size: 11px; }
  dd { margin: 5px 0 0; font: 600 14px/1 ui-monospace, monospace; }
  .midi-error { margin-top: 16px; color: #f28a8a !important; }
  @media (max-width: 680px) { .midi-grid, .midi-metrics { grid-template-columns: 1fr; } }
</style>
