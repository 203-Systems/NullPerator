<script>
  import { onMount, tick } from 'svelte'
  import DevicePanel from './components/DevicePanel.svelte'
  import TopBar from './components/TopBar.svelte'
  import MobilePlayBar from './components/MobilePlayBar.svelte'
  import LeftNav from './components/LeftNav.svelte'
  import ToolTray from './components/ToolTray.svelte'
  import ToolPanelStack from './components/ToolPanelStack.svelte'
  import ErrorBoundary from './components/ErrorBoundary.svelte'
  import SettingsPanel from './components/SettingsPanel.svelte'
  import AboutPanel from './components/AboutPanel.svelte'
  import FilesPanel from './components/FilesPanel.svelte'
  import MidiPanel from './components/MidiPanel.svelte'
  import LogsPanel from './components/LogsPanel.svelte'
  import TracePanel from './components/TracePanel.svelte'
  import { toggleTool } from './stores/tools.js'
  import { runtimeStore } from './stores/runtime.js'
  import { settingsStore } from './stores/settings.js'

  const sections = ['Device', 'Files', 'MIDI', 'Logs', 'Trace', 'Settings', 'About']
  const query = new URLSearchParams(window.location.search)
  const forceDeveloperMode = query.get('dev') === '1' || query.get('views-test') === '1' || query.get('inputDiagnostics') === '1'
  const mobileViewport = window.matchMedia('(max-width: 720px), (orientation: landscape) and (max-width: 960px) and (max-height: 539px)')
  const resolveDeveloperMode = (preference) => forceDeveloperMode || (preference === 'auto'
    ? !mobileViewport.matches
    : Boolean(preference))
  let developerPreference = settingsStore.snapshot().developerMode
  let activeSection = 'Device'
  let openTools = []
  let developerMode = resolveDeveloperMode(developerPreference)
  let mobileSettingsOpen = false
  let runtime = runtimeStore.getSnapshot()
  let audio = runtime.audio?.snapshot?.() ?? { state:'unavailable', metrics:null, capability:null }
  let midi = runtime.midi?.snapshot?.() ?? { state:'unavailable' }
  let storage = runtime.storage?.snapshot?.() ?? { state:'unavailable' }
  let canvasGeneration = 0
  let detachAudio=()=>{}, detachMidi=()=>{}, detachStorage=()=>{}, detachSettings=()=>{}

  async function restart(){ await runtimeStore.stop(); canvasGeneration+=1; await tick(); await runtimeStore.start() }
  async function stopRuntime(){ await runtimeStore.stop() }
  async function applySettingsRestart(){ const enabled=settingsStore.snapshot().lowLatencyAudio; const url=new URL(location.href); const active=url.searchParams.get('audio')==='worklet'; if(active!==enabled){enabled?url.searchParams.set('audio','worklet'):url.searchParams.delete('audio');location.assign(url);return} await restart() }
  function selectSection(section){ activeSection=section }
  function toggleDock(tool){ openTools=toggleTool(openTools,tool) }
  function synchronizeDeveloperMode(preference = developerPreference) {
    developerPreference = preference
    developerMode = resolveDeveloperMode(preference)
    if (developerMode) mobileSettingsOpen = false
  }
  function setDeveloperMode(enabled){
    synchronizeDeveloperMode(Boolean(enabled))
    mobileSettingsOpen = false
    settingsStore.update({ developerMode })
    if (!developerMode) { activeSection='Device'; openTools=[] }
  }

  onMount(()=>{
    const workbenchHandle = Object.freeze({ restart, stop: stopRuntime })
    const handleViewportChange = () => synchronizeDeveloperMode()
    mobileViewport.addEventListener?.('change', handleViewportChange)
    globalThis.__picoTrackerWorkbench = workbenchHandle
    const unsubscribe=runtimeStore.subscribe((snapshot)=>{
      runtime=snapshot; detachAudio();detachMidi();detachStorage()
      if(snapshot.audio)detachAudio=snapshot.audio.subscribe((next)=>(audio=next));else audio={state:'unavailable',metrics:null,capability:null}
      if(snapshot.midi)detachMidi=snapshot.midi.subscribe((next)=>(midi=next));else midi={state:'unavailable'}
      if(snapshot.storage)detachStorage=snapshot.storage.subscribe((next)=>(storage=next));else storage={state:'unavailable'}
      if(snapshot.trace&&snapshot.trace.snapshot().state!=='capturing')snapshot.trace.setMask(settingsStore.snapshot().traceMask)
    })
    detachSettings=settingsStore.subscribe((next)=>{
      synchronizeDeveloperMode(next.developerMode)
      if(runtime.trace?.snapshot?.().state!=='capturing')runtime.trace?.setMask?.(next.traceMask)
    })
    runtimeStore.start().catch(()=>{})
    return()=>{mobileViewport.removeEventListener?.('change', handleViewportChange);if(globalThis.__picoTrackerWorkbench===workbenchHandle)delete globalThis.__picoTrackerWorkbench;unsubscribe();detachAudio();detachMidi();detachStorage();detachSettings();runtimeStore.stop().catch(()=>{})}
  })
</script>

<svelte:head><title>NullPerator</title><link rel="icon" href="data:,"/><meta name="description" content="PicoTracker WebAssembly development and performance workbench"/></svelte:head>

<div class="dashboard" data-developer-mode={developerMode ? 'true' : 'false'}>
  {#if developerMode}
    <TopBar {runtime} {audio} {storage} {midi} {developerMode} onDeveloperModeChange={setDeveloperMode}/>
  {:else}
    <MobilePlayBar onDeveloperModeChange={setDeveloperMode} onOpenChange={(open)=>(mobileSettingsOpen=open)}/>
  {/if}
  <div class="dashboard-body">
    {#if developerMode}<LeftNav {sections} active={activeSection} onSelect={selectSection}/>{/if}
    <main class="workspace" inert={mobileSettingsOpen}>
      {#if runtime.state==='failed'}
        <section class="recovery-card" role="alert" data-recovery-kind={runtime.error?.includes('Cross-origin isolation')?'isolation':'runtime'}>
          <p class="eyebrow">Runtime recovery</p><h1>{runtime.error?.includes('Cross-origin isolation')?'Cross-origin isolation is missing':'PicoTracker runtime stopped'}</h1><p>{runtime.error}</p>
          {#if runtime.error?.includes('Cross-origin isolation')}<code>Cross-Origin-Opener-Policy: same-origin<br/>Cross-Origin-Embedder-Policy: require-corp</code><button type="button" onclick={()=>location.reload()}>Reload after fixing headers</button>{:else}<button type="button" onclick={()=>restart().catch(()=>{})}>Retry runtime</button>{/if}
        </section>
      {/if}
      <ErrorBoundary label={`${activeSection} panel`}>
        <section class="device-stage" aria-label="Operator simulator" hidden={developerMode && activeSection!=='Device'}>
          {#key canvasGeneration}<DevicePanel {runtime} {audio} settings={settingsStore} compact={!developerMode}/>{/key}
        </section>
        {#if developerMode && activeSection==='Files'}<div class="page-panel"><FilesPanel files={runtime.files} storage={runtime.storage} hostFolder={runtime.hostFolder} disabled={runtime.state!=='ready'}/></div>
        {:else if developerMode && activeSection==='MIDI'}<div class="page-panel"><MidiPanel midi={runtime.midi} disabled={runtime.state!=='ready'}/></div>
        {:else if developerMode && activeSection==='Logs'}<div class="page-panel"><LogsPanel logs={runtime.logs}/></div>
        {:else if developerMode && activeSection==='Trace'}<div class="page-panel"><TracePanel trace={runtime.trace}/></div>
        {:else if developerMode && activeSection==='Settings'}<div class="page-panel"><SettingsPanel settings={settingsStore} trace={runtime.trace} audio={runtime.audio} runtimeState={runtime.state} onRestart={()=>applySettingsRestart().catch(()=>{})}/></div>
        {:else if developerMode && activeSection==='About'}<div class="page-panel"><AboutPanel buildMetadata={runtime.buildMetadata}/></div>{/if}
      </ErrorBoundary>
    </main>
    {#if developerMode && activeSection==='Device'}
      <ToolPanelStack {openTools} {runtime} onClose={(tool)=>toggleDock(tool)}/>
      <ToolTray {openTools} onToggle={toggleDock} onRestart={()=>restart().catch(()=>{})} disabled={runtime.state!=='ready'}/>
    {/if}
    <div class="audio-diagnostics" hidden aria-hidden="true" data-audio-capability={audio.capability?(audio.capability.available?'available':'unavailable'):'unknown'} data-audio-capability-reason={audio.capability?.reason??''} data-audio-worklet-callbacks={audio.metrics?.callbackCount??0} data-audio-underruns={audio.metrics?.underrunFrames??0} data-audio-setup-phase={audio.metrics?.setupPhase??0} data-audio-unlock-main-thread={audio.metrics?.unlockOnBrowserMainThread??0} data-audio-render-micros={audio.metrics?.renderMicros??0} data-audio-callback-micros={audio.metrics?.callbackMicros??0} data-audio-callback-max-micros={audio.metrics?.callbackMaxMicros??0} data-audio-processing-deadline-micros={audio.metrics?.callbackDeadlineMicros??0} data-audio-processing-deadline-misses={audio.metrics?.callbackDeadlineMisses??0}></div>
  </div>
</div>
