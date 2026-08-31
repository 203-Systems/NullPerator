<script>
  import { onDestroy, tick } from 'svelte'
  import { Close, Settings } from 'carbon-icons-svelte'

  export let onDeveloperModeChange = () => {}
  export let onOpenChange = () => {}
  let open = false
  let trigger
  let settingsDialog
  let closeButton
  let developerButton
  let restoreTarget = null
  let dialogRevision = 0

  function restoreFocus(target) {
    if (target && target !== document.body && target.isConnected
      && !target.matches?.(':disabled') && !target.closest?.('[inert],[hidden]')) {
      target.focus?.({ preventScroll: true })
      if (document.activeElement === target) return
    }
    trigger?.focus({ preventScroll: true })
  }

  async function openSettings() {
    const revision = ++dialogRevision
    restoreTarget = document.activeElement
    open = true
    onOpenChange(true)
    await tick()
    if (revision !== dialogRevision || !open || !settingsDialog) return
    if (!settingsDialog.open) settingsDialog.showModal()
    await tick()
    if (revision === dialogRevision && open && settingsDialog.open) {
      closeButton?.focus({ preventScroll: true })
    }
  }
  async function closeSettings() {
    const revision = ++dialogRevision
    const target = restoreTarget
    restoreTarget = null
    if (settingsDialog?.open) settingsDialog.close()
    open = false
    onOpenChange(false)
    await tick()
    if (revision === dialogRevision && !open) restoreFocus(target)
  }
  function handleDialogKeydown(event) {
    if (event.key !== 'Tab') return
    if (event.shiftKey && document.activeElement === closeButton) {
      event.preventDefault(); developerButton?.focus()
    } else if (!event.shiftKey && document.activeElement === developerButton) {
      event.preventDefault(); closeButton?.focus()
    }
  }
  function handleBackdropClick(event) {
    if (event.target !== settingsDialog) return
    const bounds = settingsDialog.getBoundingClientRect()
    if (event.clientX < bounds.left || event.clientX > bounds.right
      || event.clientY < bounds.top || event.clientY > bounds.bottom) {
      closeSettings()
    }
  }

  onDestroy(() => {
    dialogRevision += 1
    restoreTarget = null
    if (settingsDialog?.open) settingsDialog.close()
    if (open) onOpenChange(false)
  })
</script>

<header class="play-bar" aria-label="Play mode controls">
  <button bind:this={trigger} type="button" class="settings-trigger" aria-label="Settings" aria-expanded={open}
    onclick={openSettings}>
    <Settings size={19}/>
  </button>
</header>

{#if open}
  <dialog bind:this={settingsDialog} class="settings-sheet" aria-labelledby="play-settings-title" tabindex="-1"
    oncancel={(event) => { event.preventDefault(); closeSettings() }} onclick={handleBackdropClick} onkeydown={handleDialogKeydown}>
    <header>
      <div><p>PLAY MODE</p><h2 id="play-settings-title">Settings</h2></div>
      <button bind:this={closeButton} type="button" class="close" aria-label="Close settings" onclick={closeSettings}><Close size={18}/></button>
    </header>
    <button bind:this={developerButton} type="button" class="developer-entry" aria-label="Developer mode"
      onclick={() => onDeveloperModeChange(true)}>
      <span><strong>Developer mode</strong><small>Files, MIDI, logs, trace and device tools</small></span>
      <span class="arrow" aria-hidden="true">›</span>
    </button>
  </dialog>
{/if}

<style>
  .play-bar{display:flex;height:44px;flex:0 0 auto;align-items:center;justify-content:flex-end;padding:0 8px;border-bottom:1px solid var(--border);background:var(--bg-0);z-index:10}
  button{color:var(--muted);background:transparent;cursor:pointer}
  .settings-trigger{display:grid;width:44px;height:44px;padding:0;place-items:center;border:0;border-radius:10px}
  .settings-trigger:hover,.settings-trigger:focus-visible{color:var(--accent);background:var(--accent-soft)}
  .settings-sheet{position:fixed;z-index:31;top:auto;right:auto;left:50%;bottom:max(12px,env(safe-area-inset-bottom));width:min(calc(100% - 24px),380px);max-width:none;margin:0;translate:-50% 0;padding:14px;border:1px solid rgba(255,255,255,.12);border-radius:16px;color:var(--text);background:#15161a;box-shadow:0 18px 60px rgba(0,0,0,.55)}
  .settings-sheet::backdrop{background:rgba(0,0,0,.58)}
  .settings-sheet header{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
  .settings-sheet p{margin:0 0 4px;color:var(--accent);font:600 9px/1 var(--mono);letter-spacing:.14em}
  .settings-sheet h2{margin:0;font-size:17px}
  .close{display:grid;width:44px;height:44px;padding:0;place-items:center;border:1px solid var(--border);border-radius:9px}
  .developer-entry{display:flex;width:100%;min-height:66px;align-items:center;justify-content:space-between;gap:12px;padding:12px;border:1px solid var(--border);border-radius:11px;text-align:left;background:rgba(255,255,255,.025)}
  .developer-entry:hover{border-color:rgba(76,201,240,.35);background:var(--accent-soft)}
  .developer-entry span:first-child{display:grid;gap:4px}.developer-entry strong{color:var(--text);font-size:13px}.developer-entry small{color:var(--muted);font-size:10px;line-height:1.35}
  .arrow{color:var(--accent);font:26px/1 var(--sans)}
</style>
