<script>
  import { Dashboard, DataBase, Music, Activity, Settings, Information } from 'carbon-icons-svelte'

  export let sections = []
  export let active = ''
  export let onSelect = () => {}
  let expanded = false

  const iconMap = { Device: Dashboard, Files: DataBase, MIDI: Music, Logs: Activity, Trace: Activity, Settings, About: Information }
</script>

<nav class="left-nav" class:expanded aria-label="Workbench sections"
  onmouseenter={() => (expanded = true)} onmouseleave={() => (expanded = false)}>
  <div class="nav-group">
    {#each sections as section}
      {@const Icon = iconMap[section] ?? Dashboard}
      <button type="button" class:nav-active={active === section} aria-label={section}
        aria-current={active === section ? 'page' : undefined} title={section}
        onclick={() => onSelect(section)}>
        <Icon size={20} /><span class="nav-label">{section}</span>
      </button>
    {/each}
  </div>
</nav>

<style>
  .left-nav { display:flex; flex-direction:column; width:60px; padding:8px 0; overflow:hidden; flex-shrink:0; border-right:1px solid var(--border); background:var(--panel); transition:width .22s cubic-bezier(.2,.8,.2,1); z-index:8; }
  .left-nav.expanded { width:168px; }
  .nav-group { display:flex; flex-direction:column; gap:2px; }
  button { display:flex; align-items:center; gap:10px; width:100%; min-height:46px; padding:10px 0 10px 18px; overflow:hidden; border:0; border-left:2px solid transparent; color:var(--muted); background:none; font-size:.82rem; font-weight:500; white-space:nowrap; cursor:pointer; transition:color .12s,background .12s; }
  button:hover { color:var(--text); background:rgba(255,255,255,.03); }
  button.nav-active { color:var(--accent); border-left-color:var(--accent); background:rgba(76,201,240,.06); }
  .nav-label { display:inline-block; max-width:0; overflow:hidden; opacity:0; transform:translateX(-6px); transition:opacity .14s ease,max-width .24s cubic-bezier(.2,.8,.2,1),transform .22s cubic-bezier(.2,.8,.2,1); }
  .expanded .nav-label { max-width:128px; opacity:1; transform:translateX(0); transition-delay:.05s; }
  @media(max-width:720px){.left-nav,.left-nav.expanded{width:52px}.left-nav{padding-top:5px}.left-nav.expanded .nav-label{max-width:0;opacity:0}.left-nav button{min-height:44px;padding-left:14px}}
  @media(max-height:399px){.left-nav{overflow-x:hidden;overflow-y:auto;scrollbar-width:none}.left-nav::-webkit-scrollbar{display:none}}
</style>
