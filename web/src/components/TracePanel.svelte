<script>
  import { onDestroy } from 'svelte'
  import { TRACE_CATEGORIES } from '../trace/registry.js'

  export let trace = null
  let handle = null
  let unsubscribe = () => {}
  let feedback = ''
  let iterations = 256
  let deadlineUs = 3000
  let snapshot = { state: 'unavailable', mask: 1023, recordCount: 0, capacity: 0, dropped: 0, summaries: [], benchmark: null, error: null, captureDurationMs: 0 }
  const categories = Object.entries(TRACE_CATEGORIES).map(([bit, name]) => ({ bit: Number(bit), name }))

  $: if (trace !== handle) {
    unsubscribe(); handle = trace
    snapshot = trace?.snapshot?.() ?? snapshot
    unsubscribe = trace?.subscribe?.((next) => (snapshot = next)) ?? (() => {})
  }
  const run = async (work, success = '') => {
    feedback = ''
    try { await work(); feedback = success }
    catch (error) { feedback = error instanceof Error ? error.message : String(error) }
  }
  const toggleCategory = (bit, checked) => trace.setMask(checked ? snapshot.mask | bit : snapshot.mask & ~bit)
  const microseconds = (value) => `${Number(value ?? 0).toLocaleString()} µs`
  const duration = (value) => `${(Number(value ?? 0) / 1000).toFixed(2)} s`
  onDestroy(() => unsubscribe())
</script>

<section class="trace-panel" aria-labelledby="trace-heading" data-trace-state={snapshot.state}>
  <div class="section-heading">
    <div><p class="eyebrow">Allocation-free native scopes</p><h1 id="trace-heading">Performance Trace</h1></div>
    <span class="phase-badge">{snapshot.state}</span>
  </div>

  <div class="trace-card">
    <fieldset disabled={!trace || snapshot.state === 'capturing'}>
      <legend>Capture categories</legend>
      <div class="categories">{#each categories as category}
        <label><input type="checkbox" checked={(snapshot.mask & category.bit) !== 0} onchange={(event) => toggleCategory(category.bit, event.currentTarget.checked)} />{category.name}</label>
      {/each}</div>
    </fieldset>
    <div class="actions">
      {#if snapshot.state === 'capturing'}
        <button type="button" onclick={() => run(() => trace.stop(), 'Capture stopped')}>Stop capture</button>
      {:else}
        <button type="button" disabled={!trace} onclick={() => run(() => trace.start(), 'Capture started')}>Start capture</button>
      {/if}
      <button type="button" disabled={!trace} onclick={() => { trace.clear(); feedback = '' }}>Clear</button>
      <button type="button" disabled={!trace || snapshot.recordCount === 0} onclick={() => run(() => trace.downloadTrace(), 'Chrome trace downloaded')}>Download Chrome JSON</button>
      <span>{snapshot.recordCount} events / {snapshot.capacity} capacity</span>
      <span data-trace-duration-ms={Math.trunc(snapshot.captureDurationMs)}>{duration(snapshot.captureDurationMs)}</span>
      <span class:dropped={snapshot.dropped > 0}>{snapshot.dropped} overwritten</span>
    </div>
  </div>

  <div class="benchmark-card">
    <div><h2>Synthetic DSP microbenchmark</h2><p>Triangle-wave fixture v1 · 8 channels · 64 rows · 128-frame stereo blocks · not the tracker engine</p></div>
    <label>Blocks <input type="number" min="1" max="2048" bind:value={iterations} /></label>
    <label>Deadline µs <input type="number" min="0" max="1000000" bind:value={deadlineUs} /></label>
    <button type="button" disabled={!trace} onclick={() => run(() => trace.runBenchmark({ iterations, warmupIterations: 8, deadlineUs }), 'Benchmark complete')}>Run benchmark</button>
    {#if snapshot.benchmark}<dl>
      <div><dt>Median</dt><dd>{microseconds(snapshot.benchmark.medianUs)}</dd></div>
      <div><dt>p95 / p99</dt><dd>{microseconds(snapshot.benchmark.p95Us)} / {microseconds(snapshot.benchmark.p99Us)}</dd></div>
      <div><dt>Maximum</dt><dd>{microseconds(snapshot.benchmark.maximumUs)}</dd></div>
      <div><dt>Misses</dt><dd>{snapshot.benchmark.deadlineMisses}</dd></div>
      <div><dt>Fixture hash</dt><dd>0x{snapshot.benchmark.fixtureHash.toString(16).padStart(8, '0')}</dd></div>
      <div><dt>Total work</dt><dd>{snapshot.benchmark.totalWork.toLocaleString()}</dd></div>
    </dl>{/if}
  </div>

  {#if feedback || snapshot.error}<p role="status" class:error={snapshot.error}>{feedback || snapshot.error}</p>{/if}
  <div class="summary-card">
    <h2>Scope and input-to-frame latency summaries</h2>
    <table><thead><tr><th>Scope</th><th>Thread</th><th>Count</th><th>Result</th><th>p50</th><th>p95</th><th>p99</th><th>Max</th></tr></thead>
      <tbody>{#each snapshot.summaries as summary}
        <tr><td><small>{summary.categoryName}</small>{summary.nameText}</td><td>{summary.threadName}</td><td>{summary.count}</td><td>{summary.successCount || summary.failureCount ? `${summary.successCount} ok / ${summary.failureCount} failed` : '—'}</td><td>{microseconds(summary.p50Us)}</td><td>{microseconds(summary.p95Us)}</td><td>{microseconds(summary.p99Us)}</td><td>{microseconds(summary.maxUs)}</td></tr>
      {:else}<tr><td colspan="8">No complete scopes or latency samples captured yet.</td></tr>{/each}</tbody>
    </table>
  </div>
</section>

<style>
  .trace-panel { width: min(100%, 1120px); margin: 0 auto; }
  .trace-card, .benchmark-card, .summary-card { margin-bottom: 14px; padding: 16px; border: 1px solid var(--border); border-radius: 10px; background: var(--surface); }
  fieldset { border: 0; margin: 0; padding: 0; } legend, h2 { margin: 0 0 9px; font-size: 13px; } .categories, .actions { display: flex; flex-wrap: wrap; gap: 9px 14px; align-items: center; }
  .categories label, .benchmark-card label { color: var(--muted); font-size: 11px; text-transform: capitalize; }
  button, input { border: 1px solid var(--border); border-radius: 7px; padding: 8px 10px; color: #e7e7ea; background: var(--surface-raised); } button:not(:disabled) { cursor: pointer; }
  .actions { margin-top: 14px; color: var(--muted); font-size: 11px; } .dropped, .error { color: #f28a8a; }
  .benchmark-card { display: grid; grid-template-columns: minmax(240px,1fr) 120px 140px auto; gap: 12px; align-items: end; } .benchmark-card p { margin: 0; color: var(--muted); font-size: 11px; }
  .benchmark-card label { display: grid; gap: 5px; } dl { grid-column: 1/-1; display: grid; grid-template-columns: repeat(6, minmax(0,1fr)); gap: 8px; margin: 8px 0 0; } dl div { padding: 9px; background: rgba(255,255,255,.025); } dt { color: var(--muted); font-size: 10px; } dd { margin: 4px 0 0; font: 11px ui-monospace, monospace; }
  table { width: 100%; border-collapse: collapse; font: 11px ui-monospace, monospace; } th, td { padding: 8px; border-bottom: 1px solid var(--border); text-align: right; } th:first-child, td:first-child { text-align: left; } td small { display: block; color: var(--muted); text-transform: uppercase; }
  @media (max-width: 850px) { .benchmark-card { grid-template-columns: 1fr 1fr; } dl { grid-template-columns: repeat(2,1fr); } .summary-card { overflow-x: auto; } }
</style>
