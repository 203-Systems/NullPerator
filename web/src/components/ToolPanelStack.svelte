<script>
  import { onDestroy } from 'svelte'
  import { Close } from 'carbon-icons-svelte'
  import FilesPanel from './FilesPanel.svelte'
  import MidiPanel from './MidiPanel.svelte'
  import LogsPanel from './LogsPanel.svelte'
  import TracePanel from './TracePanel.svelte'
  export let openTools = []
  export let runtime
  export let onClose = () => {}
  let panelWidth = 380
  let resizing = false
  let detachResize = () => {}
  function startResize(event){
    event.preventDefault(); detachResize(); resizing=true
    const move=(e)=>{ panelWidth=Math.max(300,Math.min(720,(window.innerWidth-60-e.clientX)/Math.max(1,openTools.length))) }
    const up=()=>{resizing=false;detachResize()}
    detachResize=()=>{window.removeEventListener('pointermove',move);window.removeEventListener('pointerup',up);window.removeEventListener('pointercancel',up);window.removeEventListener('blur',up);detachResize=()=>{}}
    window.addEventListener('pointermove',move);window.addEventListener('pointerup',up);window.addEventListener('pointercancel',up);window.addEventListener('blur',up)
  }
  onDestroy(()=>detachResize())
</script>

{#if openTools.length}
  <aside class="panel-stack" class:resizing style:width={`${panelWidth * openTools.length}px`} aria-label="Open tools">
    <button class="resize" aria-label="Resize tools" title="Resize tools" onpointerdown={startResize}></button>
    <div class="panel-grid" style:grid-template-columns={`repeat(${openTools.length}, ${panelWidth}px)`}>
      {#each openTools as tool (tool)}
        <section class="panel-slot" aria-label={`${tool} tool panel`}>
          <header><span>{tool}</span><button type="button" aria-label={`Close ${tool} tool`} onclick={() => onClose(tool)}><Close size={16}/></button></header>
          <div class="panel-body">
            {#if tool === 'Files'}
              <FilesPanel files={runtime.files} storage={runtime.storage} hostFolder={runtime.hostFolder} disabled={runtime.state !== 'ready'} />
            {:else if tool === 'MIDI'}
              <MidiPanel midi={runtime.midi} disabled={runtime.state !== 'ready'} />
            {:else if tool === 'Logs'}
              <LogsPanel logs={runtime.logs} />
            {:else if tool === 'Trace'}
              <TracePanel trace={runtime.trace} />
            {/if}
          </div>
        </section>
      {/each}
    </div>
  </aside>
{/if}

<style>
  .panel-stack{position:relative;display:flex;flex-shrink:0;max-width:calc(100vw - 540px);min-width:300px;overflow-x:auto;overflow-y:hidden;border-left:1px solid var(--border);background:var(--bg-1)} .resizing{user-select:none}
  .resize{position:absolute;inset:0 auto 0 0;width:6px;padding:0;border:0;background:transparent;cursor:col-resize;z-index:3}.resize::after{content:'';position:absolute;left:2px;inset-block:0;width:1px;background:rgba(255,255,255,.06)}.resize:hover::after,.resizing .resize::after{background:var(--accent)}
  .panel-grid{display:grid;width:max-content;min-width:100%}.panel-slot{display:flex;flex-direction:column;min-width:0;min-height:0;overflow:hidden;border-right:1px solid var(--border)}
  header{display:flex;align-items:center;justify-content:space-between;min-height:36px;padding:6px 10px;background:var(--panel);border-bottom:1px solid var(--border);flex-shrink:0}header span{color:var(--muted);font-size:.72rem;font-weight:600;letter-spacing:.08em;text-transform:uppercase}header button{display:grid;width:22px;height:22px;padding:0;place-items:center;border:1px solid var(--border);border-radius:4px;color:var(--muted);background:none;cursor:pointer}header button:hover{color:var(--text);border-color:var(--accent)}
  .panel-body{flex:1;min-height:0;overflow:auto;padding:14px}
  @media(max-width:920px){.panel-stack{position:absolute;right:60px;inset-block:50px 52px;max-width:calc(100vw - 120px);z-index:7;box-shadow:-20px 0 40px rgba(0,0,0,.4)}}
</style>
