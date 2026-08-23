<script>
  export let runtime
  export let audio
  export let storage
  export let midi
  const status = (value, fallback = 'unavailable') => value?.state ?? fallback
</script>

<header class="top-bar">
  <div class="top-bar-left">
    <img class="top-bar-logo" src="/203dark.svg" alt="203 Systems" />
    <span class="top-bar-title">NullPerator</span>
  </div>
  <div class="top-bar-right">
    <span class="top-status" data-runtime-state={runtime.state} title={runtime.error ?? undefined}>
      <span class="status-dot"></span><span>Runtime {runtime.state}</span>
    </span>
    <span class="top-status" data-storage-state={status(storage)} data-storage-dirty={storage?.dirty ? 'true' : 'false'} title={storage?.error ?? (storage?.dirty ? 'Persistent changes are not durable yet.' : undefined)}>
      <span class="status-dot"></span><span>Storage {status(storage)}</span>
    </span>
    <span class="top-status compact" data-midi-state={status(midi)} title={midi?.error ?? undefined}>
      <span class="status-dot"></span><span>MIDI {status(midi)}</span>
    </span>
    <span class="top-status audio" data-audio-state={audio.state} aria-label={`Audio ${audio.state}`} title={audio.error ?? undefined}>
      <span class="status-dot"></span><span>Audio {audio.state}</span>
    </span>
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
  [data-runtime-state='failed'] .status-dot,[data-storage-state='failed'] .status-dot,[data-midi-state='failed'] .status-dot,[data-midi-state='denied'] .status-dot,[data-audio-state='failed'] .status-dot { background:#ff6b6b; box-shadow:0 0 6px rgba(255,107,107,.5); }
  @media(max-width:1000px){ .compact { display:none; } }
  @media(max-width:720px){ .top-status:not(.audio) { display:none; } .top-bar { padding:0 10px; } }
</style>
