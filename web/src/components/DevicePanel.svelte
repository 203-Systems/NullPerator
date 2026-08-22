<script>
  import { onDestroy, onMount } from 'svelte'

  import VirtualControls from './VirtualControls.svelte'
  import { createInputStore } from '../stores/input.js'

  export let runtime

  let panel
  let actionMask = 0
  let actionGeneration = 0
  let lastAction = -1
  const input = createInputStore({
    pressAction: (action) => runtime.input?.pressAction(action),
    releaseAction: (action) => runtime.input?.releaseAction(action),
    releaseAllActions: () => runtime.input?.releaseAllActions(),
  })

  function focusCanvas() {
    panel?.querySelector('#picotracker-canvas')?.focus()
  }

  function isTrackerFocused() {
    return panel?.querySelector('#picotracker-canvas') === document.activeElement
  }

  function refreshActionState() {
    actionMask = runtime.input?.getActionMask?.() ?? 0
    actionGeneration = runtime.input?.getActionGeneration?.() ?? 0
    lastAction = runtime.input?.getLastAction?.() ?? -1
  }

  function diagnosticsEnabled() {
    return new URLSearchParams(window.location.search).get('inputDiagnostics') === '1'
  }

  $: if (runtime.state !== 'ready') {
    input.releaseAll()
    actionMask = 0
    actionGeneration = 0
    lastAction = -1
  }

  onMount(() => {
    const detach = input.attach({ isActive: isTrackerFocused })
    // These values are tracing diagnostics for browser tests and manual debug
    // sessions. Do not add three mutex-taking WASM calls to every production
    // frame just to keep data attributes current.
    const timer = diagnosticsEnabled() ? window.setInterval(refreshActionState, 16) : null
    if (timer !== null) refreshActionState()
    return () => {
      if (timer !== null) window.clearInterval(timer)
      detach()
    }
  })
  onDestroy(() => input.releaseAll())
</script>

<div class="device-input-host" bind:this={panel} onfocusout={(event) => {
  if (!panel?.contains(event.relatedTarget)) input.releaseAll()
}}>
  <div class="device-panel">
    <div class="device-frame">
      <!-- SDL2 2.32 hard-codes #canvas for its passive browser event hooks. -->
      <canvas id="canvas" aria-hidden="true" tabindex="-1"></canvas>
      <canvas
        id="picotracker-canvas"
        width="240"
        height="240"
        tabindex="0"
        aria-label="PicoTracker display"
        data-frame-content={runtime.frameContent}
        data-action-mask={actionMask}
        data-action-generation={actionGeneration}
        data-last-action={lastAction}
        onpointerdown={focusCanvas}
      ></canvas>
    </div>
    <div class="device-caption">
      <span>240 × 240 logical pixels</span>
      <span>{runtime.error ?? (runtime.state === 'ready' ? 'Tracker UI running' : 'Starting tracker UI')}</span>
    </div>
  </div>
  <VirtualControls {input} disabled={runtime.state !== 'ready'} />
</div>
