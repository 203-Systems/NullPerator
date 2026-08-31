<script>
  import { onDestroy, onMount } from 'svelte'
  import VirtualControls from './VirtualControls.svelte'
  import { createInputStore } from '../stores/input.js'

  export let runtime
  export let audio = { state: 'unavailable', error: null }
  export let settings = null
  export let compact = false

  let panel
  let actionMask = 0
  let actionGeneration = 0
  let lastAction = -1
  let heldActions = []
  let displayScale = settings?.snapshot?.().displayScale ?? 'fit'
  let detachSettings = () => {}
  const scaleFor = (value) => compact ? 1 : (value === 'fit' ? 1.4 : Number(value) || 1)
  const input = createInputStore({
    pressAction: (action) => runtime.input?.pressAction(action),
    repeatAction: (action) => runtime.input?.repeatAction(action),
    releaseAction: (action) => runtime.input?.releaseAction(action),
    releaseAllActions: () => runtime.input?.releaseAllActions(),
  })
  const detachInput = input.subscribe((next) => { heldActions = next })

  function focusCanvas() { panel?.querySelector('#picotracker-canvas')?.focus({ preventScroll: true }) }
  function unlockAudio() { runtime.audio?.unlockAudio?.().catch(() => {}) }
  function isTrackerActive(event) {
    if (!panel || panel.getClientRects().length === 0) return false
    const active = document.activeElement
    if (!active) return true
    const tag = active.tagName
    if (active.isContentEditable || tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return false
    // Enter/Space on a focused virtual switch already produces its native
    // button click; do not dispatch the globally mapped action a second time.
    if ((event?.code === 'Enter' || event?.code === 'Space') && active.closest?.('.operator-controls')) return false
    return true
  }
  function refreshActionState() {
    actionMask = runtime.input?.getActionMask?.() ?? 0
    actionGeneration = runtime.input?.getActionGeneration?.() ?? 0
    lastAction = runtime.input?.getLastAction?.() ?? -1
  }
  function diagnosticsEnabled() { return new URLSearchParams(window.location.search).get('inputDiagnostics') === '1' }

  $: if (runtime.state !== 'ready') { input.releaseAll(); actionMask = 0; actionGeneration = 0; lastAction = -1 }

  onMount(() => {
    detachSettings = settings?.subscribe?.((next) => { displayScale = next.displayScale }) ?? (() => {})
    const detach = input.attach({ isActive: isTrackerActive })
    const timer = diagnosticsEnabled() ? window.setInterval(refreshActionState, 16) : null
    if (timer !== null) refreshActionState()
    requestAnimationFrame(focusCanvas)
    return () => { if (timer !== null) window.clearInterval(timer); detach(); detachSettings() }
  })
  onDestroy(() => { detachInput(); input.releaseAll() })
</script>

<div class="device-input-host" class:compact bind:this={panel} onfocusout={(event) => { if (!panel?.contains(event.relatedTarget)) input.releaseAll() }}>
  <h1 class="sr-only">PicoTracker Device</h1>
  <div class="device-scene">
    <div class="operator-device" data-display-scale={displayScale} style={`--device-scale:${scaleFor(displayScale)}`}>
      <div class="operator-screen-housing">
        <div class="screen-bezel">
          <canvas id="canvas" aria-hidden="true" tabindex="-1"></canvas>
          <canvas id="picotracker-canvas" width="240" height="240" tabindex="0" aria-label="PicoTracker display"
            data-frame-content={runtime.frameContent} data-action-mask={actionMask}
            data-action-generation={actionGeneration} data-last-action={lastAction}
            onpointerdown={focusCanvas}></canvas>
          <div class="screen-glass" aria-hidden="true"></div>
        </div>
      </div>
      <VirtualControls {input} {heldActions} disabled={runtime.state !== 'ready'} {compact} />
    </div>
    {#if audio.state === 'locked' || audio.state === 'suspended'}
      <div class="audio-gate">
        <div class="audio-unlock" role="dialog" aria-modal="true" aria-labelledby="audio-unlock-title">
          <p class="eyebrow">Audio</p>
          <h2 id="audio-unlock-title">Enable sound</h2>
          <p>Your browser needs one click before PicoTracker can play audio.</p>
          <button type="button" onclick={unlockAudio}>Enable sound</button>
        </div>
      </div>
    {/if}
  </div>

  {#if !compact}<footer class="keyboard-helper" aria-label="Keyboard shortcuts">
    <div><span class="key-cluster"><kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd></span><span>Move</span></div>
    <div><kbd>J</kbd><span>Option</span></div>
    <div><kbd>K</kbd><span>Edit</span></div>
    <div><kbd>X</kbd><span>Shift</span></div>
    <div><kbd>C</kbd><span>Play</span></div>
  </footer>{/if}
</div>

<style>
  .device-input-host { display:flex; flex-direction:column; width:100%; height:100%; min-height:0; overflow:hidden; }
  .device-scene { position:relative; display:flex; flex:1; min-height:0; align-items:safe center; justify-content:safe center; overflow:auto; padding:24px; background:#0e0f12; }
  .compact .device-scene { overflow:hidden; padding:clamp(6px,2vw,14px); background:var(--bg-0); }
  .operator-device { position:relative; width:320px; flex:0 0 auto; zoom:var(--device-scale,1); }
  .operator-screen-housing { position:relative; padding:0; }
  .screen-bezel { position:relative; width:264px; height:264px; margin:auto; padding:11px; border:1px solid #343841; background:#050608; }
  #picotracker-canvas { display:block; width:240px; height:240px; outline:0; background:#06070a; image-rendering:pixelated; image-rendering:crisp-edges; }
  #picotracker-canvas:focus-visible { box-shadow:0 0 0 1px var(--accent); }
  #canvas { display:none; }
  .screen-glass { position:absolute; inset:11px; pointer-events:none; }
  .audio-gate { position:absolute; inset:0; z-index:4; display:grid; place-items:center; padding:16px; background:rgba(8,9,12,.48); }
  .audio-unlock { width:min(300px,100%); padding:16px; border:1px solid #454a54; border-radius:8px; color:var(--text); background:rgba(18,20,25,.98); box-shadow:0 18px 50px rgba(0,0,0,.48); }
  .audio-unlock .eyebrow { margin:0 0 5px; color:var(--accent); font:600 9px/1 var(--mono); letter-spacing:.14em; text-transform:uppercase; }
  .audio-unlock h2 { margin:0; font-size:15px; }
  .audio-unlock p:not(.eyebrow) { margin:7px 0 13px; color:var(--muted); font-size:11px; line-height:1.45; }
  .audio-unlock button { width:100%; padding:8px 12px; border:1px solid rgba(76,201,240,.55); border-radius:5px; color:#071015; background:var(--accent); font-weight:700; cursor:pointer; }
  .keyboard-helper { display:flex; min-height:52px; align-items:center; justify-content:center; gap:24px; padding:7px 16px; border-top:1px solid var(--border); background:transparent; color:var(--muted); overflow:auto; flex-shrink:0; }
  .keyboard-helper>div { display:flex; align-items:center; gap:7px; white-space:nowrap; font-size:.7rem; }
  .key-cluster { display:flex; align-items:center; gap:3px; }
  kbd { display:grid; min-width:22px; height:22px; padding:0 4px; place-items:center; border:1px solid rgba(255,255,255,.17); border-bottom-color:rgba(255,255,255,.3); border-radius:4px; color:#e7e9ec; background:linear-gradient(#292b31,#191a1e); box-shadow:0 2px 0 #070708; font:600 10px/1 var(--mono); }
  @media(max-height:760px){ .device-scene{align-items:flex-start}.keyboard-helper{gap:14px;padding-inline:10px} }
  @media(max-width:720px){ .device-scene{padding:12px}.device-input-host:not(.compact) .operator-device{zoom:.86!important}.keyboard-helper{justify-content:flex-start}.keyboard-helper>div>span:last-child{display:none} }
  @media(max-width:360px){
    .compact .device-scene{padding-inline:0}
    .device-input-host:not(.compact) .operator-device{zoom:.72!important}
  }
  @media(orientation:landscape) and (max-height:539px){
    .compact .device-scene{padding:6px 12px}
    .compact .operator-device{display:grid;width:544px;height:264px;grid-template-columns:264px 280px;align-items:center}
    .compact :global(.operator-controls){margin:0}
  }
  @media(orientation:landscape) and (max-height:539px) and (max-width:567px){
    .compact .device-scene{padding:6px 0}
    .compact .operator-device{width:480px;height:240px;grid-template-columns:240px 240px}
    .compact .screen-bezel{width:240px;height:240px;padding:0;border:0}
    .compact .screen-glass{inset:0}
  }
  @media(min-height:540px){
    .compact .operator-device{height:min(720px,calc(100dvh - 72px - env(safe-area-inset-bottom)))}
    .compact :global(.operator-controls){position:absolute;left:0;bottom:0;margin:0}
  }
</style>
