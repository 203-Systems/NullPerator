<script>
  import { onDestroy } from 'svelte'

  export let logs = null
  let handle = null
  let unsubscribe = () => {}
  let feedback = ''
  let snapshot = { records: [], categories: [], threads: [], retained: 0, capacity: 0, dropped: 0, paused: false, filter: {} }

  $: if (logs !== handle) {
    unsubscribe()
    handle = logs
    snapshot = logs?.snapshot?.() ?? snapshot
    unsubscribe = logs?.subscribe?.((next) => (snapshot = next)) ?? (() => {})
  }
  const run = async (work, success) => {
    feedback = ''
    try { await work(); feedback = success }
    catch (error) { feedback = error instanceof Error ? error.message : String(error) }
  }
  const time = (value) => Number.isFinite(value) && value > 0
    ? new Date(value).toLocaleTimeString([], { hour12: false, fractionalSecondDigits: 3 }) : '—'
  onDestroy(() => unsubscribe())
</script>

<section class="logs-panel" aria-labelledby="logs-heading">
  <div class="section-heading">
    <div><p class="eyebrow">Bounded runtime diagnostics</p><h1 id="logs-heading">Logs</h1></div>
    <span class="phase-badge">{snapshot.paused ? 'paused' : 'live'}</span>
  </div>

  <div class="log-toolbar">
    <label><span>Severity</span><select value={snapshot.filter.minimumSeverity} onchange={(event) => logs.setLogFilter({ minimumSeverity: event.currentTarget.value })}>
      {#each ['debug', 'info', 'warn', 'error'] as severity}<option value={severity}>{severity}</option>{/each}
    </select></label>
    <label><span>Category</span><select value={snapshot.filter.category} onchange={(event) => logs.setLogFilter({ category: event.currentTarget.value })}>
      <option value="">All</option>{#each snapshot.categories as category}<option value={category}>{category}</option>{/each}
    </select></label>
    <label><span>Thread</span><select value={snapshot.filter.thread} onchange={(event) => logs.setLogFilter({ thread: event.currentTarget.value })}>
      <option value="">All</option>{#each snapshot.threads as thread}<option value={thread}>{thread}</option>{/each}
    </select></label>
    <label class="search"><span>Search</span><input type="search" value={snapshot.filter.text} oninput={(event) => logs.setLogFilter({ text: event.currentTarget.value })} /></label>
  </div>

  <div class="log-actions">
    <button type="button" onclick={() => logs.setPaused(!snapshot.paused)}>{snapshot.paused ? 'Resume' : 'Pause'}</button>
    <button type="button" onclick={() => { logs.clearLogs(); feedback = '' }}>Clear</button>
    <button type="button" onclick={() => run(() => logs.copyLogs(), 'Copied filtered logs')}>Copy</button>
    <button type="button" onclick={() => run(() => logs.downloadLogs(), 'Downloaded filtered logs')}>Download JSONL</button>
    <span>{snapshot.records.length} shown / {snapshot.retained} retained / {snapshot.capacity} capacity</span>
    <span class:dropped={snapshot.dropped > 0}>{snapshot.dropped} dropped</span>
  </div>

  {#if feedback}<p class="feedback" role="status">{feedback}</p>{/if}
  <ol class="log-list" aria-label="Runtime log records">
    {#each snapshot.records as record (record.sequence)}
      <li class:log-error={record.severity === 'error'}>
        <time>{time(record.wallTime)}</time><span class="severity">{record.severity}</span>
        <span class="category">{record.category}</span><span class="thread">{record.thread}</span>
        <span class="message">{record.message}{record.truncated ? ' …[truncated]' : ''}{record.repeat > 1 ? ` ×${record.repeat}` : ''}</span>
      </li>
    {:else}<li class="empty">No log records match the current filter.</li>{/each}
  </ol>
</section>

<style>
  .logs-panel { width: min(100%, 1180px); margin: 0 auto; }
  .log-toolbar { display: grid; grid-template-columns: repeat(3, minmax(110px, .5fr)) minmax(220px, 1.5fr); gap: 10px; }
  label { display: grid; gap: 5px; color: var(--muted); font-size: 11px; }
  select, input, button { border: 1px solid var(--border); border-radius: 7px; padding: 8px 10px; color: #e7e7ea; background: var(--surface-raised); }
  button { cursor: pointer; }
  .log-actions { display: flex; align-items: center; flex-wrap: wrap; gap: 8px; margin: 12px 0; color: var(--muted); font-size: 11px; }
  .dropped { color: #f28a8a; }
  .feedback { color: var(--muted); font-size: 12px; }
  .log-list { max-height: calc(100vh - 310px); min-height: 260px; overflow: auto; margin: 0; padding: 0; border: 1px solid var(--border); border-radius: 9px; background: #0c0d10; list-style: none; }
  li { display: grid; grid-template-columns: 92px 48px 100px 90px minmax(0, 1fr); gap: 8px; padding: 7px 10px; border-bottom: 1px solid rgba(255,255,255,.045); font: 11px/1.35 ui-monospace, monospace; }
  time, .thread { color: var(--muted); } .severity, .category { text-transform: uppercase; } .message { overflow-wrap: anywhere; white-space: pre-wrap; }
  .log-error .severity, .log-error .message { color: #f28a8a; } .empty { display: block; color: var(--muted); padding: 18px; }
  @media (max-width: 820px) { .log-toolbar { grid-template-columns: 1fr 1fr; } li { grid-template-columns: 76px 44px 78px minmax(0,1fr); } .thread { display: none; } }
</style>
