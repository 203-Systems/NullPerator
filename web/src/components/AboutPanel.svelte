<script>
  import { version as packageVersion } from '../../package.json'

  export let buildMetadata = null
  export let developerMode = false
  const value = (candidate) => candidate ?? 'unavailable'
  const productVersion = packageVersion.replace(/\.0$/, '')
</script>

<section class="about-panel" aria-labelledby="about-heading">
  <div class="section-heading">
    <div><p class="eyebrow">NullPerator</p><h1 id="about-heading">About</h1></div>
    <span class="phase-badge">Version {productVersion}</span>
  </div>
  <div class="about-grid">
    <article><h2>Make music anywhere</h2>
      <p>The same NullPerator projects and tracker workflow are available on the Web, iOS and NullPerator hardware.</p>
    </article>
    <article><h2>Your files stay yours</h2>
      <p>Projects and samples are stored in this browser. They leave it only when you explicitly download, back up or sync them to a folder.</p>
    </article>
    <article><h2>Open source and licenses</h2>
      <p>NullPerator includes open-source libraries, fonts and platform components under their respective licenses.</p>
      <a href="/THIRD_PARTY_NOTICES.md" target="_blank" rel="noreferrer">Read third-party notices</a>
    </article>
    {#if developerMode}<article class="build-details"><h2>Developer build details</h2><dl>
      <div><dt>Commit</dt><dd>{value(buildMetadata?.commit)}{buildMetadata?.dirty ? ' (dirty)' : ''}</dd></div>
      <div><dt>Built</dt><dd>{value(buildMetadata?.builtAt)}</dd></div>
      <div><dt>Runtime</dt><dd>WebAssembly · pthreads · AudioWorklet</dd></div>
      <div><dt>Toolchain</dt><dd>{value(buildMetadata?.emscripten)}</dd></div>
    </dl></article>{/if}
  </div>
</section>

<style>
  .about-panel { width: min(100%, 960px); margin: 0 auto; }
  .about-grid { display: grid; grid-template-columns: repeat(auto-fit,minmax(240px,1fr)); gap: 12px; }
  article { padding: 16px; border: 1px solid var(--border); border-radius: var(--radius-card); background: var(--surface); }
  .build-details { grid-column: 1/-1; } h2 { font-size: 13px; } p, dt { color: var(--muted); font-size: 12px; line-height: 1.6; }
  a { display:inline-flex; min-width:44px; min-height:44px; align-items:center; color:var(--accent); font-size:12px; text-underline-offset:3px; }
  dl { margin: 0; } dl div { display: grid; grid-template-columns: 90px 1fr; padding: 8px 0; border-bottom: 1px solid var(--border); }
  dd { margin: 0; font: 11px var(--mono); overflow-wrap: anywhere; }
  @media (max-width: 700px) { .about-grid { grid-template-columns: 1fr; } .build-details { grid-column: auto; } }
</style>
