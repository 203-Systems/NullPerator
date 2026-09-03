<script>
  import { Dashboard, DataBase, Music, Activity, Settings, Information, Terminal } from 'carbon-icons-svelte'
  import { sectionLabel } from '../navigation.js'

  export let sections = []
  export let active = ''
  export let onSelect = () => {}

  const iconMap = { Device: Dashboard, Files: DataBase, MIDI: Music, Logs: Terminal, Trace: Activity, Settings, About: Information }
</script>

<nav class="left-nav" aria-label="Main navigation">
  <div class="nav-group">
    {#each sections as section}
      {#if section === 'Device'}<span class="group-label">Workspace</span>{/if}
      {#if section === 'Logs'}<span class="group-label developer-label">Developer</span>{/if}
      {#if section === 'Settings'}<span class="group-label utility-label">Application</span>{/if}
      {@const Icon = iconMap[section] ?? Dashboard}
      <button type="button" class:nav-active={active === section} aria-label={sectionLabel(section)}
        aria-current={active === section ? 'page' : undefined} title={sectionLabel(section)}
        onclick={() => onSelect(section)}>
        <Icon size={20} /><span class="nav-label">{sectionLabel(section)}</span>
      </button>
    {/each}
  </div>
</nav>

<style>
  .left-nav { display:flex; flex-direction:column; width:168px; padding:10px 8px; overflow:hidden; flex-shrink:0; border-right:1px solid var(--border); background:var(--panel); z-index:8; }
  .nav-group { display:flex; flex-direction:column; gap:2px; }
  .group-label { margin:8px 10px 5px; color:rgba(212,212,216,.36); font:600 9px/1 var(--mono); letter-spacing:.12em; text-transform:uppercase; }
  .group-label:first-child { margin-top:4px; }
  .developer-label,.utility-label { margin-top:15px; padding-top:13px; border-top:1px solid var(--border); }
  button { display:flex; align-items:center; gap:11px; width:100%; min-height:44px; padding:9px 10px; overflow:hidden; border:0; border-radius:7px; color:var(--muted); background:none; font-size:.8rem; font-weight:500; white-space:nowrap; cursor:pointer; transition:color .12s,background .12s; }
  button:hover { color:var(--text); background:rgba(255,255,255,.03); }
  button.nav-active { color:var(--accent); background:rgba(76,201,240,.08); }
  .nav-label { display:inline-block; overflow:hidden; text-overflow:ellipsis; }
  @media(max-height:540px){.left-nav{overflow-x:hidden;overflow-y:auto;scrollbar-width:none}.left-nav::-webkit-scrollbar{display:none}}
</style>
