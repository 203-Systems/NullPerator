<script>
  export let runtime
  export let audio
  export let storage
  export let midi
  export let developerMode = false
  export let onDeveloperModeChange = () => {}
  const status = (value, fallback = 'unavailable') => value?.state ?? fallback
  $: audioDisplayState = audio?.capability?.mode === 'disabled'
    ? 'disabled'
    : status(audio)
  $: audioTitle = audioDisplayState === 'disabled'
    ? audio?.capability?.reason
    : audio?.error
</script>

<header class="top-bar">
  <div class="top-bar-left">
    <img class="top-bar-logo" src="/203dark.svg" alt="203 Systems" />
    <span class="top-bar-title">NullPerator</span>
  </div>
  <div class="top-bar-right">
    <span class="top-status runtime" data-runtime-state={runtime.state} title={runtime.error ?? undefined}>
      <span class="status-dot"></span><span>Runtime {runtime.state}</span>
    </span>
    {#if developerMode}<span class="top-status advanced" data-storage-state={status(storage)} data-storage-dirty={storage?.dirty ? 'true' : 'false'} title={storage?.error ?? (storage?.dirty ? 'Persistent changes are not durable yet.' : undefined)}>
      <span class="status-dot"></span><span>Storage {status(storage)}</span>
    </span>
    <span class="top-status compact advanced" data-midi-state={status(midi)} title={midi?.error ?? undefined}>
      <span class="status-dot"></span><span>MIDI {status(midi)}</span>
    </span>
    <span class="top-status audio advanced" data-audio-state={audioDisplayState} aria-label={`Audio ${audioDisplayState}`} title={audioTitle ?? undefined}>
      <span class="status-dot"></span><span>Audio {audioDisplayState}</span>
    </span>{/if}
    <button type="button" class="developer-toggle" class:enabled={developerMode}
      aria-label="Developer mode" aria-pressed={developerMode}
      onclick={() => onDeveloperModeChange(!developerMode)}>
      <span class="toggle-track" aria-hidden="true"><span></span></span>
      <span class="toggle-label">Developer</span>
    </button>
  </div>
</header>

<style>
  .top-bar { display:flex; align-items:center; justify-content:space-between; height:50px; padding:0 16px; flex-shrink:0; gap:14px; border-bottom:1px solid var(--border); background:var(--panel); font-size:.88rem; z-index:10; }
  .top-bar-left,.top-bar-right,.top-status { display:flex; align-items:center; }
  .top-bar-left { gap:12px; min-width:0; }
  .top-bar-right { gap:12px; }
  .top-bar-logo { display:block; width:59px; height:24px; object-fit:contain; }
  .top-bar-title { font-weight:600; font-size:.96rem; letter-spacing:.03em; white-space:nowrap; }
  .top-status { gap:7px; color:var(--muted); font-size:.78rem; white-space:nowrap; }
  .status-dot { width:8px; height:8px; flex-shrink:0; border-radius:50%; background:#f7c266; box-shadow:0 0 6px rgba(247,194,102,.4); }
  [data-runtime-state='ready'] .status-dot,[data-storage-state='ready'] .status-dot,[data-midi-state='ready'] .status-dot,[data-audio-state='running'] .status-dot { background:#3dd68c; box-shadow:0 0 6px rgba(61,214,140,.5); }
  [data-storage-dirty='true'] .status-dot { background:#f7c266; box-shadow:0 0 6px rgba(247,194,102,.4); }
  [data-audio-state='disabled'] .status-dot { background:#737984; box-shadow:none; }
  [data-runtime-state='failed'] .status-dot,[data-storage-state='failed'] .status-dot,[data-midi-state='failed'] .status-dot,[data-midi-state='denied'] .status-dot,[data-audio-state='failed'] .status-dot { background:#ff6b6b; box-shadow:0 0 6px rgba(255,107,107,.5); }
  .developer-toggle { display:flex; min-height:34px; align-items:center; gap:8px; padding:5px 8px; border:1px solid var(--border); border-radius:999px; color:var(--muted); background:rgba(255,255,255,.025); font-size:.72rem; }
  .developer-toggle:hover { color:var(--text); border-color:rgba(76,201,240,.35); }
  .toggle-track { position:relative; width:27px; height:15px; flex:0 0 auto; border:1px solid rgba(255,255,255,.17); border-radius:999px; background:#0c0d10; }
  .toggle-track span { position:absolute; left:2px; top:2px; width:9px; height:9px; border-radius:50%; background:#737984; transition:translate .18s cubic-bezier(.2,.8,.2,1),background .18s; }
  .developer-toggle.enabled { color:var(--accent); border-color:rgba(76,201,240,.32); background:var(--accent-soft); }
  .enabled .toggle-track span { translate:12px 0; background:var(--accent); }
  @media(max-width:1000px){ .compact { display:none; } }
  @media(max-width:720px){ .top-bar { height:48px; padding:0 10px; } .top-bar-logo{width:48px}.top-bar-title{font-size:.86rem}.top-status{display:none}.developer-toggle{min-width:64px;min-height:38px;justify-content:center}.toggle-label{font-size:0}.toggle-label::after{content:'DEV';font-size:.65rem;letter-spacing:.08em} }
</style>
