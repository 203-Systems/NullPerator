<script>
  import { onDestroy, onMount } from 'svelte'
  import VirtualControls from './VirtualControls.svelte'
  import { createInputStore } from '../stores/input.js'

  export let runtime
  export let settings = null

  let panel
  let actionMask = 0
  let actionGeneration = 0
  let lastAction = -1
  let displayScale = settings?.snapshot?.().displayScale ?? 'fit'
  let detachSettings = () => {}
  const scaleFor = (value) => value === 'fit' ? 1 : Number(value) || 1
  const input = createInputStore({
    pressAction: (action) => runtime.input?.pressAction(action),
    releaseAction: (action) => runtime.input?.releaseAction(action),
    releaseAllActions: () => runtime.input?.releaseAllActions(),
  })

  function focusCanvas() { panel?.querySelector('#picotracker-canvas')?.focus() }
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
  onDestroy(() => input.releaseAll())
</script>

<div class="device-input-host" bind:this={panel} onfocusout={(event) => { if (!panel?.contains(event.relatedTarget)) input.releaseAll() }}>
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
      <VirtualControls {input} disabled={runtime.state !== 'ready'} />
      <div class="side-wordmark" aria-hidden="true">OPERATOR</div>
    </div>
    <div class="device-meta">
      <span><b></b>{runtime.error ?? (runtime.state === 'ready' ? 'Firmware live' : 'Booting firmware')}</span>
      <span>240 × 240 · Node target</span>
    </div>
  </div>

  <footer class="keyboard-helper" aria-label="Keyboard shortcuts">
    <div><span class="key-cluster"><kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd></span><span>Move</span></div>
    <div><kbd>J</kbd><span>Enter</span></div>
    <div><kbd>K</kbd><span>Edit</span></div>
    <div><kbd>X</kbd><span>Alt / chord</span></div>
    <div><kbd>C</kbd><span>Tap Play · Hold Nav</span></div>
    <div class="combo"><span class="key-cluster"><kbd>X</kbd><i>+</i><kbd>C</kbd></span><span>X then C: Hold Alt + Play</span></div>
  </footer>
</div>

<style>
  .device-input-host { display:flex; flex-direction:column; width:100%; height:100%; min-height:0; overflow:hidden; }
  .device-scene { position:relative; display:flex; flex:1; min-height:0; align-items:center; justify-content:center; overflow:auto; padding:26px 24px 18px; background:radial-gradient(circle at 50% 38%,rgba(76,201,240,.055),transparent 34%),linear-gradient(135deg,#111216,#0c0d10); }
  .device-scene::before { content:''; position:absolute; width:460px; height:120px; bottom:10%; border-radius:50%; background:rgba(0,0,0,.52); filter:blur(35px); transform:perspective(400px) rotateX(65deg); }
  .operator-device { position:relative; width:300px; flex:0 0 auto; filter:drop-shadow(0 28px 38px rgba(0,0,0,.58)); transform:perspective(1000px) rotateX(1.2deg); zoom:var(--device-scale,1); }
  .operator-screen-housing { position:relative; padding:18px 22px 17px; border:1px solid #515259; border-radius:16px 16px 3px 3px; background:linear-gradient(138deg,#efefed 0,#9b9da2 3%,#2d2e33 7%,#111216 92%,#4d4e52 100%); box-shadow:inset 0 2px 1px rgba(255,255,255,.45),inset 0 -10px 18px #050506; z-index:2; }
  .screen-bezel { position:relative; width:256px; height:256px; padding:8px; border:1px solid #32343a; background:#050608; box-shadow:inset 0 0 0 2px #111318,0 3px 8px #000; }
  #picotracker-canvas { display:block; width:240px; height:240px; outline:0; background:#06070a; image-rendering:pixelated; image-rendering:crisp-edges; }
  #picotracker-canvas:focus-visible { box-shadow:0 0 0 1px var(--accent),0 0 14px rgba(76,201,240,.25); }
  #canvas { display:none; }
  .screen-glass { position:absolute; inset:8px; pointer-events:none; background:linear-gradient(145deg,rgba(255,255,255,.075),transparent 22% 72%,rgba(76,201,240,.025)); box-shadow:inset 0 0 24px rgba(0,0,0,.45); }
  .side-wordmark { position:absolute; right:-14px; top:286px; color:rgba(255,255,255,.12); font:600 10px/1 var(--mono); letter-spacing:.38em; writing-mode:vertical-rl; z-index:3; }
  .device-meta { position:absolute; left:18px; bottom:13px; display:flex; flex-direction:column; gap:5px; color:var(--muted); font:500 10px/1.3 var(--mono); }
  .device-meta span:first-child { color:#b9bdc4; } .device-meta b { display:inline-block; width:6px; height:6px; margin-right:6px; border-radius:50%; background:#3dd68c; box-shadow:0 0 6px rgba(61,214,140,.55); }
  .keyboard-helper { display:flex; min-height:52px; align-items:center; justify-content:center; gap:24px; padding:7px 16px; border-top:1px solid var(--border); background:var(--panel); color:var(--muted); overflow:auto; flex-shrink:0; }
  .keyboard-helper>div { display:flex; align-items:center; gap:7px; white-space:nowrap; font-size:.7rem; }
  .key-cluster { display:flex; align-items:center; gap:3px; }
  kbd { display:grid; min-width:22px; height:22px; padding:0 4px; place-items:center; border:1px solid rgba(255,255,255,.17); border-bottom-color:rgba(255,255,255,.3); border-radius:4px; color:#e7e9ec; background:linear-gradient(#292b31,#191a1e); box-shadow:0 2px 0 #070708; font:600 10px/1 var(--mono); }
  i { font:normal 9px/1 var(--mono); color:rgba(255,255,255,.25); }
  @media(max-height:760px){ .device-scene{align-items:flex-start}.keyboard-helper{gap:14px;padding-inline:10px} }
  @media(max-width:720px){ .device-scene{padding:12px}.operator-device{transform:scale(.82);margin:-45px}.device-meta{display:none}.keyboard-helper{justify-content:flex-start}.keyboard-helper>div>span:last-child{display:none} }
</style>
