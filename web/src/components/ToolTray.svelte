<script>
  import { DataBase, Music, Terminal, Activity, Restart } from 'carbon-icons-svelte'
  import { DEVICE_TOOLS } from '../stores/tools.js'
  export let openTools = []
  export let onToggle = () => {}
  export let onRestart = () => {}
  export let disabled = false
  let expanded = false
  const icons = { Files: DataBase, MIDI: Music, Logs: Terminal, Trace: Activity }
</script>

<aside class="tool-tray" class:expanded aria-label="Tool tray" onmouseenter={() => (expanded=true)} onmouseleave={() => (expanded=false)}>
  {#each DEVICE_TOOLS as tool}
    {@const Icon = icons[tool.id]}
    <button type="button" class:tray-active={openTools.includes(tool.id)} aria-label={`Toggle ${tool.label} tool`} title={tool.label} onclick={() => onToggle(tool.id)}>
      <Icon size={20}/><span>{tool.label}</span>
    </button>
  {/each}
  <div class="tray-footer">
    <button type="button" class="reset" {disabled} title="Reset emulator" onclick={onRestart}><Restart size={20}/><span>Reset</span></button>
  </div>
</aside>

<style>
  .tool-tray { display:flex; flex-direction:column; width:60px; padding:6px 0; gap:1px; flex-shrink:0; overflow:hidden; border-left:1px solid var(--border); background:var(--panel); transition:width .22s cubic-bezier(.2,.8,.2,1); z-index:8; }
  .tool-tray.expanded{width:144px}
  button{display:flex;align-items:center;gap:10px;width:100%;min-height:46px;padding:10px 0 10px 18px;overflow:hidden;border:0;border-right:2px solid transparent;color:var(--muted);background:none;font-size:.82rem;font-weight:500;white-space:nowrap;cursor:pointer;transition:color .12s,background .12s}
  button:hover{color:var(--text);background:rgba(255,255,255,.03)} button.tray-active{color:var(--accent);border-right-color:var(--accent);background:rgba(76,201,240,.06)}
  button span{display:inline-block;max-width:0;overflow:hidden;opacity:0;transform:translateX(-6px);transition:opacity .14s,max-width .24s,transform .22s}.expanded button span{max-width:100px;opacity:1;transform:none;transition-delay:.05s}
  .tray-footer{display:flex;flex-direction:column;margin-top:auto;padding-top:8px;border-top:1px solid rgba(255,255,255,.05)} .reset{color:#ffb48a}.reset:disabled{opacity:.35;cursor:not-allowed}
  @media(max-width:720px){.tool-tray{display:none}}
</style>
