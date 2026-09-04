<script>
  export let runtime
  export let audio
  export let storage
  export let midi
  export let developerMode = false
  const status = (value, fallback = 'unavailable') => value?.state ?? fallback
  const runtimeLabel = (state) => ({ ready: 'Ready', booting: 'Starting', stopping: 'Stopping', failed: 'Needs attention', idle: 'Idle' })[state] ?? state
  $: audioDisplayState = audio?.capability?.mode === 'disabled'
    ? 'disabled'
    : status(audio)
  $: audioTitle = audioDisplayState === 'disabled'
    ? audio?.capability?.reason
    : audio?.error
  $: saving = Boolean(storage?.dirty || storage?.syncing || storage?.mutating)
  $: storageFailed = status(storage) === 'failed'
  $: storageLabel = storageFailed ? 'Saving failed' : saving ? 'Saving' : `Storage ${status(storage)}`
  $: storageActionable = saving || storageFailed
  $: midiActionable = Boolean(midi?.inputConnected || midi?.outputConnected || ['failed', 'denied'].includes(status(midi)))
  $: audioActionable = ['failed', 'locked', 'suspended'].includes(audioDisplayState)
  $: runtimeVisible = developerMode || runtime.state !== 'ready'
  $: storageVisible = developerMode || storageActionable
  $: midiVisible = developerMode || midiActionable
  $: audioVisible = developerMode || audioActionable
</script>

<header class="top-bar">
  <div class="top-bar-left">
    <img class="top-bar-logo" src="/203dark.svg" alt="203 Systems" />
    <span class="top-bar-title">NullPerator</span>
  </div>
  <div class="top-bar-right">
    {#if runtimeVisible}<span class="top-status runtime" class:developer-detail={developerMode && runtime.state === 'ready'} data-status-state={runtime.state} title={runtime.error ?? undefined}>
      <span class="status-dot"></span><span>{runtimeLabel(runtime.state)}</span>
    </span>{/if}
    {#if storageVisible}<span class="top-status secondary" class:developer-detail={developerMode && !storageActionable} data-storage-state={status(storage)} data-storage-dirty={storage?.dirty ? 'true' : 'false'} title={storage?.error ?? (saving ? 'Saving changes in this browser.' : undefined)}>
      <span class="status-dot"></span><span>{storageLabel}</span>
    </span>{/if}
    {#if midiVisible}<span class="top-status secondary" class:developer-detail={developerMode && !midiActionable} data-midi-state={status(midi)} title={midi?.error ?? undefined}>
      <span class="status-dot"></span><span>{midi?.inputConnected || midi?.outputConnected ? 'MIDI connected' : `MIDI ${status(midi)}`}</span>
    </span>{/if}
    {#if audioVisible}<span class="top-status audio secondary" class:developer-detail={developerMode && !audioActionable} data-audio-state={audioDisplayState} aria-label={`Audio ${audioDisplayState}`} title={audioTitle ?? undefined}>
      <span class="status-dot"></span><span>Audio {audioDisplayState}</span>
    </span>{/if}
    {#if developerMode}<span class="developer-badge">Developer tools</span>{/if}
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
  .status-dot { width:8px; height:8px; flex-shrink:0; border-radius:50%; background:var(--warning); box-shadow:0 0 6px rgba(247,194,102,.4); }
  [data-status-state='ready'] .status-dot,[data-storage-state='ready'] .status-dot,[data-midi-state='ready'] .status-dot,[data-audio-state='running'] .status-dot { background:var(--success-dot); box-shadow:0 0 6px rgba(61,214,140,.5); }
  [data-storage-dirty='true'] .status-dot { background:var(--warning); box-shadow:0 0 6px rgba(247,194,102,.4); }
  [data-audio-state='disabled'] .status-dot { background:var(--disabled); box-shadow:none; }
  [data-status-state='failed'] .status-dot,[data-storage-state='failed'] .status-dot,[data-midi-state='failed'] .status-dot,[data-midi-state='denied'] .status-dot,[data-audio-state='failed'] .status-dot { background:var(--danger); box-shadow:0 0 6px rgba(255,107,107,.5); }
  .developer-badge { padding:5px 7px; border:1px solid var(--accent-border-soft); border-radius:var(--radius-tight); color:var(--accent); background:var(--accent-soft); font:600 9px/1 var(--mono); letter-spacing:.06em; white-space:nowrap; }
  @media(max-width:1000px){ .developer-detail { display:none; } }
</style>
