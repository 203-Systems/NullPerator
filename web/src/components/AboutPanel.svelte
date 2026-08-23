<script>
  export let buildMetadata = null
  const value = (candidate) => candidate ?? 'unavailable'
</script>

<section class="about-panel" aria-labelledby="about-heading">
  <div class="section-heading">
    <div><p class="eyebrow">Build and licensing</p><h1 id="about-heading">About</h1></div>
    <span class="phase-badge">Static WASM workbench</span>
  </div>
  <div class="about-grid">
    <article><h2>Build identity</h2><dl>
      <div><dt>Commit</dt><dd>{value(buildMetadata?.commit)}{buildMetadata?.dirty ? ' (dirty)' : ''}</dd></div>
      <div><dt>Built</dt><dd>{value(buildMetadata?.builtAt)}</dd></div>
      <div><dt>Runtime</dt><dd>WebAssembly · pthreads · AudioWorklet</dd></div>
      <div><dt>Toolchain</dt><dd>{value(buildMetadata?.emscripten)}</dd></div>
    </dl></article>
    <article><h2>Third-party notices</h2>
      <p>The workbench shell adapts MatrixOS Mystrix Simulator WebUI concepts under the MIT License.</p>
      <p>PicoTracker includes additional libraries, fonts, and platform components under their respective licenses. The distributable notice file is authoritative.</p>
    </article>
    <article><h2>Privacy boundary</h2>
      <p>Projects, samples, logs, MIDI and traces stay in this browser or in folders you explicitly select. The workbench contains no telemetry uploader.</p>
    </article>
  </div>
</section>

<style>
  .about-panel { width: min(100%, 960px); margin: 0 auto; }
  .about-grid { display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: 14px; }
  article { padding: 18px; border: 1px solid var(--border); border-radius: 10px; background: var(--surface); }
  article:last-child { grid-column: 1/-1; } h2 { font-size: 13px; } p, dt { color: var(--muted); font-size: 12px; line-height: 1.6; }
  dl { margin: 0; } dl div { display: grid; grid-template-columns: 90px 1fr; padding: 8px 0; border-bottom: 1px solid var(--border); }
  dd { margin: 0; font: 11px ui-monospace,monospace; overflow-wrap: anywhere; }
  @media (max-width: 700px) { .about-grid { grid-template-columns: 1fr; } article:last-child { grid-column: auto; } }
</style>
