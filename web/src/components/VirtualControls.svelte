<script>
  export let input
  export let disabled = false
  export let heldActions = []
  export let compact = false
  export let nativeHostActive = false

  const pointerClickSuppression = new WeakSet()
  const source = (event) => `pointer:${event.pointerId}`

  function press(event, action) {
    event.preventDefault()
    try { event.currentTarget.setPointerCapture?.(event.pointerId) } catch {}
    input?.press(action, source(event))
  }
  function release(event, action, suppressClick = false) {
    if (suppressClick) pointerClickSuppression.add(event.currentTarget)
    input?.release(action, source(event))
  }
  function cancel(event, action) { event.preventDefault(); input?.release(action, source(event)) }
  function activate(event, action) {
    const keyboardActivation = event.detail === 0
    if (!keyboardActivation && pointerClickSuppression.delete(event.currentTarget)) { event.preventDefault(); return }
    if (keyboardActivation) pointerClickSuppression.delete(event.currentTarget)
    event.preventDefault()
    const clickSource = `virtual-click:${action}`
    input?.press(action, clickSource)
    if (nativeHostActive) setTimeout(() => input?.release(action, clickSource), 90)
    else input?.release(action, clickSource)
  }

</script>

<div class="operator-controls" class:compact class:native-host={nativeHostActive}
  role="group"
  aria-label="NullPerator virtual controls"
  oncontextmenu={(event) => event.preventDefault()} ondragstart={(event) => event.preventDefault()}>
  <div class="d-pad" aria-label="Directional controls">
    {#each [['up','Up','W','▲'],['left','Left','A','◀'],['down','Down','S','▼'],['right','Right','D','▶']] as [action,label,key,glyph]}
      {#if nativeHostActive}
        <button type="button" class="simple-direction {action}" class:pressed={heldActions.includes(action)} aria-label={label} aria-pressed={heldActions.includes(action)} data-action={action} {disabled}
          onpointerdown={(event) => press(event, action)} onpointerup={(event) => release(event, action, true)}
          onpointercancel={(event) => cancel(event, action)} onlostpointercapture={(event) => release(event, action)} onclick={(event) => activate(event, action)}><span>{glyph}</span></button>
      {:else}
        <button type="button" class={action} class:pressed={heldActions.includes(action)} aria-label={label} aria-pressed={heldActions.includes(action)} data-action={action} {disabled}
          onpointerdown={(event) => press(event, action)} onpointerup={(event) => release(event, action, true)}
          onpointercancel={(event) => cancel(event, action)} onlostpointercapture={(event) => release(event, action)} onclick={(event) => activate(event, action)}>
          <span class="switch"><span>{glyph}</span></span><kbd>{key}</kbd>
        </button>
      {/if}
    {/each}
  </div>

  <div class="face-buttons">
    <button type="button" class="face enter" class:pressed={heldActions.includes('enter')} aria-label="ENTER" aria-pressed={heldActions.includes('enter')} data-action="enter" {disabled}
      onpointerdown={(e)=>press(e,'enter')} onpointerup={(e)=>release(e,'enter',true)} onpointercancel={(e)=>cancel(e,'enter')} onlostpointercapture={(e)=>release(e,'enter')} onclick={(e)=>activate(e,'enter')}>
      {#if nativeHostActive}<span class="native-label">ENTER</span>{:else}<span class="switch"><span>↵</span></span><kbd>K</kbd><em>ENTER</em>{/if}
    </button>
    <button type="button" class="face option" class:pressed={heldActions.includes('option')} aria-label="OPTION" aria-pressed={heldActions.includes('option')} data-action="option" {disabled}
      onpointerdown={(e)=>press(e,'option')} onpointerup={(e)=>release(e,'option',true)} onpointercancel={(e)=>cancel(e,'option')} onlostpointercapture={(e)=>release(e,'option')} onclick={(e)=>activate(e,'option')}>
      {#if nativeHostActive}<span class="native-label">OPTION</span>{:else}<span class="switch"><span>✦</span></span><kbd>J</kbd><em>OPTION</em>{/if}
    </button>
  </div>

  <div class="bottom-buttons">
    {#if nativeHostActive}
    <button type="button" class:pressed={heldActions.includes('play')} aria-label="PLAY" aria-pressed={heldActions.includes('play')} data-action="play" {disabled}
      onpointerdown={(e)=>press(e,'play')} onpointerup={(e)=>release(e,'play',true)} onpointercancel={(e)=>cancel(e,'play')} onlostpointercapture={(e)=>release(e,'play')} onclick={(e)=>activate(e,'play')}>
      <span class="native-label">PLAY</span>
    </button>
    <button type="button" class:pressed={heldActions.includes('shift')} aria-label="SHIFT" aria-pressed={heldActions.includes('shift')} data-action="shift" {disabled}
      onpointerdown={(e)=>press(e,'shift')} onpointerup={(e)=>release(e,'shift',true)} onpointercancel={(e)=>cancel(e,'shift')} onlostpointercapture={(e)=>release(e,'shift')} onclick={(e)=>activate(e,'shift')}>
      <span class="native-label">SHIFT</span>
    </button>
    {:else}
    <button type="button" class:pressed={heldActions.includes('play')} aria-label="PLAY" aria-pressed={heldActions.includes('play')} data-action="play" {disabled}
      onpointerdown={(e)=>press(e,'play')} onpointerup={(e)=>release(e,'play',true)} onpointercancel={(e)=>cancel(e,'play')} onlostpointercapture={(e)=>release(e,'play')} onclick={(e)=>activate(e,'play')}>
      <span class="switch"><span>▶</span></span><kbd>C</kbd><em>PLAY</em>
    </button>
    <button type="button" class:pressed={heldActions.includes('shift')} aria-label="SHIFT" aria-pressed={heldActions.includes('shift')} data-action="shift" {disabled}
      onpointerdown={(e)=>press(e,'shift')} onpointerup={(e)=>release(e,'shift',true)} onpointercancel={(e)=>cancel(e,'shift')} onlostpointercapture={(e)=>release(e,'shift')} onclick={(e)=>activate(e,'shift')}>
      <span class="switch"></span><kbd>X</kbd><em>SHIFT</em>
    </button>
    {/if}
  </div>
</div>

<style>
  .operator-controls { position:relative; width:100%; height:208px; margin-top:12px; overflow:visible; touch-action:none; user-select:none; }
  button { position:absolute; width:48px; height:48px; padding:0; border:1px solid var(--border-strong); border-radius:var(--radius-control); color:var(--text); background:var(--bg-2); cursor:pointer; touch-action:none; transition:color 120ms,border-color 120ms,background 120ms,transform 80ms; }
  button:hover:not(:disabled) { border-color:var(--accent-border); background:var(--accent-soft); }
  button:disabled { cursor:not-allowed; opacity:.45; }
  .d-pad button,.face { transform:rotate(45deg); }
  .d-pad button:active,.d-pad button.pressed,.face:active,.face.pressed { transform:rotate(45deg) translate(1px,1px); border-color:var(--accent); background:var(--panel); box-shadow:inset 0 0 0 1px var(--accent-fill-strong); }
  .switch { position:absolute; left:50%; top:27px; display:grid; width:18px; height:14px; translate:-50% 0; place-items:center; color:var(--muted); background:transparent; }
  .switch::before { display:none; }
  .switch > span { color:var(--muted); font:700 9px/1 var(--mono); }
  kbd { position:absolute; left:50%; top:8px; display:grid; min-width:18px; height:14px; translate:-50% 0; place-items:center; border:0; color:var(--accent); background:transparent; font:600 11px/1 var(--mono); }
  em { position:absolute; left:50%; bottom:8px; translate:-50% 0; color:var(--muted); font:500 7px/1 var(--mono); letter-spacing:.07em; font-style:normal; }
  .d-pad .switch,.d-pad kbd,.face kbd,.face em { rotate:-45deg; }
  .face .switch { display:none; }
  .d-pad { position:absolute; left:23px; top:20px; width:142px; height:126px; }
  .d-pad .up{left:47px;top:0}.d-pad .left{left:8px;top:39px}.d-pad .down{left:47px;top:78px}.d-pad .right{left:86px;top:39px}
  .face-buttons { position:absolute; right:12px; top:28px; width:126px; height:126px; }
  .face.enter{left:67px;top:0}.face.option{left:28px;top:39px}
  .bottom-buttons { position:absolute; left:107px; bottom:0; width:106px; height:48px; }
  .bottom-buttons .switch { display:none; }
  .bottom-buttons button:active,.bottom-buttons button.pressed { transform:translateY(1px); border-color:var(--accent); background:var(--panel); box-shadow:inset 0 0 0 1px var(--accent-fill-strong); }
  .bottom-buttons button:first-child{left:0}.bottom-buttons button:last-child{right:0}
  .operator-controls.compact kbd { display:none; }
  .operator-controls.compact { height:166px; margin-top:10px; }
  .compact button,.compact .d-pad button,.compact .face { width:52px; height:52px; border-radius:14px; transform:none; }
  .compact button:active,.compact button.pressed,.compact .d-pad button:active,.compact .d-pad button.pressed,.compact .face:active,.compact .face.pressed { transform:translateY(1px); }
  .compact .d-pad { left:0; top:4px; width:174px; height:156px; }
  .compact .d-pad .up{left:61px;top:0}.compact .d-pad .left{left:7px;top:52px}.compact .d-pad .down{left:61px;top:104px}.compact .d-pad .right{left:115px;top:52px}
  .compact .d-pad .switch { top:18px; }
  .compact .d-pad .switch,.compact .d-pad kbd,.compact .face kbd,.compact .face em { rotate:0deg; }
  .compact .d-pad .switch > span { font-size:13px; }
  .compact .face-buttons { right:0; top:4px; width:120px; height:120px; }
  .compact .face.enter{left:64px;top:0}.compact .face.option{left:4px;top:0}
  .compact .face .switch { display:grid; top:11px; }
  .compact .face .switch > span { font-size:12px; }
  .compact .bottom-buttons { left:auto; right:0; top:64px; bottom:auto; width:120px; height:52px; }
  .compact .bottom-buttons button:first-child{left:4px}.compact .bottom-buttons button:last-child{right:4px}
  .compact .bottom-buttons em { top:21px; bottom:auto; }
  .compact .face.enter,.compact .bottom-buttons button:last-child { border-color:var(--accent-border-soft); }
  .operator-controls.native-host {
    --control-slot-edge:clamp(56px,14vw,62px);
    --control-edge:calc(var(--control-slot-edge) + 6px);
    /* Grow each diamond toward the centre while preserving the old outer
       diamond footprint: 2*step + key*sqrt(2) stays constant. */
    --control-gap:calc(var(--control-slot-edge) - 2.25px);
    --face-edge:clamp(76px,19vw,82px);
    --bottom-w:clamp(116px,29vw,128px);
    --bottom-h:62px;
    --bottom-gap:22px;
    --upper-controls-top:clamp(58px,7.5dvh,74px);
    flex:1 1 auto;
    display:grid;
    grid-template-columns:1fr 1fr;
    grid-template-rows:minmax(0,1fr) var(--bottom-h);
    column-gap:18px;
    width:min(100%,430px);
    height:auto;
    min-height:0;
    margin:12px auto 0;
    padding:0 18px;
    overflow:hidden;
  }
  .operator-controls.native-host button { appearance:none; -webkit-appearance:none; -webkit-tap-highlight-color:transparent; -webkit-touch-callout:none; outline:none; }
  .operator-controls.native-host .d-pad {
    position:relative; inset:auto; grid-column:1; grid-row:1; align-self:start; justify-self:center;
    width:calc(var(--control-gap) * 2 + var(--control-edge)); height:calc(var(--control-gap) * 2 + var(--control-edge)); margin-top:var(--upper-controls-top);
  }
  .operator-controls.native-host .d-pad .simple-direction {
    position:absolute; width:var(--control-edge); height:var(--control-edge); padding:0;
    border:1px solid #686868; border-radius:5px; color:#e8e8e8;
    background:linear-gradient(145deg,#181818,#080808);
    box-shadow:inset 0 0 0 3px #070707,inset 0 0 0 4px #303030,0 2px 6px rgba(0,0,0,.75);
    transform:rotate(45deg); transform-origin:50% 50%; font:600 17px/1 system-ui,sans-serif;
  }
  .operator-controls.native-host .d-pad .simple-direction>span { position:absolute; inset:0; display:grid; place-items:center; transform:rotate(-45deg); pointer-events:none; }
  .operator-controls.native-host .d-pad .simple-direction:active,
  .operator-controls.native-host .d-pad .simple-direction.pressed { border-color:#49d6e6; color:#49d6e6; background:#171717; transform:rotate(45deg) scale(.96); }
  .operator-controls.native-host .d-pad .up { left:var(--control-gap); top:0; }
  .operator-controls.native-host .d-pad .left { left:0; top:var(--control-gap); }
  .operator-controls.native-host .d-pad .right { left:calc(var(--control-gap) * 2); top:var(--control-gap); }
  .operator-controls.native-host .d-pad .down { left:var(--control-gap); top:calc(var(--control-gap) * 2); }
  .operator-controls.native-host .face-buttons {
    position:relative; inset:auto; grid-column:2; grid-row:1; display:grid; grid-template-columns:repeat(2,var(--face-edge)); gap:12px;
    align-self:start; justify-self:center; width:auto; height:auto;
    margin-top:calc(var(--upper-controls-top) + var(--control-gap) + (var(--control-edge) - var(--face-edge)) / 2);
  }
  .operator-controls.native-host .face,
  .operator-controls.native-host .bottom-buttons button {
    position:relative; inset:auto; display:grid; place-items:center; padding:0; border:1px solid #686868; border-radius:6px;
    color:#f2f2f2; background:linear-gradient(145deg,#181818,#080808);
    box-shadow:inset 0 0 0 3px #070707,inset 0 0 0 4px #303030,0 2px 6px rgba(0,0,0,.75); transform:none;
  }
  .operator-controls.native-host .face { width:var(--face-edge); height:var(--face-edge); }
  .operator-controls.native-host .face.option { grid-column:1; grid-row:1; }
  .operator-controls.native-host .face.enter { grid-column:2; grid-row:1; }
  .operator-controls.native-host .face:active,.operator-controls.native-host .face.pressed,
  .operator-controls.native-host .bottom-buttons button:active,.operator-controls.native-host .bottom-buttons button.pressed { border-color:#49d6e6; background:#171717; transform:scale(.97); }
  .operator-controls.native-host .native-label { display:grid; width:100%; height:100%; place-items:center; color:#f1f1f1; font:500 11px/1 ui-monospace,SFMono-Regular,Menlo,monospace; letter-spacing:.025em; pointer-events:none; }
  .operator-controls.native-host .bottom-buttons {
    position:relative; inset:auto; grid-column:1 / -1; grid-row:2; display:grid; grid-template-columns:repeat(2,var(--bottom-w)); gap:var(--bottom-gap);
    justify-content:center; align-self:end; width:auto; height:var(--bottom-h); margin-left:0;
  }
  .operator-controls.native-host .bottom-buttons button { width:var(--bottom-w); height:var(--bottom-h); }
  @media(orientation:portrait){
    .operator-controls.native-host { align-self:center; }
  }
  @media(max-width:499px) and (orientation:portrait){
    .operator-controls.native-host {
      --control-edge:var(--control-slot-edge);
      --control-gap:calc(var(--control-slot-edge) + 2px);
      --upper-controls-top:58px;
    }
    .operator-controls.native-host .face-buttons {
      margin-top:calc(var(--upper-controls-top) + var(--control-gap) + (var(--control-edge) - var(--face-edge)) / 2 - .25px);
      margin-left:12px;
    }
  }
  @media(min-width:500px) and (orientation:portrait){
    .operator-controls.native-host { --control-slot-edge:66px; --face-edge:88px; --bottom-w:142px; --bottom-h:68px; --bottom-gap:30px; --upper-controls-top:clamp(64px,7.5dvh,82px); width:min(100%,720px); padding-inline:42px; }
    .operator-controls.native-host .face-buttons { gap:18px; }
  }
  @media(orientation:landscape){
    .operator-controls.native-host {
      --control-slot-edge:clamp(48px,6.8vw,64px); --face-edge:clamp(66px,9vw,86px);
      --bottom-w:clamp(96px,12vw,116px); --bottom-h:56px; --bottom-gap:clamp(14px,2vw,24px);
      grid-template-columns:minmax(0,1fr) var(--native-screen-size) minmax(0,1fr); grid-template-rows:minmax(0,1fr) var(--bottom-h);
      column-gap:var(--landscape-control-gap); row-gap:12px; width:100%; height:var(--native-screen-size); margin:0; padding:0; overflow:visible; pointer-events:none;
    }
    .operator-controls.native-host button { pointer-events:auto; }
    .operator-controls.native-host .d-pad { grid-column:1; align-self:center; margin-top:0; }
    .operator-controls.native-host .face-buttons { grid-column:3; align-self:center; gap:clamp(10px,1.5vw,18px); margin:0; }
    .operator-controls.native-host .bottom-buttons { display:contents; }
    .operator-controls.native-host .bottom-buttons button:first-child { grid-column:1; grid-row:2; align-self:end; justify-self:center; }
    .operator-controls.native-host .bottom-buttons button:last-child { grid-column:3; grid-row:2; align-self:end; justify-self:center; }
  }
  @media(orientation:portrait) and (max-height:539px){
    .operator-controls.compact{width:280px;height:156px;margin-top:5px}
    .compact button,.compact .d-pad button,.compact .face{width:48px;height:48px;border-radius:12px}
    .compact .d-pad{left:0;top:4px;width:148px;height:144px}
    .compact .d-pad .up{left:50px;top:0}.compact .d-pad .left{left:0;top:48px}.compact .d-pad .down{left:50px;top:96px}.compact .d-pad .right{left:100px;top:48px}
    .compact .face-buttons{right:0;top:4px;width:116px;height:116px}
    .compact .face.enter{left:60px;top:0}.compact .face.option{left:0;top:0}
    .compact .bottom-buttons{right:0;top:60px;width:116px;height:48px}
    .compact .bottom-buttons button:first-child{left:0}.compact .bottom-buttons button:last-child{right:0}
  }
  @media(orientation:landscape) and (max-height:539px){
    .operator-controls.compact{width:280px;height:156px}
    .compact button,.compact .d-pad button,.compact .face{width:48px;height:48px;border-radius:12px}
    .compact .d-pad{left:0;top:4px;width:148px;height:144px}
    .compact .d-pad .up{left:50px;top:0}.compact .d-pad .left{left:0;top:48px}.compact .d-pad .down{left:50px;top:96px}.compact .d-pad .right{left:100px;top:48px}
    .compact .face-buttons{right:0;top:4px;width:116px;height:116px}
    .compact .face.enter{left:60px;top:0}.compact .face.option{left:0;top:0}
    .compact .bottom-buttons{right:0;top:60px;width:116px;height:48px}
    .compact .bottom-buttons button:first-child{left:0}.compact .bottom-buttons button:last-child{right:0}
  }
  @media(orientation:landscape) and (max-height:539px) and (max-width:567px){
    .operator-controls.compact{width:240px;height:136px}
    .compact button,.compact .d-pad button,.compact .face{width:44px;height:44px;border-radius:11px}
    .compact .d-pad{left:0;top:2px;width:132px;height:132px}
    .compact .d-pad .up{left:44px;top:0}.compact .d-pad .left{left:0;top:44px}.compact .d-pad .down{left:44px;top:88px}.compact .d-pad .right{left:88px;top:44px}
    .compact .face-buttons{right:0;top:20px;width:100px;height:96px}
    .compact .face.enter{left:56px;top:0}.compact .face.option{left:0;top:0}
    .compact .bottom-buttons{right:0;top:72px;width:100px;height:44px}
  }
</style>
