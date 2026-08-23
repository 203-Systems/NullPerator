<script>
  export let input
  export let disabled = false

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
  function cancel(event) { event.preventDefault(); input?.releaseAll() }
  function pressStart(event) {
    event.preventDefault()
    try { event.currentTarget.setPointerCapture?.(event.pointerId) } catch {}
    input?.pressStart(source(event))
  }
  function releaseStart(event, suppressClick = false) {
    if (suppressClick) pointerClickSuppression.add(event.currentTarget)
    input?.releaseStart(source(event))
  }
  function activateStart(event) {
    const keyboardActivation = event.detail === 0
    if (!keyboardActivation && pointerClickSuppression.delete(event.currentTarget)) { event.preventDefault(); return }
    if (keyboardActivation) pointerClickSuppression.delete(event.currentTarget)
    event.preventDefault()
    input?.pressStart('virtual-click:start'); input?.releaseStart('virtual-click:start')
  }
  function activate(event, action) {
    const keyboardActivation = event.detail === 0
    if (!keyboardActivation && pointerClickSuppression.delete(event.currentTarget)) { event.preventDefault(); return }
    if (keyboardActivation) pointerClickSuppression.delete(event.currentTarget)
    event.preventDefault()
    const clickSource = `virtual-click:${action}`
    input?.press(action, clickSource); input?.release(action, clickSource)
  }

</script>

<div class="operator-controls" aria-label="PicoTracker virtual controls">
  <div class="pcb-lines" aria-hidden="true"></div>
  <div class="d-pad" aria-label="Directional controls">
    {#each [['up','Up','W','▲'],['left','Left','A','◀'],['down','Down','S','▼'],['right','Right','D','▶']] as [action,label,key,glyph]}
      <button type="button" class={action} aria-label={label} data-action={action} {disabled}
        onpointerdown={(event) => press(event, action)} onpointerup={(event) => release(event, action, true)}
        onpointercancel={cancel} onlostpointercapture={(event) => release(event, action)} onclick={(event) => activate(event, action)}>
        <span class="switch"><span>{glyph}</span></span><kbd>{key}</kbd>
      </button>
    {/each}
  </div>

  <div class="face-buttons">
    <button type="button" class="face enter" aria-label="ENTER" data-action="enter" {disabled}
      onpointerdown={(e)=>press(e,'enter')} onpointerup={(e)=>release(e,'enter',true)} onpointercancel={cancel} onlostpointercapture={(e)=>release(e,'enter')} onclick={(e)=>activate(e,'enter')}>
      <span class="switch"><span>↵</span></span><kbd>J</kbd><em>ENTER</em>
    </button>
    <button type="button" class="face edit" aria-label="EDIT" data-action="edit" {disabled}
      onpointerdown={(e)=>press(e,'edit')} onpointerup={(e)=>release(e,'edit',true)} onpointercancel={cancel} onlostpointercapture={(e)=>release(e,'edit')} onclick={(e)=>activate(e,'edit')}>
      <span class="switch"><span>✦</span></span><kbd>K</kbd><em>EDIT</em>
    </button>
  </div>

  <div class="bottom-buttons">
    <button type="button" aria-label="ALT" data-action="alt" {disabled}
      onpointerdown={(e)=>press(e,'alt')} onpointerup={(e)=>release(e,'alt',true)} onpointercancel={cancel} onlostpointercapture={(e)=>release(e,'alt')} onclick={(e)=>activate(e,'alt')}>
      <span class="switch"></span><kbd>X</kbd><em>ALT</em>
    </button>
    <button type="button" aria-label="PLAY" data-action="start" title="Tap: PLAY · Hold: NAV · Hold ALT first: hold ALT+PLAY" {disabled}
      onpointerdown={pressStart} onpointerup={(e)=>releaseStart(e,true)} onpointercancel={cancel} onlostpointercapture={releaseStart} onclick={activateStart}>
      <span class="switch"><span>▶</span></span><kbd>C</kbd><em>START</em>
    </button>
  </div>
</div>

<style>
  .operator-controls { position:relative; width:100%; height:176px; overflow:hidden; border:1px solid #34343a; border-top:0; border-radius:0 0 16px 16px; background:linear-gradient(145deg,#17181b 0%,#090a0c 58%,#15161a 100%); box-shadow:inset 0 1px rgba(255,255,255,.07),inset 0 -16px 34px rgba(0,0,0,.5); touch-action:none; user-select:none; }
  .operator-controls::before,.operator-controls::after { content:''; position:absolute; bottom:10px; width:8px; height:8px; border-radius:50%; background:radial-gradient(circle at 38% 35%,#d9d9d2,#777 50%,#222 70%); box-shadow:0 0 0 1px #030303; }
  .operator-controls::before { left:12px; } .operator-controls::after { right:12px; }
  .pcb-lines { position:absolute; inset:0; opacity:.18; background:repeating-radial-gradient(ellipse at 50% 45%,transparent 0 14px,#58646b 15px 15.5px,transparent 16px 23px); clip-path:polygon(15% 10%,85% 8%,78% 88%,20% 90%); }
  button { position:absolute; width:48px; height:48px; padding:0; border:0; color:#aeb2b7; background:transparent; cursor:pointer; touch-action:none; }
  button:disabled { cursor:not-allowed; opacity:.45; }
  .switch { position:absolute; left:8px; top:5px; display:grid; width:32px; height:32px; place-items:center; border:2px solid #d8d9d6; border-radius:5px; color:#8b9198; background:linear-gradient(145deg,#f0f0ed 0 12%,#83878b 14% 29%,#202226 31% 100%); box-shadow:0 3px 4px #000,0 0 0 1px #171719; transform:rotate(-4deg); }
  .switch::before { content:''; width:22px; height:22px; border-radius:50%; background:radial-gradient(circle at 37% 30%,#33353a,#111216 70%); box-shadow:inset 0 -2px 3px #000,0 1px 1px rgba(255,255,255,.12); }
  .switch > span { position:absolute; color:#858b92; font:700 9px/1 var(--mono); opacity:.7; }
  button:active .switch { transform:rotate(-4deg) translateY(2px); filter:brightness(.8); }
  kbd { position:absolute; right:0; top:0; display:grid; min-width:17px; height:17px; place-items:center; border:1px solid rgba(255,255,255,.16); border-radius:3px; color:#d9f7ff; background:#272b30; font:600 9px/1 var(--mono); box-shadow:0 1px 2px #000; }
  em { position:absolute; left:50%; bottom:-1px; transform:translateX(-50%); color:rgba(255,255,255,.36); font:500 7px/1 var(--mono); letter-spacing:.07em; font-style:normal; }
  .d-pad { position:absolute; left:35px; top:21px; width:126px; height:126px; transform:rotate(-7deg); }
  .d-pad button { transform:rotate(7deg); } .d-pad .up{left:39px;top:0}.d-pad .left{left:0;top:39px}.d-pad .down{left:39px;top:78px}.d-pad .right{left:78px;top:39px}
  .face-buttons { position:absolute; right:28px; top:25px; width:90px; height:95px; transform:rotate(6deg); }
  .face-buttons button { transform:rotate(-6deg); } .face.enter{right:0;top:0}.face.edit{left:0;top:45px}
  .bottom-buttons { position:absolute; left:164px; bottom:13px; width:102px; height:52px; transform:rotate(-2deg); }
  .bottom-buttons button:first-child{left:0}.bottom-buttons button:last-child{right:0}
</style>
