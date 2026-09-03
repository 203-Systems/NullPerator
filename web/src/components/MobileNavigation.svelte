<script>
  import { onDestroy, tick } from 'svelte'
  import {
    Activity,
    Close,
    Dashboard,
    DataBase,
    Information,
    Menu,
    Music,
    Settings,
    Terminal,
  } from 'carbon-icons-svelte'

  import { DEVELOPER_SECTIONS, USER_SECTIONS, sectionLabel } from '../navigation.js'

  export let sections = []
  export let active = 'Device'
  export let developerMode = false
  export let developerModeLocked = false
  export let onSelect = () => {}
  export let onDeveloperModeChange = () => {}
  export let onOpenChange = () => {}

  const icons = {
    Device: Dashboard,
    Files: DataBase,
    MIDI: Music,
    Settings,
    About: Information,
    Logs: Terminal,
    Trace: Activity,
  }
  const descriptions = {
    Device: 'Play and edit the tracker',
    Files: 'Projects, samples and backups',
    MIDI: 'Connect input and output devices',
    Settings: 'Display and audio preferences',
    About: 'Version, privacy and licenses',
    Logs: 'Runtime messages',
    Trace: 'Performance capture',
  }

  let open = false
  let trigger
  let dialog
  let closeButton
  let restoreTarget = null
  let revision = 0

  function restoreFocus(target) {
    if (target && target !== document.body && target.isConnected
      && !target.matches?.(':disabled') && !target.closest?.('[inert],[hidden]')) {
      target.focus?.({ preventScroll: true })
      if (document.activeElement === target) return
    }
    trigger?.focus({ preventScroll: true })
  }

  async function openMenu() {
    const currentRevision = ++revision
    restoreTarget = document.activeElement
    open = true
    onOpenChange(true)
    await tick()
    if (currentRevision !== revision || !open || !dialog) return
    if (!dialog.open) dialog.showModal()
    await tick()
    if (currentRevision === revision && dialog.open) closeButton?.focus({ preventScroll: true })
  }

  async function closeMenu() {
    const currentRevision = ++revision
    const target = restoreTarget
    restoreTarget = null
    if (dialog?.open) dialog.close()
    open = false
    onOpenChange(false)
    await tick()
    if (currentRevision === revision) restoreFocus(target)
  }

  async function choose(section) {
    onSelect(section)
    await closeMenu()
  }

  function handleBackdrop(event) {
    if (event.target === dialog) closeMenu()
  }

  function trapFocus(event) {
    if (event.key !== 'Tab' || !dialog) return
    const controls = [...dialog.querySelectorAll('button:not(:disabled)')]
    if (!controls.length) return
    const first = controls[0]
    const last = controls.at(-1)
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault(); last.focus()
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault(); first.focus()
    }
  }

  onDestroy(() => {
    revision += 1
    restoreTarget = null
    if (dialog?.open) dialog.close()
    if (open) onOpenChange(false)
  })
</script>

<header class="mobile-bar" aria-label="NullPerator navigation">
  <div class="brand">
    <img src="/203dark.svg" alt="203 Systems" />
    <span>NullPerator</span>
  </div>
  <span class="current-section">{sectionLabel(active)}</span>
  <button bind:this={trigger} type="button" class="menu-trigger" aria-label="Open menu"
    aria-expanded={open} onclick={openMenu}>
    <Menu size={21}/>
  </button>
</header>

{#if open}
  <dialog bind:this={dialog} class="menu-sheet" aria-labelledby="mobile-menu-title" tabindex="-1"
    oncancel={(event) => { event.preventDefault(); closeMenu() }} onclick={handleBackdrop} onkeydown={trapFocus}>
    <div class="sheet-surface">
      <header>
        <div><p>NULLPERATOR</p><h2 id="mobile-menu-title">Menu</h2></div>
        <button bind:this={closeButton} type="button" class="close" aria-label="Close menu" onclick={closeMenu}><Close size={19}/></button>
      </header>

      <p class="group-label">Workspace</p>
      <nav aria-label="Mobile workspace sections">
        {#each USER_SECTIONS as section}
          {@const Icon = icons[section]}
          <button type="button" class:active={active === section} aria-current={active === section ? 'page' : undefined}
            aria-label={sectionLabel(section)} title={sectionLabel(section)} onclick={() => choose(section)}>
            <Icon size={20}/>
            <span><strong>{sectionLabel(section)}</strong><small>{descriptions[section]}</small></span>
          </button>
        {/each}
      </nav>

      <div class="developer-row">
        <span><strong>Developer tools</strong><small>{developerModeLocked ? 'Enabled by this diagnostic URL' : 'Add logs and performance trace'}</small></span>
        <button type="button" class="toggle" class:on={developerMode} data-developer-toggle
          aria-label="Developer tools" aria-pressed={developerMode} disabled={developerModeLocked}
          onclick={() => onDeveloperModeChange(!developerMode)}><span></span></button>
      </div>

      {#if developerMode}
        <p class="group-label diagnostics-label">Diagnostics</p>
        <nav class="diagnostics" aria-label="Developer sections">
          {#each DEVELOPER_SECTIONS.filter((section) => sections.includes(section)) as section}
            {@const Icon = icons[section]}
            <button type="button" class:active={active === section} aria-current={active === section ? 'page' : undefined}
              aria-label={sectionLabel(section)} title={sectionLabel(section)} onclick={() => choose(section)}>
              <Icon size={20}/>
              <span><strong>{section}</strong><small>{descriptions[section]}</small></span>
            </button>
          {/each}
        </nav>
      {/if}
    </div>
  </dialog>
{/if}

<style>
  .mobile-bar { display:grid; height:48px; flex:0 0 auto; grid-template-columns:minmax(0,1fr) auto 44px; align-items:center; gap:8px; padding:0 6px 0 12px; border-bottom:1px solid var(--border); background:var(--panel); z-index:10; }
  .brand { display:flex; min-width:0; align-items:center; gap:8px; }
  .brand img { display:block; width:40px; height:18px; object-fit:contain; }
  .brand span { overflow:hidden; font-size:.82rem; font-weight:650; letter-spacing:.025em; text-overflow:ellipsis; white-space:nowrap; }
  .current-section { color:var(--muted); font:600 9px/1 var(--mono); letter-spacing:.1em; text-transform:uppercase; white-space:nowrap; }
  button { color:var(--text); background:transparent; cursor:pointer; }
  .menu-trigger,.close { display:grid; width:44px; height:44px; padding:0; place-items:center; border:0; border-radius:8px; color:var(--muted); }
  .menu-trigger:hover,.menu-trigger:focus-visible,.close:hover,.close:focus-visible { color:var(--accent); background:var(--accent-soft); }
  .menu-sheet { position:fixed; z-index:31; inset:0; width:100%; max-width:none; height:100%; max-height:none; margin:0; padding:0; border:0; color:var(--text); background:transparent; }
  .menu-sheet::backdrop { background:rgba(0,0,0,.66); backdrop-filter:blur(4px); }
  .sheet-surface { position:absolute; right:0; bottom:0; left:0; max-height:min(720px,calc(100dvh - 12px)); overflow:auto; padding:14px 14px max(16px,env(safe-area-inset-bottom)); border:1px solid rgba(255,255,255,.12); border-bottom:0; border-radius:16px 16px 0 0; background:#111216; box-shadow:0 -18px 60px rgba(0,0,0,.58); overscroll-behavior:contain; }
  .sheet-surface>header { display:flex; align-items:center; justify-content:space-between; margin-bottom:12px; }
  .sheet-surface header p,.group-label { margin:0 0 4px; color:var(--accent); font:600 9px/1 var(--mono); letter-spacing:.14em; text-transform:uppercase; }
  .sheet-surface h2 { margin:0; font-size:18px; }
  .sheet-surface nav { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:7px; }
  .sheet-surface nav>button { display:grid; min-height:62px; grid-template-columns:24px minmax(0,1fr); align-items:center; gap:9px; padding:10px; border:1px solid var(--border); border-radius:9px; text-align:left; background:rgba(255,255,255,.02); }
  .sheet-surface nav>button:hover,.sheet-surface nav>button:focus-visible { border-color:rgba(76,201,240,.38); background:var(--accent-soft); }
  .sheet-surface nav>button.active { color:var(--accent); border-color:rgba(76,201,240,.35); background:var(--accent-soft); }
  nav button>span,.developer-row>span { display:grid; min-width:0; gap:3px; }
  nav strong,.developer-row strong { font-size:12px; }
  nav small,.developer-row small { overflow:hidden; color:var(--muted); font-size:9px; line-height:1.35; text-overflow:ellipsis; }
  .developer-row { display:flex; min-height:62px; align-items:center; justify-content:space-between; gap:14px; margin-top:14px; padding:10px 12px; border-top:1px solid var(--border); border-bottom:1px solid var(--border); }
  .toggle { position:relative; width:44px; height:44px; flex:0 0 auto; padding:0; border:0; border-radius:10px; background:transparent; }
  .toggle::before { content:""; position:absolute; inset:10px 1px; border:1px solid rgba(255,255,255,.18); border-radius:999px; background:#090a0c; }
  .toggle span { position:absolute; left:5px; top:14px; width:16px; height:16px; border-radius:50%; background:#737984; transition:translate .16s ease,background .16s ease; }
  .toggle.on::before { border-color:rgba(76,201,240,.45); }
  .toggle.on span { translate:18px 0; background:var(--accent); }
  .toggle:disabled { cursor:not-allowed; opacity:.65; }
  .diagnostics-label { margin-top:14px; }
  .sheet-surface nav.diagnostics { grid-template-columns:repeat(2,minmax(0,1fr)); }
  @media(max-width:359px){
    .brand span { display:none; }
    .sheet-surface nav { grid-template-columns:1fr; }
  }
  @media(orientation:landscape) and (max-height:539px){
    .sheet-surface { top:10px; right:10px; bottom:10px; left:auto; width:min(520px,calc(100vw - 20px)); max-height:none; border-bottom:1px solid rgba(255,255,255,.12); border-radius:14px; }
    .sheet-surface nav { grid-template-columns:repeat(3,minmax(0,1fr)); }
    .sheet-surface nav.diagnostics { grid-template-columns:repeat(2,minmax(0,1fr)); }
  }
</style>
