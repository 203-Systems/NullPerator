<script>
  import { onDestroy, onMount } from 'svelte'
  import { cubicOut } from 'svelte/easing'
  import { fade, fly, slide } from 'svelte/transition'

  export let runtime = null
  export let settings
  export let midi = null
  export let midiSnapshot = { state: 'unavailable' }
  export let onReboot = async () => {}

  let open = false
  let midiPage = false
  let privacyPage = false
  let busy = false
  let softwareBuildOpen = false
  let feedback = ''
  let settingsSnapshot = settings.snapshot()
  let controller = { connected: false, count: 0, names: [] }
  const detachSettings = settings.subscribe((next) => { settingsSnapshot = next })

  $: controllerLabel = controller.connected
    ? controller.names?.[0] ?? `${controller.count} connected`
    : 'Not connected'
  $: midiLabel = midiSnapshot.state === 'ready'
    ? (midiSnapshot.inputConnected || midiSnapshot.outputConnected ? 'Connected' : 'Not connected')
    : midiSnapshot.state === 'idle' ? 'Not connected'
    : midiSnapshot.state === 'unsupported' ? 'Unavailable on iOS' : midiSnapshot.state
  $: iosVersion = runtime?.buildMetadata?.iosVersion ?? 'Unknown'
  $: iosBuild = runtime?.buildMetadata?.iosBuild ?? 'Unknown'
  $: nullPeratorVersion = runtime?.buildMetadata?.nullPeratorVersion ?? 'Unknown'
  $: buildHash = runtime?.buildMetadata?.buildHash ?? 'Unknown'
  $: shortBuildHash = buildHash === 'Unknown' ? buildHash : String(buildHash).slice(0, 8)
  $: buildTime = runtime?.buildMetadata?.buildTime ?? 'Unknown'
  $: detailPage = midiPage || privacyPage
  $: pageEyebrow = privacyPage ? 'LEGAL' : midiPage ? 'MIDI' : 'NULLPERATOR'
  $: pageTitle = privacyPage ? 'PRIVACY POLICY' : midiPage ? 'ROUTE MAP' : 'SETTINGS'

  function nativeCommand(command) {
    if (command === 'openFiles' && globalThis.__nullPeratorHost?.openFiles) {
      return globalThis.__nullPeratorHost.openFiles()
    }
    return globalThis.webkit?.messageHandlers?.nullPeratorNative?.postMessage({ command })
  }

  async function reboot() {
    if (busy) return
    busy = true
    feedback = 'Saving…'
    try {
      await runtime?.storage?.flushNow?.('native-settings-reboot')
      feedback = 'Restarting…'
      await onReboot()
      open = false
    } catch (error) {
      feedback = error instanceof Error ? error.message : String(error)
    } finally {
      busy = false
    }
  }

  async function enableMidi() {
    feedback = ''
    try { await midi?.requestMidiAccess?.() }
    catch (error) { feedback = error instanceof Error ? error.message : String(error) }
  }

  async function openMidiPage() {
    midiPage = true
    feedback = ''
    if (midiSnapshot.state !== 'ready' && midiSnapshot.state !== 'requesting') {
      await enableMidi()
    }
  }

  async function selectMidiInput(event) {
    feedback = ''
    try { await midi?.selectMidiInput?.(event.currentTarget.value || null) }
    catch (error) { feedback = error instanceof Error ? error.message : String(error) }
  }

  async function selectMidiOutput(event) {
    feedback = ''
    try { await midi?.selectMidiOutput?.(event.currentTarget.value || null) }
    catch (error) { feedback = error instanceof Error ? error.message : String(error) }
  }

  onMount(() => {
    controller = globalThis.__nullPeratorControllerState ?? controller
    const controllerChanged = (event) => { controller = event.detail }
    const keydown = (event) => {
      if (event.key !== 'Escape') return
      if (detailPage) { midiPage = false; privacyPage = false }
      else open = false
    }
    globalThis.addEventListener('nullperator-controller-change', controllerChanged)
    globalThis.addEventListener('keydown', keydown)
    return () => {
      globalThis.removeEventListener('nullperator-controller-change', controllerChanged)
      globalThis.removeEventListener('keydown', keydown)
    }
  })
  onDestroy(detachSettings)
</script>

<button class="settings-trigger" type="button" aria-label="Open settings" aria-expanded={open}
  onclick={() => { open = true; midiPage = false; privacyPage = false; softwareBuildOpen = false; feedback = '' }}>
  <span></span><span></span><span></span>
</button>

{#if open}
  <div class="settings-backdrop" role="presentation" transition:fade={{ duration: 160 }}
    onclick={(event) => { if (event.target === event.currentTarget) open = false }}>
    <div class="settings-sheet" role="dialog" aria-modal="true" aria-labelledby="native-settings-title"
      transition:fly={{ y: 32, duration: 240, easing: cubicOut }}>
      <header>
        <div class="header-title">
          {#if detailPage}<button class="back" type="button" aria-label="Back to settings" onclick={() => { midiPage = false; privacyPage = false }}><span aria-hidden="true">←</span></button>{/if}
          <div><small>{pageEyebrow}</small><h2 id="native-settings-title">{pageTitle}</h2></div>
        </div>
        <button class="close" type="button" aria-label="Close settings" onclick={() => { open = false; midiPage = false; privacyPage = false }}><span aria-hidden="true">×</span></button>
      </header>

      {#if midiPage}
        <div class="midi-page">
          <button class="bluetooth-row" type="button" onclick={() => nativeCommand('openBluetoothMidi')}>
            <span class="bluetooth-mark" aria-hidden="true">
              <svg viewBox="0 0 24 24" focusable="false">
                <path d="M17.71 7.71 12 2v7.59L7.41 5 6 6.41 11.59 12 6 17.59 7.41 19 12 14.41V22l5.71-5.71L13.41 12l4.3-4.29ZM14 6.83l.88.88-.88.88V6.83Zm0 8.58.88.88-.88.88v-1.76Z" />
              </svg>
            </span>
            <span class="row-copy"><strong>BLUETOOTH MIDI</strong><small>Open iOS Bluetooth MIDI connections</small></span>
            <span class="chevron">›</span>
          </button>

          <section class="route-map" aria-label="MIDI route map">
            <div class="route-card">
              <div class="route-heading"><strong>INPUT ROUTE</strong><small>External MIDI into NullPerator</small></div>
              <div class="route-flow">
                <label>
                  <span>SOURCE</span>
                  <select disabled={midiSnapshot.state !== 'ready'} value={midiSnapshot.selectedInputId ?? ''} onchange={selectMidiInput}>
                    <option value="">OFF</option>
                    {#each midiSnapshot.inputs ?? [] as port (port.id)}
                      <option value={port.id}>{port.manufacturer ? `${port.manufacturer} · ` : ''}{port.name}</option>
                    {/each}
                  </select>
                </label>
                <span class="route-arrow">→</span>
                <span class="route-target">NULLPERATOR</span>
              </div>
            </div>

            <div class="route-card">
              <div class="route-heading"><strong>OUTPUT ROUTE</strong><small>NullPerator to external MIDI</small></div>
              <div class="route-flow output-flow">
                <span class="route-target">NULLPERATOR</span>
                <span class="route-arrow">→</span>
                <label>
                  <span>DESTINATION</span>
                  <select disabled={midiSnapshot.state !== 'ready'} value={midiSnapshot.selectedOutputId ?? ''} onchange={selectMidiOutput}>
                    <option value="">OFF</option>
                    {#each midiSnapshot.outputs ?? [] as port (port.id)}
                      <option value={port.id}>{port.manufacturer ? `${port.manufacturer} · ` : ''}{port.name}</option>
                    {/each}
                  </select>
                </label>
              </div>
            </div>
          </section>

          {#if midiSnapshot.state !== 'ready'}
            <button class="enable-midi" type="button" disabled={midiSnapshot.state === 'requesting'} onclick={enableMidi}>
              {midiSnapshot.state === 'requesting' ? 'CONNECTING…' : 'ENABLE MIDI'}
            </button>
          {/if}
          <p class="midi-status" class:online={midiSnapshot.inputConnected || midiSnapshot.outputConnected}>
            {midiLabel}
          </p>
        </div>
      {:else if privacyPage}
        <div class="privacy-page">
          <p class="privacy-intro">Last updated August 30, 2026</p>

          <section>
            <strong>NO DATA COLLECTION</strong>
            <p>NullPerator does not collect, transmit, sell, or use personal data for tracking. The app contains no advertising or analytics SDK.</p>
          </section>

          <section>
            <strong>ON-DEVICE DATA</strong>
            <p>Projects, samples, settings, controller mappings, and MIDI route choices are stored locally on your device. Files are shared only when you choose to use the iOS Files app.</p>
          </section>

          <section>
            <strong>MIDI, BLUETOOTH & CONTROLLERS</strong>
            <p>MIDI messages and controller input are processed locally to operate the tracker. NullPerator does not upload this information.</p>
          </section>

          <section>
            <strong>EXTERNAL LINKS</strong>
            <p>The hardware purchase button opens 203.io outside the app. Information you provide there is governed by the website's privacy terms.</p>
          </section>

          <button class="privacy-contact" type="button" onclick={() => nativeCommand('openPrivacyPolicy')}>
            VIEW ONLINE PRIVACY POLICY <span aria-hidden="true">↗</span>
          </button>
        </div>
      {:else}
        <div class="settings-list">
        <div class="setting-row">
          <div class="row-copy"><strong>CONTROLLER</strong><small>{controllerLabel}</small></div>
          <span class:online={controller.connected} class="status-dot" aria-hidden="true"></span>
        </div>

        <button class="setting-row tappable" type="button"
          aria-pressed={settingsSnapshot.hideControlsWithGamepad}
          onclick={() => settings.update({ hideControlsWithGamepad: !settingsSnapshot.hideControlsWithGamepad })}>
          <span class="row-copy"><strong>HIDE TOUCH CONTROLS</strong><small>When a gamepad is connected</small></span>
          <span class:on={settingsSnapshot.hideControlsWithGamepad} class="toggle" aria-hidden="true"><i></i></span>
        </button>

        <button class="setting-row tappable" type="button" onclick={openMidiPage}>
          <span class="row-copy"><strong>MIDI</strong><small>{midiLabel} · Route Map & Bluetooth</small></span>
          <span class="chevron">›</span>
        </button>

        <button class="setting-row tappable" type="button" onclick={() => nativeCommand('openFiles')}>
          <span class="row-copy"><strong>FILES</strong><small>Open the NullPerator folder in Files</small></span>
          <span class="chevron">›</span>
        </button>

        <button class="setting-row tappable purchase-row" type="button" onclick={() => nativeCommand('purchaseHardware')}>
          <span class="row-copy"><strong>PURCHASE NULLPERATOR HARDWARE</strong><small>Open the official 203.io product page</small></span>
          <span class="chevron">↗</span>
        </button>

        <button class="setting-row tappable" type="button" onclick={() => { privacyPage = true; feedback = '' }}>
          <span class="row-copy"><strong>PRIVACY POLICY</strong><small>How NullPerator handles your data</small></span>
          <span class="chevron">›</span>
        </button>

        <button class="setting-row tappable version-row" type="button" aria-expanded={softwareBuildOpen}
          onclick={() => { softwareBuildOpen = !softwareBuildOpen }}>
          <div class="row-copy"><strong>SOFTWARE VERSION</strong><small>Application and firmware builds</small></div>
          <div class="version-side">
            <div class="version-values">
              <b>iOS</b><span>{iosVersion} ({iosBuild})</span>
              <b>NULLPERATOR</b><span>{nullPeratorVersion}</span>
            </div>
            <span class:open={softwareBuildOpen} class="version-disclosure" aria-hidden="true">›</span>
          </div>
        </button>
        {#if softwareBuildOpen}
          <div class="build-details" transition:slide={{ duration: 180, easing: cubicOut }}>
            <div><span>BUILD HASH</span><code>{shortBuildHash}</code></div>
            <div><span>BUILD TIME</span><time>{buildTime}</time></div>
          </div>
        {/if}
        </div>

        <button class="reboot" type="button" disabled={busy || runtime?.state === 'booting' || runtime?.state === 'stopping'} onclick={reboot}>
          {busy ? 'PLEASE WAIT' : 'REBOOT NULLPERATOR'}
        </button>
      {/if}
      {#if feedback}<p class="feedback" role="status">{feedback}</p>{/if}
    </div>
  </div>
{/if}

<style>
  button { font:inherit; }
  .settings-trigger { position:fixed; z-index:20; right:max(8px,env(safe-area-inset-right)); bottom:calc(max(8px,env(safe-area-inset-bottom)) + 10px); display:grid; width:42px; height:42px; padding:10px 9px; align-content:space-between; border:1px solid #777; border-radius:5px; color:#ddd; background:linear-gradient(145deg,#171717,#080808); box-shadow:inset 0 0 0 3px #070707,inset 0 0 0 4px #303030,0 2px 6px rgba(0,0,0,.75); -webkit-touch-callout:none; -webkit-user-select:none; user-select:none; }
  .settings-trigger span { display:block; width:100%; height:1px; background:#aaa; }
  .settings-trigger span:nth-child(1),.settings-trigger span:nth-child(2),.settings-trigger span:nth-child(3){width:100%}
  .settings-trigger:active { border-color:#fff; background:#171717; }
  .settings-backdrop { position:fixed; z-index:30; inset:0; display:flex; align-items:flex-end; justify-content:center; padding:0; background:rgba(0,0,0,.7); backdrop-filter:blur(5px); }
  .settings-sheet { width:min(100%,620px); max-height:calc(100dvh - env(safe-area-inset-top) - 18px); overflow:auto; padding:18px 18px calc(env(safe-area-inset-bottom) + 18px); border:1px solid #3b3b3b; border-bottom:0; border-radius:14px 14px 0 0; color:#eee; background:#080808; box-shadow:0 -20px 60px rgba(0,0,0,.55); font-family:ui-monospace,SFMono-Regular,Menlo,monospace; }
  header { display:flex; align-items:center; justify-content:space-between; padding:2px 2px 16px; }
  .header-title { display:flex; min-width:0; align-items:center; gap:10px; }
  header small { color:#49d6e6; font-size:8px; letter-spacing:.18em; }
  h2 { margin:4px 0 0; font-size:16px; letter-spacing:.08em; }
  .back,.close { display:grid; width:34px; height:34px; padding:0; place-items:center; border:1px solid #444; border-radius:3px; color:#bbb; background:#0d0d0d; }
  .back { font:400 18px/1 system-ui; }
  .close { font:300 22px/1 system-ui; }
  .back span,.close span { display:block; line-height:1; }
  .back:active,.close:active { color:#050505; border-color:#49d6e6; background:#49d6e6; }
  .settings-list { border-top:1px solid #292929; }
  .setting-row { display:flex; min-height:64px; width:100%; align-items:center; justify-content:space-between; gap:16px; padding:12px 2px; border:0; border-bottom:1px solid #292929; color:inherit; background:transparent; text-align:left; }
  .row-copy { display:grid; min-width:0; gap:5px; }
  .row-copy strong { font-size:10px; letter-spacing:.1em; }
  .row-copy small { overflow:hidden; color:#777; font-size:9px; text-overflow:ellipsis; white-space:nowrap; }
  .status-dot { width:8px; height:8px; flex:0 0 auto; border:1px solid #666; border-radius:50%; background:#222; }
  .status-dot.online { border-color:#49d6e6; background:#49d6e6; box-shadow:0 0 9px rgba(73,214,230,.45); }
  .toggle { position:relative; width:24px; height:24px; flex:0 0 auto; border:1px solid #4a4a4a; border-radius:3px; background:#151515; transition:border-color 140ms ease,background 140ms ease; }
  .toggle i { position:absolute; inset:5px; border-radius:1px; background:#555; transition:background 140ms ease; }
  .toggle.on { border-color:#49d6e6; background:#102a2d; }
  .toggle.on i { background:#49d6e6; }
  .version-values { display:grid; grid-template-columns:max-content max-content; flex:0 0 auto; align-items:baseline; column-gap:8px; row-gap:5px; color:#aaa; font-size:9px; letter-spacing:.04em; }
  .version-values span { white-space:nowrap; text-align:left; }
  .version-values b { color:#666; font-size:8px; letter-spacing:.08em; text-align:right; }
  .version-row { grid-column:1 / -1; }
  .version-side { display:flex; flex:0 0 auto; align-items:center; gap:12px; }
  .version-disclosure { display:grid; width:22px; height:22px; place-items:center; color:#888; font:300 25px/1 system-ui; transition:transform 160ms ease,color 120ms ease; }
  .version-disclosure.open { transform:rotate(90deg); }
  .version-row:active .version-disclosure { color:#49d6e6; }
  .build-details { display:grid; grid-column:1 / -1; gap:9px; padding:11px 12px; border-bottom:1px solid #292929; background:#0d0d0d; }
  .build-details > div { display:grid; grid-template-columns:78px minmax(0,1fr); align-items:start; gap:12px; }
  .build-details span { color:#666; font-size:8px; letter-spacing:.1em; }
  .build-details code,.build-details time { min-width:0; color:#aaa; font:8px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace; overflow-wrap:anywhere; text-align:right; }
  .tappable { cursor:pointer; }
  .purchase-row strong { color:#49d6e6; }
  .chevron { color:#888; font:300 25px/1 system-ui; }
  .reboot { width:100%; height:44px; margin-top:18px; border:1px solid #555; border-radius:3px; color:#ddd; background:#111; font-size:9px; letter-spacing:.12em; }
  .reboot:active:not(:disabled) { color:#050505; border-color:#49d6e6; background:#49d6e6; }
  .reboot:disabled { opacity:.45; }
  .feedback { margin:10px 0 0; color:#888; font-size:9px; text-align:center; }
  .midi-page { display:grid; min-height:0; gap:12px; overflow:auto; border-top:1px solid #292929; padding-top:12px; overscroll-behavior:contain; }
  .bluetooth-row { display:grid; grid-template-columns:34px minmax(0,1fr) auto; min-height:58px; width:100%; align-items:center; gap:12px; padding:8px 12px; border:1px solid #3b3b3b; border-radius:6px; color:inherit; background:#101010; text-align:left; }
  .bluetooth-row:active { border-color:#49d6e6; }
  .bluetooth-mark { display:grid; width:30px; height:30px; place-items:center; border:1px solid #3d6870; border-radius:3px; color:#49d6e6; background:#0a1719; }
  .bluetooth-mark svg { width:18px; height:18px; overflow:visible; }
  .bluetooth-mark path { fill:currentColor; }
  .route-map { display:grid; gap:10px; }
  .route-card { display:grid; gap:12px; padding:12px; border:1px solid #292929; border-radius:6px; background:#0d0d0d; }
  .route-heading { display:flex; align-items:baseline; justify-content:space-between; gap:12px; }
  .route-heading strong { font-size:9px; letter-spacing:.1em; }
  .route-heading small { color:#666; font-size:8px; }
  .route-flow { display:grid; grid-template-columns:minmax(0,1fr) auto minmax(92px,.55fr); align-items:end; gap:10px; }
  .route-flow.output-flow { grid-template-columns:minmax(92px,.55fr) auto minmax(0,1fr); }
  .route-flow label { display:grid; min-width:0; gap:5px; }
  .route-flow label>span { color:#666; font-size:7px; letter-spacing:.12em; }
  .route-flow select { width:100%; height:34px; min-width:0; padding:0 8px; border:1px solid #444; border-radius:4px; color:#ddd; background:#111; font:9px ui-monospace,SFMono-Regular,Menlo,monospace; text-overflow:ellipsis; }
  .route-flow select:disabled { opacity:.5; }
  .route-arrow { align-self:center; color:#49d6e6; font-size:17px; }
  .route-target { display:grid; height:34px; place-items:center; border:1px solid #31555b; border-radius:4px; color:#49d6e6; background:#0a1719; font-size:8px; letter-spacing:.08em; }
  .enable-midi { width:100%; height:38px; border:1px solid #49d6e6; border-radius:4px; color:#071012; background:#49d6e6; font-size:8px; letter-spacing:.12em; }
  .enable-midi:disabled { opacity:.45; }
  .midi-status { margin:0; color:#777; font-size:8px; text-align:center; text-transform:uppercase; }
  .midi-status.online { color:#49d6e6; }
  .privacy-page { display:grid; gap:12px; min-height:0; overflow:auto; border-top:1px solid #292929; padding-top:12px; overscroll-behavior:contain; }
  .privacy-intro { margin:0; color:#666; font-size:8px; letter-spacing:.06em; }
  .privacy-page section { display:grid; gap:6px; padding:12px; border:1px solid #292929; border-radius:3px; background:#0d0d0d; }
  .privacy-page section strong { color:#ddd; font-size:9px; letter-spacing:.1em; }
  .privacy-page section p { margin:0; color:#888; font-size:9px; line-height:1.55; }
  .privacy-contact { min-height:40px; border:1px solid #3d6870; border-radius:3px; color:#49d6e6; background:#0a1719; font-size:8px; letter-spacing:.1em; }
  .privacy-contact:active { color:#050505; border-color:#49d6e6; background:#49d6e6; }
  @media(orientation:landscape){
    .settings-backdrop {
      align-items:center;
      padding:
        max(10px,env(safe-area-inset-top))
        max(18px,calc(env(safe-area-inset-right) + 10px))
        max(10px,env(safe-area-inset-bottom))
        max(18px,calc(env(safe-area-inset-left) + 10px));
    }
    .settings-sheet {
      display:grid;
      grid-template-rows:auto minmax(0,1fr) auto auto;
      width:min(760px,100%);
      max-height:100%;
      overflow:hidden;
      padding:12px 16px;
      border:1px solid #3b3b3b;
      border-radius:10px;
      box-shadow:0 20px 70px rgba(0,0,0,.72);
    }
    header { padding:0 0 9px; }
    header small { font-size:7px; }
    h2 { margin-top:2px; font-size:14px; }
    .close { width:30px; height:30px; font-size:22px; line-height:26px; }
    .back { width:30px; height:30px; font-size:24px; line-height:25px; }
    .settings-list {
      display:grid;
      grid-template-columns:minmax(0,1fr) minmax(0,1fr);
      column-gap:22px;
      overflow:auto;
      overscroll-behavior:contain;
    }
    .setting-row { min-height:50px; gap:12px; padding:7px 2px; }
    .settings-list > .setting-row:last-child { grid-column:1 / -1; }
    .row-copy { gap:3px; }
    .row-copy strong { font-size:9px; }
    .row-copy small { font-size:8px; }
    .reboot { height:38px; margin-top:11px; }
    .feedback { margin-top:7px; }
    .midi-page { grid-template-columns:1fr 1fr; grid-template-rows:auto minmax(0,1fr) auto; column-gap:14px; }
    .bluetooth-row { grid-column:1 / -1; min-height:50px; }
    .route-map { grid-column:1 / -1; grid-template-columns:1fr 1fr; }
    .route-card { min-height:112px; padding:10px; }
    .route-heading { display:grid; gap:3px; }
    .enable-midi { grid-column:1 / -1; }
    .midi-status { grid-column:1 / -1; }
    .privacy-page { grid-template-columns:1fr 1fr; column-gap:14px; }
    .privacy-intro,.privacy-contact { grid-column:1 / -1; }
    .privacy-page section { padding:10px; }
  }
</style>
