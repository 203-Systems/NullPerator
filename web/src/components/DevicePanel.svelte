<script>
  import { onDestroy, onMount, tick } from 'svelte'
  import VirtualControls from './VirtualControls.svelte'
  import { createInputStore } from '../stores/input.js'

  export let runtime
  export let audio = { state: 'unavailable', error: null }
  export let settings = null
  export let compact = false
  export let nativeHostActive = false

  let panel
  let scene
  let unlockDialog
  let unlockButton
  let audioRestoreTarget = null
  let audioPromptRevision = 0
  let actionMask = 0
  let actionGeneration = 0
  let lastAction = -1
  let heldActions = []
  let displayScale = nativeHostActive ? '1.5' : (settings?.snapshot?.().displayScale ?? 'fit')
  let fitScale = 1.4
  let hideControlsWithGamepad = settings?.snapshot?.().hideControlsWithGamepad ?? false
  let controllerConnected = false
  let detachSettings = () => {}
  const scaleFor = (value, compactMode, availableScale) => compactMode ? 1 : (value === 'fit' ? availableScale : Number(value) || 1)
  const input = createInputStore({
    pressAction: (action) => runtime.input?.pressAction(action),
    repeatAction: (action) => runtime.input?.repeatAction(action),
    releaseAction: (action) => runtime.input?.releaseAction(action),
    releaseAllActions: () => runtime.input?.releaseAllActions(),
  })
  const detachInput = input.subscribe((next) => { heldActions = next })

  function focusCanvas() { panel?.querySelector('canvas[data-tracker-display]')?.focus({ preventScroll: true }) }
  function updateFitScale() {
    if (!scene || compact) return
    const style = getComputedStyle(scene)
    const innerWidth = scene.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight)
    const innerHeight = scene.clientHeight - parseFloat(style.paddingTop) - parseFloat(style.paddingBottom)
    // The Device page stays mounted while another workspace page is active.
    // Ignore the transient zero-size ResizeObserver sample from `hidden` so
    // returning to the tracker never flashes at the minimum fit scale.
    if (innerWidth <= 0 || innerHeight <= 0) return
    const next = Math.max(.75, Math.min(1.4, innerWidth / 320, innerHeight / 496))
    fitScale = Math.floor(next * 1000) / 1000
  }
  async function resetModeScroll(compactMode, target) {
    if (!target) return
    await tick()
    if (compact !== compactMode || scene !== target) return
    updateFitScale()
    target.scrollTop = 0
    target.scrollLeft = 0
  }
  async function unlockAudio() {
    try { await runtime.audio?.unlockAudio?.() } catch {}
  }
  function restoreAudioPromptFocus(target) {
    if (target && target !== document.body && target.isConnected
      && !target.matches?.(':disabled') && !target.closest?.('[inert],[hidden]')) {
      target.focus?.({ preventScroll: true })
      if (document.activeElement === target) return
    }
    focusCanvas()
  }
  async function synchronizeAudioPrompt(blocked, target, button) {
    const revision = ++audioPromptRevision
    if (blocked && target) {
      if (!target.open) {
        audioRestoreTarget = document.activeElement
        target.showModal()
      }
      await tick()
      if (revision === audioPromptRevision && audioBlocked && unlockDialog === target && target.open) {
        button?.focus({ preventScroll: true })
      }
      return
    }
    if (blocked) return
    const restoreTarget = audioRestoreTarget
    audioRestoreTarget = null
    if (target?.open) target.close()
    if (!restoreTarget) return
    await tick()
    if (revision === audioPromptRevision && !audioBlocked) restoreAudioPromptFocus(restoreTarget)
  }
  function trapAudioPromptFocus(event) {
    if (event.key !== 'Tab') return
    event.preventDefault()
    unlockButton?.focus({ preventScroll: true })
  }
  // Chromium can move focus to the document when a modal backdrop is clicked.
  function containAudioPromptFocus(event) {
    if (unlockDialog?.contains(event.relatedTarget)) return
    queueMicrotask(() => {
      if (!audioBlocked || !unlockDialog?.open
        || unlockDialog.contains(document.activeElement)) return
      unlockButton?.focus({ preventScroll: true })
    })
  }

  function isTrackerActive(event) {
    if (runtime.state !== 'ready') return false
    if (audioBlocked) return false
    if (!panel || panel.getClientRects().length === 0) return false
    // DevicePanel remains mounted behind compact modal navigation. `inert`
    // prevents focus and pointer input, but our window-level keyboard mapping
    // must explicitly honor it as well.
    if (panel.closest('[inert]')) return false
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

  function postNative(command, payload = {}) {
    return globalThis.webkit?.messageHandlers?.nullPeratorNative?.postMessage({ command, ...payload })
  }
  function decodeBase64(value) {
    const binary = atob(String(value ?? ''))
    return Uint8Array.from(binary, (character) => character.charCodeAt(0))
  }
  function applyNativeBattery(state = globalThis.__nullPeratorNativeBattery) {
    if (!nativeHostActive || !state || !runtime.battery) return
    runtime.battery.setState(state)
  }
  function attachRawGamepadFaceButtons() {
    if (!nativeHostActive || typeof navigator.getGamepads !== 'function') return () => {}
    const bindings = [
      { index: 0, action: 'enter' },
      { index: 1, action: 'option' },
    ]
    const held = new Map(bindings.map(({ index }) => [index, false]))
    let frame = 0
    let attached = true
    const poll = () => {
      if (!attached) return
      const pads = Array.from(navigator.getGamepads() ?? []).filter(Boolean)
      for (const { index, action } of bindings) {
        const pressed = document.visibilityState === 'visible'
          && pads.some((pad) => pad.connected !== false && Boolean(pad.buttons?.[index]?.pressed))
        if (pressed === held.get(index)) continue
        held.set(index, pressed)
        const source = `gamepad:b${index}`
        if (pressed) input.press(action, source)
        else input.release(action, source)
      }
      frame = requestAnimationFrame(poll)
    }
    frame = requestAnimationFrame(poll)
    return () => {
      attached = false
      cancelAnimationFrame(frame)
      for (const { index, action } of bindings) input.release(action, `gamepad:b${index}`)
    }
  }
  function attachNativeFramePump() {
    if (!nativeHostActive) return () => {}
    const canvas = panel?.querySelector('#nullperator-canvas')
    const context = canvas?.getContext('2d', { alpha: false })
    if (!canvas || !context) return () => {}
    const width = 240
    const height = 240
    const image = context.createImageData(width, height)
    let palette = new Uint8Array(256 * 3)
    let sequence = 0
    let frame = 0
    let attached = true
    let inFlight = false
    let reportedError = false

    const applyPacket = (packet) => {
      if (!packet || packet.version !== 1 || packet.changed !== true) return
      if (packet.width !== width || packet.height !== height) throw new Error('Unexpected native frame size')
      const nextPalette = decodeBase64(packet.palette)
      if (nextPalette.byteLength !== palette.byteLength) throw new Error('Invalid native frame palette')
      palette = nextPalette
      for (const region of Array.isArray(packet.regions) ? packet.regions : []) {
        const x = Number(region.x)
        const y = Number(region.y)
        const regionWidth = Number(region.width)
        const regionHeight = Number(region.height)
        if (![x, y, regionWidth, regionHeight].every(Number.isInteger)
          || x < 0 || y < 0 || regionWidth <= 0 || regionHeight <= 0
          || x + regionWidth > width || y + regionHeight > height) {
          throw new Error('Invalid native frame region')
        }
        const indices = decodeBase64(region.indices)
        if (indices.byteLength !== regionWidth * regionHeight) throw new Error('Invalid native frame pixels')
        for (let row = 0; row < regionHeight; row += 1) {
          for (let column = 0; column < regionWidth; column += 1) {
            const color = indices[row * regionWidth + column] * 3
            const pixel = ((y + row) * width + x + column) * 4
            image.data[pixel] = palette[color]
            image.data[pixel + 1] = palette[color + 1]
            image.data[pixel + 2] = palette[color + 2]
            image.data[pixel + 3] = 255
          }
        }
        context.putImageData(image, 0, 0, x, y, regionWidth, regionHeight)
      }
      sequence = Number(packet.sequence) >>> 0
      canvas.dataset.nativeFrameSequence = String(sequence)
      reportedError = false
    }
    const poll = () => {
      if (!attached) return
      frame = requestAnimationFrame(poll)
      if (inFlight || document.visibilityState !== 'visible') return
      inFlight = true
      void Promise.resolve(postNative('nativeFrame', { after: sequence }))
        .then(applyPacket)
        .catch((error) => {
          if (!reportedError) console.error('[NativeCore] frame failed', error)
          reportedError = true
        })
        .finally(() => { inFlight = false })
    }
    frame = requestAnimationFrame(poll)
    return () => { attached = false; cancelAnimationFrame(frame) }
  }

  $: audioBlocked = !nativeHostActive && (audio.state === 'locked' || audio.state === 'suspended')
  $: deviceScale = scaleFor(displayScale, compact, fitScale)
  $: if (runtime.state !== 'ready' || audioBlocked) { input.releaseAll(); actionMask = 0; actionGeneration = 0; lastAction = -1 }
  $: synchronizeAudioPrompt(audioBlocked, unlockDialog, unlockButton)
  $: resetModeScroll(compact, scene)
  $: if (nativeHostActive && runtime.battery) applyNativeBattery()

  onMount(() => {
    const resizeObserver = typeof ResizeObserver === 'function' ? new ResizeObserver(updateFitScale) : null
    resizeObserver?.observe(scene)
    updateFitScale()
    detachSettings = settings?.subscribe?.((next) => {
      displayScale = nativeHostActive ? '1.5' : next.displayScale
      hideControlsWithGamepad = Boolean(next.hideControlsWithGamepad)
    }) ?? (() => {})
    controllerConnected = Boolean(globalThis.__nullPeratorControllerState?.connected)
    const controllerChanged = (event) => { controllerConnected = Boolean(event.detail?.connected) }
    globalThis.addEventListener('nullperator-controller-change', controllerChanged)
    const detach = input.attach({ isActive: isTrackerActive })
    const detachRawGamepad = attachRawGamepadFaceButtons()
    const detachNativeFrames = attachNativeFramePump()
    const timer = diagnosticsEnabled() ? window.setInterval(refreshActionState, 16) : null
    if (timer !== null) refreshActionState()
    requestAnimationFrame(focusCanvas)
    const nativeBridge = nativeHostActive ? Object.freeze({
      press: (action) => input.press(action, `native:${action}`),
      release: (action) => input.release(action, `native:${action}`),
      releaseAll: () => input.releaseAll(),
      setBattery: (percentage, charging, available = true) => {
        const state = Object.freeze({ percentage, charging, available })
        globalThis.__nullPeratorNativeBattery = state
        applyNativeBattery(state)
      },
    }) : null
    if (nativeBridge) globalThis.__nullPeratorHost = nativeBridge
    return () => {
      if (nativeBridge && globalThis.__nullPeratorHost === nativeBridge) delete globalThis.__nullPeratorHost
      globalThis.removeEventListener('nullperator-controller-change', controllerChanged)
      if (timer !== null) window.clearInterval(timer)
      resizeObserver?.disconnect(); detachNativeFrames(); detachRawGamepad(); detach(); detachSettings()
    }
  })
  onDestroy(() => {
    audioPromptRevision += 1
    const restoreTarget = audioRestoreTarget
    audioRestoreTarget = null
    if (unlockDialog?.open) unlockDialog.close()
    if (restoreTarget) queueMicrotask(() => restoreAudioPromptFocus(restoreTarget))
    detachInput()
    input.releaseAll()
  })
</script>

<div class="device-input-host" class:compact class:native-host={nativeHostActive} bind:this={panel}
  role={nativeHostActive ? 'application' : undefined} aria-label={nativeHostActive ? 'NullPerator' : undefined}
  onfocusout={(event) => { if (!panel?.contains(event.relatedTarget)) input.releaseAll() }}>
  <h1 class="sr-only">NullPerator Player</h1>
  <div class="device-scene" bind:this={scene}>
    <div class="operator-device" inert={audioBlocked} data-display-scale={displayScale} style={`--device-scale:${deviceScale}`}>
      <div class="operator-screen-housing">
        <div class="screen-bezel">
          <canvas id="canvas" aria-hidden="true" tabindex="-1"></canvas>
          <canvas id={nativeHostActive ? 'nullperator-canvas' : 'picotracker-canvas'} data-tracker-display width="240" height="240" tabindex="0" aria-label="NullPerator display"
            data-frame-content={runtime.frameContent} data-action-mask={actionMask}
            data-action-generation={actionGeneration} data-last-action={lastAction}
            onpointerdown={focusCanvas}></canvas>
          <div class="screen-glass" aria-hidden="true"></div>
        </div>
      </div>
      {#if !(nativeHostActive && hideControlsWithGamepad && controllerConnected)}
        <VirtualControls {input} {heldActions} disabled={runtime.state !== 'ready'} compact={nativeHostActive ? false : compact} {nativeHostActive} />
      {/if}
    </div>
    {#if !nativeHostActive}<dialog bind:this={unlockDialog} class="audio-gate audio-unlock" aria-labelledby="audio-unlock-title"
      oncancel={(event) => event.preventDefault()} onkeydown={trapAudioPromptFocus}
      onfocusout={containAudioPromptFocus}>
      <p class="eyebrow">Audio</p>
      <h2 id="audio-unlock-title">Enable sound</h2>
      <p>Your browser needs one click before NullPerator can play audio.</p>
      <button bind:this={unlockButton} type="button" onclick={unlockAudio}>Enable sound</button>
    </dialog>{/if}
  </div>

  {#if !compact}<footer class="keyboard-helper" aria-label="Keyboard shortcuts">
    <div><span class="key-cluster"><kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd></span><span>Move</span></div>
    <div><kbd>J</kbd><span>Option</span></div>
    <div><kbd>K</kbd><span>Enter</span></div>
    <div><kbd>C</kbd><span>Play</span></div>
    <div><kbd>X</kbd><span>Shift</span></div>
  </footer>{/if}
</div>

<style>
  .device-input-host { display:flex; flex-direction:column; width:100%; height:100%; min-height:0; overflow:hidden; }
  .device-scene { position:relative; display:flex; flex:1; min-height:0; align-items:safe center; justify-content:safe center; overflow:auto; padding:24px; background:var(--bg-1); }
  .compact .device-scene { overflow:hidden; padding:clamp(6px,2vw,14px); background:var(--bg-0); }
  .native-host .device-scene { align-items:flex-start; overflow:hidden; padding:max(38px,calc(env(safe-area-inset-top) + 28px)) 0 max(8px,env(safe-area-inset-bottom)); background:#000; }
  /* Preserve the former fixed "Large" iOS geometry. The native shell owns
     viewport scaling, so these values should not be derived from web prefs. */
  .native-host .operator-device { --native-screen-size:min(89.64vw,475.2px); display:flex; width:100vw; height:100%; min-height:0; flex-direction:column; zoom:1!important; }
  .native-host .operator-screen-housing { flex:0 0 auto; }
  .native-host .screen-bezel { width:var(--native-screen-size); height:var(--native-screen-size); padding:0; border:1px solid #383838; background:#050505; }
  .native-host canvas[data-tracker-display] { width:100%; height:100%; background:#050505; }
  .native-host canvas[data-tracker-display]:focus-visible { box-shadow:none; }
  .native-host .screen-glass { inset:0; }
  .operator-device { position:relative; width:320px; flex:0 0 auto; zoom:var(--device-scale,1); }
  .operator-screen-housing { position:relative; padding:0; }
  .screen-bezel { position:relative; width:264px; height:264px; margin:auto; padding:11px; border:1px solid #343841; background:#050608; }
  canvas[data-tracker-display] { display:block; width:240px; height:240px; outline:0; background:#06070a; image-rendering:pixelated; image-rendering:crisp-edges; }
  canvas[data-tracker-display]:focus-visible { box-shadow:0 0 0 1px var(--accent); }
  #canvas { display:none; }
  .screen-glass { position:absolute; inset:11px; pointer-events:none; }
  .audio-gate { position:fixed; inset:0; box-sizing:border-box; width:min(300px,calc(100% - 32px)); height:max-content; max-height:calc(100dvh - 32px); margin:auto; overflow:auto; }
  .audio-gate::backdrop { background:rgba(8,9,12,.72); }
  .audio-unlock { padding:16px; border:1px solid var(--border-strong); border-radius:var(--radius-overlay); color:var(--text); background:var(--panel); box-shadow:0 18px 48px rgba(0,0,0,.45); }
  .audio-unlock .eyebrow { margin:0 0 5px; color:var(--accent); font:600 9px/1 var(--mono); letter-spacing:.14em; text-transform:uppercase; }
  .audio-unlock h2 { margin:0; font-size:15px; }
  .audio-unlock p:not(.eyebrow) { margin:7px 0 13px; color:var(--muted); font-size:11px; line-height:1.45; }
  .audio-unlock button { width:100%; min-height:44px; padding:0 12px; border:1px solid var(--accent-border); border-radius:var(--radius-control); color:var(--text-accent); background:var(--accent-fill); font-weight:700; cursor:pointer; }
  .audio-unlock button:hover { border-color:rgba(76,201,240,.72); background:rgba(76,201,240,.18); }
  .keyboard-helper { display:flex; min-height:52px; align-items:center; justify-content:center; gap:24px; padding:7px 16px; border-top:1px solid var(--border); background:var(--panel); color:var(--muted); overflow:auto; flex-shrink:0; }
  .keyboard-helper>div { display:flex; align-items:center; gap:7px; white-space:nowrap; font-size:.7rem; }
  .key-cluster { display:flex; align-items:center; gap:3px; }
  kbd { display:grid; min-width:22px; height:22px; padding:0 4px; place-items:center; border:1px solid var(--border-strong); border-radius:var(--radius-tight); color:var(--text); background:var(--bg-2); font:600 10px/1 var(--mono); }
  @media(max-height:760px){ .device-scene{align-items:flex-start}.keyboard-helper{gap:14px;padding-inline:10px} }
  @media(max-width:720px){ .device-scene{padding:12px}.device-input-host:not(.compact) .operator-device{zoom:.86!important}.keyboard-helper{justify-content:flex-start}.keyboard-helper>div>span:last-child{display:none} }
  @media(max-width:360px){
    .compact .device-scene{padding-inline:0}
    .device-input-host:not(.compact) .operator-device{zoom:.72!important}
  }
  @media(orientation:portrait) and (max-height:539px){
    .compact .device-scene{padding:6px 0}
    .compact .operator-device{width:280px}
    .compact .screen-bezel{width:240px;height:240px;padding:0;border:0}
    .compact .screen-glass{inset:0}
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
  @media(min-height:540px) and (orientation:portrait){
    .compact.native-host .operator-device{height:100%;min-height:0}
    .compact.native-host :global(.operator-controls){position:relative;left:auto;bottom:auto;margin-top:12px}
  }
  @media(max-width:499px) and (orientation:portrait){
    .native-host .device-scene {
      padding-top:max(100px,calc(env(safe-area-inset-top) + 28px));
      padding-bottom:max(42px,env(safe-area-inset-bottom));
    }
  }
  @media(orientation:landscape){
    .native-host .device-scene {
      align-items:center;
      padding:max(8px,env(safe-area-inset-top)) max(12px,env(safe-area-inset-right)) max(8px,env(safe-area-inset-bottom)) max(12px,env(safe-area-inset-left));
    }
    .native-host .operator-device {
      --native-screen-size:min(90.72dvh,54vw,calc(100dvh - 16px),620px);
      --landscape-control-gap:clamp(12px,1.8vw,24px);
      display:grid;
      grid-template-columns:minmax(0,1fr) var(--native-screen-size) minmax(0,1fr);
      grid-template-rows:var(--native-screen-size);
      width:min(100%,1240px);
      height:var(--native-screen-size);
      align-items:stretch;
      column-gap:var(--landscape-control-gap);
    }
    .native-host .operator-screen-housing { grid-column:2; grid-row:1; }
    .native-host :global(.operator-controls) { grid-column:1 / -1; grid-row:1; }
  }
  @media(min-width:500px) and (orientation:portrait){
    .native-host .operator-device { --native-screen-size:min(88.56vw,777.6px); }
  }
</style>
