<script>
  import { onMount, tick } from 'svelte'
  import DevicePanel from './components/DevicePanel.svelte'
  import TopBar from './components/TopBar.svelte'
  import MobileNavigation from './components/MobileNavigation.svelte'
  import LeftNav from './components/LeftNav.svelte'
  import ErrorBoundary from './components/ErrorBoundary.svelte'
  import SettingsPanel from './components/SettingsPanel.svelte'
  import FilesPanel from './components/FilesPanel.svelte'
  import MidiPanel from './components/MidiPanel.svelte'
  import LogsPanel from './components/LogsPanel.svelte'
  import NativeSettingsOverlay from './components/NativeSettingsOverlay.svelte'
  import TracePanel from './components/TracePanel.svelte'
  import { isDeveloperSection, visibleSections } from './navigation.js'
  import { runtimeStore } from './stores/runtime.js'
  import { nativeRuntimeStore } from './stores/nativeRuntime.js'
  import { nativeAppSettingsStore } from './stores/nativeAppSettings.js'
  import { settingsStore } from './stores/settings.js'

  const query = new URLSearchParams(window.location.search)
  const nativeHostActive = globalThis.__nullPeratorNativeCore === true
  const activeRuntimeStore = nativeHostActive ? nativeRuntimeStore : runtimeStore
  if (nativeHostActive) document.documentElement.classList.add('native-host')
  const forceDeveloperMode = query.get('dev') === '1' || query.get('views-test') === '1' || query.get('inputDiagnostics') === '1'
  const mobileViewport = window.matchMedia('(max-width: 720px), (orientation: landscape) and (max-width: 960px) and (max-height: 539px)')
  const resolveDeveloperMode = (preference) => !nativeHostActive && (forceDeveloperMode || preference === true)
  let activeSection = 'Device'
  let developerMode = resolveDeveloperMode(settingsStore.snapshot().developerMode)
  let compactLayout = nativeHostActive || mobileViewport.matches
  let mobileMenuOpen = false
  let runtime = activeRuntimeStore.getSnapshot()
  let audio = runtime.audio?.snapshot?.() ?? { state:'unavailable', metrics:null, capability:null }
  let midi = runtime.midi?.snapshot?.() ?? { state:'unavailable' }
  let storage = runtime.storage?.snapshot?.() ?? { state:'unavailable' }
  let canvasGeneration = 0
  let recoveryButton
  let recoveryFocusRevision = 0
  let detachAudio=()=>{}, detachMidi=()=>{}, detachStorage=()=>{}, detachSettings=()=>{}

  function restoreRuntimeFocus(target) {
    if (target && target !== document.body && target.isConnected
      && !target.matches?.(':disabled') && !target.closest?.('[inert],[hidden]')) {
      target.focus?.({ preventScroll: true })
      if (document.activeElement === target) return
    }
    document.querySelector('.device-stage:not([hidden]) canvas[data-tracker-display]')?.focus({ preventScroll: true })
  }
  async function restart(){
    const focusTarget = document.activeElement
    await activeRuntimeStore.stop(); canvasGeneration+=1; await tick(); await activeRuntimeStore.start(); await tick()
    restoreRuntimeFocus(focusTarget)
  }
  async function synchronizeRecoveryFocus(state, button) {
    const revision = ++recoveryFocusRevision
    if (state !== 'failed' || !button) return
    await tick()
    if (revision === recoveryFocusRevision && runtime.state === 'failed' && recoveryButton === button) {
      button.focus({ preventScroll: true })
    }
  }
  async function stopRuntime(){ await activeRuntimeStore.stop() }
  async function applySettingsRestart(){ const enabled=settingsStore.snapshot().lowLatencyAudio; const url=new URL(location.href); const active=url.searchParams.get('audio')==='worklet'; if(active!==enabled){enabled?url.searchParams.set('audio','worklet'):url.searchParams.delete('audio');location.assign(url);return} await restart() }
  function selectSection(section){ if (sections.includes(section)) activeSection=section }
  function synchronizeDeveloperMode(preference) {
    developerMode = resolveDeveloperMode(preference)
    if (!developerMode && isDeveloperSection(activeSection)) activeSection = 'Device'
  }
  async function handleViewportChange() {
    const focusTarget = document.activeElement
    compactLayout = nativeHostActive || mobileViewport.matches
    await tick()
    if (focusTarget && focusTarget !== document.body && focusTarget.isConnected) {
      restoreRuntimeFocus(focusTarget)
      return
    }
    const layoutFallback = compactLayout
      ? document.querySelector('.menu-trigger')
      : document.querySelector('.left-nav button[aria-current="page"]')
    if (layoutFallback) {
      layoutFallback.focus({ preventScroll: true })
      return
    }
    restoreRuntimeFocus(document.activeElement)
  }
  async function setDeveloperMode(enabled){
    const preference = Boolean(enabled)
    synchronizeDeveloperMode(preference)
    settingsStore.update({ developerMode: preference })
    await tick()
    document.querySelector('[data-developer-toggle]')?.focus({ preventScroll: true })
  }

  $: sections = visibleSections(developerMode)

  $: synchronizeRecoveryFocus(runtime.state, recoveryButton)

  onMount(()=>{
    const workbenchHandle = Object.freeze({ restart, stop: stopRuntime })
    mobileViewport.addEventListener?.('change', handleViewportChange)
    const workbenchKey = nativeHostActive ? '__nullPeratorWorkbench' : '__picoTrackerWorkbench'
    globalThis[workbenchKey] = workbenchHandle
    const unsubscribe=activeRuntimeStore.subscribe((snapshot)=>{
      runtime=snapshot; document.title='NullPerator'; detachAudio();detachMidi();detachStorage()
      if(snapshot.audio)detachAudio=snapshot.audio.subscribe((next)=>(audio=next));else audio={state:'unavailable',metrics:null,capability:null}
      if(snapshot.midi)detachMidi=snapshot.midi.subscribe((next)=>(midi=next));else midi={state:'unavailable'}
      if(snapshot.storage)detachStorage=snapshot.storage.subscribe((next)=>(storage=next));else storage={state:'unavailable'}
      if(snapshot.trace&&snapshot.trace.snapshot().state!=='capturing')snapshot.trace.setMask(settingsStore.snapshot().traceMask)
    })
    detachSettings=settingsStore.subscribe((next)=>{
      synchronizeDeveloperMode(next.developerMode)
      if(runtime.trace?.snapshot?.().state!=='capturing')runtime.trace?.setMask?.(next.traceMask)
    })
    activeRuntimeStore.start().catch(()=>{})
    return()=>{mobileViewport.removeEventListener?.('change', handleViewportChange);if(globalThis[workbenchKey]===workbenchHandle)delete globalThis[workbenchKey];unsubscribe();detachAudio();detachMidi();detachStorage();detachSettings();activeRuntimeStore.stop().catch(()=>{})}
  })
</script>

<svelte:head><title>NullPerator</title><link rel="icon" href="data:,"/><meta name="description" content="NullPerator music workstation for Web, iOS and hardware"/></svelte:head>

<div class="dashboard" class:native-host={nativeHostActive} class:compact-layout={compactLayout}
  data-developer-mode={developerMode ? 'true' : 'false'} data-layout={compactLayout ? 'compact' : 'desktop'}
  data-runtime-state={runtime.state}>
  {#if !nativeHostActive && !compactLayout}
    <TopBar {runtime} {audio} {storage} {midi} {developerMode}/>
  {:else if !nativeHostActive}
    <MobileNavigation {sections} active={activeSection} {developerMode} onSelect={selectSection}
      developerModeLocked={forceDeveloperMode} onDeveloperModeChange={setDeveloperMode}
      onOpenChange={(open)=>(mobileMenuOpen=open)}/>
  {/if}
  <div class="dashboard-body">
    {#if !nativeHostActive && !compactLayout}<LeftNav {sections} active={activeSection} onSelect={selectSection}/>{/if}
    <main class="workspace" inert={mobileMenuOpen}>
      {#if runtime.state==='failed'}
        <section class="recovery-card" role="alert" data-recovery-kind={runtime.error?.includes('Cross-origin isolation')?'isolation':'runtime'}>
          <p class="eyebrow">Runtime recovery</p><h1>{runtime.error?.includes('Cross-origin isolation')?'Cross-origin isolation is missing':'NullPerator stopped'}</h1><p>{runtime.error}</p>
          {#if runtime.error?.includes('Cross-origin isolation')}<code>Cross-Origin-Opener-Policy: same-origin<br/>Cross-Origin-Embedder-Policy: require-corp</code><button bind:this={recoveryButton} type="button" onclick={()=>location.reload()}>Reload after fixing headers</button>{:else}<button bind:this={recoveryButton} type="button" onclick={()=>restart().catch(()=>{})}>Retry runtime</button>{/if}
        </section>
      {/if}
      <ErrorBoundary label={`${activeSection} panel`}>
        <section class="device-stage" aria-label="NullPerator player" hidden={activeSection!=='Device'}
          inert={runtime.state === 'failed' || runtime.state === 'stopping'}>
          {#key canvasGeneration}<DevicePanel {runtime} {audio} settings={nativeHostActive ? nativeAppSettingsStore : settingsStore} compact={compactLayout} {nativeHostActive}/>{/key}
        </section>
        {#if activeSection==='Files'}<div class="page-panel"><FilesPanel files={runtime.files} storage={runtime.storage} hostFolder={runtime.hostFolder} disabled={runtime.state!=='ready'}/></div>
        {:else if activeSection==='MIDI'}<div class="page-panel"><MidiPanel midi={runtime.midi} disabled={runtime.state!=='ready'} {developerMode}/></div>
        {:else if developerMode && activeSection==='Logs'}<div class="page-panel"><LogsPanel logs={runtime.logs}/></div>
        {:else if developerMode && activeSection==='Trace'}<div class="page-panel"><TracePanel trace={runtime.trace}/></div>
        {:else if activeSection==='Settings'}<div class="page-panel"><SettingsPanel settings={settingsStore} trace={runtime.trace} audio={runtime.audio} runtimeState={runtime.state} buildMetadata={runtime.buildMetadata} {developerMode} developerModeLocked={forceDeveloperMode} onDeveloperModeChange={setDeveloperMode} onRestart={()=>applySettingsRestart().catch(()=>{})}/></div>{/if}
      </ErrorBoundary>
    </main>
    <div class="audio-diagnostics" hidden aria-hidden="true" data-audio-capability={audio.capability?(audio.capability.available?'available':'unavailable'):'unknown'} data-audio-capability-reason={audio.capability?.reason??''} data-audio-worklet-callbacks={audio.metrics?.callbackCount??0} data-audio-underruns={audio.metrics?.underrunFrames??0} data-audio-setup-phase={audio.metrics?.setupPhase??0} data-audio-unlock-main-thread={audio.metrics?.unlockOnBrowserMainThread??0} data-audio-render-micros={audio.metrics?.renderMicros??0} data-audio-callback-micros={audio.metrics?.callbackMicros??0} data-audio-callback-max-micros={audio.metrics?.callbackMaxMicros??0} data-audio-processing-deadline-micros={audio.metrics?.callbackDeadlineMicros??0} data-audio-processing-deadline-misses={audio.metrics?.callbackDeadlineMisses??0}></div>
  </div>
</div>
{#if nativeHostActive}
  <NativeSettingsOverlay {runtime} settings={nativeAppSettingsStore} midi={runtime.midi} midiSnapshot={midi} onReboot={restart}/>
{/if}
