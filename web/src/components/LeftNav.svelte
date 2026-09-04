<script>
  import { Activity, Book, Dashboard, DataBase, Music, Settings as SettingsGear, Terminal } from 'carbon-icons-svelte'
  import { sectionLabel, WIKI_URL } from '../navigation.js'

  export let sections = []
  export let active = ''
  export let onSelect = () => {}

  const iconMap = { Device: Dashboard, Files: DataBase, MIDI: Music, Logs: Terminal, Trace: Activity, Settings: SettingsGear }
</script>

<nav class="left-nav" aria-label="Main navigation">
  <div class="nav-group">
    {#each sections.filter((section) => section !== 'Settings') as section}
      {#if section === 'Logs'}<span class="group-label developer-label">Developer</span>{/if}
      {@const Icon = iconMap[section] ?? Dashboard}
      <button type="button" class:nav-active={active === section} aria-label={sectionLabel(section)}
        aria-current={active === section ? 'page' : undefined} title={sectionLabel(section)}
        onclick={() => onSelect(section)}>
        <Icon size={20} /><span class="nav-label">{sectionLabel(section)}</span>
      </button>
    {/each}
  </div>
  {#if sections.includes('Settings')}
    <div class="settings-group">
      <a class="nav-link" href={WIKI_URL} target="_blank" rel="noopener noreferrer" aria-label="Wiki" title="Wiki">
        <Book size={20} /><span class="nav-label">Wiki</span>
      </a>
      <button type="button" class:nav-active={active === 'Settings'} aria-label={sectionLabel('Settings')}
        aria-current={active === 'Settings' ? 'page' : undefined} title={sectionLabel('Settings')}
        onclick={() => onSelect('Settings')}>
        <SettingsGear size={20} /><span class="nav-label">{sectionLabel('Settings')}</span>
      </button>
    </div>
  {/if}
</nav>

<style>
  .left-nav { display:flex; flex-direction:column; width:168px; padding:8px 0; overflow:hidden; flex-shrink:0; border-right:1px solid var(--border); background:var(--panel); z-index:8; }
  .nav-group { display:flex; min-height:0; flex:1 1 auto; flex-direction:column; gap:2px; overflow-y:auto; scrollbar-width:none; }
  .nav-group::-webkit-scrollbar { display:none; }
  .settings-group { flex:0 0 auto; margin-top:auto; padding-top:8px; border-top:1px solid var(--border); }
  .group-label { margin:8px 18px 5px; color:var(--muted); opacity:.65; font:600 9px/1 var(--mono); letter-spacing:.12em; text-transform:uppercase; }
  .developer-label { margin-top:15px; padding-top:13px; border-top:1px solid var(--border); }
  button,.nav-link { display:flex; align-items:center; gap:10px; width:100%; min-height:46px; padding:10px 12px 10px 18px; overflow:hidden; border:0; border-left:2px solid transparent; color:var(--muted); background:none; font-size:.82rem; font-weight:500; text-decoration:none; white-space:nowrap; cursor:pointer; transition:color 120ms,background 120ms,border-color 120ms; }
  button:hover,.nav-link:hover { color:var(--text); background:rgba(255,255,255,.03); }
  button.nav-active { color:var(--accent); border-left-color:var(--accent); background:rgba(76,201,240,.06); }
  .nav-label { display:inline-block; overflow:hidden; text-overflow:ellipsis; }
</style>
