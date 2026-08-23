<script>
  export let input
  export let disabled = false
  export let heldActions = []

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
  <div class="d-pad" aria-label="Directional controls">
    {#each [['up','Up','W','▲'],['left','Left','A','◀'],['down','Down','S','▼'],['right','Right','D','▶']] as [action,label,key,glyph]}
      <button type="button" class={action} class:pressed={heldActions.includes(action)} aria-label={label} aria-pressed={heldActions.includes(action)} data-action={action} {disabled}
        onpointerdown={(event) => press(event, action)} onpointerup={(event) => release(event, action, true)}
        onpointercancel={cancel} onlostpointercapture={(event) => release(event, action)} onclick={(event) => activate(event, action)}>
        <span class="switch"><span>{glyph}</span></span><kbd>{key}</kbd>
      </button>
    {/each}
  </div>

  <div class="face-buttons">
    <button type="button" class="face enter" class:pressed={heldActions.includes('enter')} aria-label="ENTER" aria-pressed={heldActions.includes('enter')} data-action="enter" {disabled}
      onpointerdown={(e)=>press(e,'enter')} onpointerup={(e)=>release(e,'enter',true)} onpointercancel={cancel} onlostpointercapture={(e)=>release(e,'enter')} onclick={(e)=>activate(e,'enter')}>
      <span class="switch"><span>↵</span></span><kbd>J</kbd><em>ENTER</em>
    </button>
    <button type="button" class="face edit" class:pressed={heldActions.includes('edit')} aria-label="EDIT" aria-pressed={heldActions.includes('edit')} data-action="edit" {disabled}
      onpointerdown={(e)=>press(e,'edit')} onpointerup={(e)=>release(e,'edit',true)} onpointercancel={cancel} onlostpointercapture={(e)=>release(e,'edit')} onclick={(e)=>activate(e,'edit')}>
      <span class="switch"><span>✦</span></span><kbd>K</kbd><em>EDIT</em>
    </button>
  </div>

  <div class="bottom-buttons">
    <button type="button" class:pressed={heldActions.includes('alt')} aria-label="ALT" aria-pressed={heldActions.includes('alt')} data-action="alt" {disabled}
      onpointerdown={(e)=>press(e,'alt')} onpointerup={(e)=>release(e,'alt',true)} onpointercancel={cancel} onlostpointercapture={(e)=>release(e,'alt')} onclick={(e)=>activate(e,'alt')}>
      <span class="switch"></span><kbd>X</kbd><em>ALT</em>
    </button>
    <button type="button" class:pressed={heldActions.includes('nav') || heldActions.includes('play')} aria-label="PLAY" aria-pressed={heldActions.includes('nav') || heldActions.includes('play')} data-action="start" title="Tap: PLAY · Hold: NAV · Hold ALT first: hold ALT+PLAY" {disabled}
      onpointerdown={pressStart} onpointerup={(e)=>releaseStart(e,true)} onpointercancel={cancel} onlostpointercapture={releaseStart} onclick={activateStart}>
      <span class="switch"><span>▶</span></span><kbd>C</kbd><em>START</em>
    </button>
  </div>
</div>

<style>
  .operator-controls { position:relative; width:100%; height:220px; margin-top:12px; overflow:visible; touch-action:none; user-select:none; }
  .operator-controls::before { content:'+'; position:absolute; left:50%; top:0; display:grid; width:22px; height:12px; translate:-50% 0; place-items:center; border:1px solid #343841; color:#747983; font:8px/1 var(--mono); pointer-events:none; }
  button { position:absolute; width:48px; height:48px; padding:0; border:1px solid #3a3e47; border-radius:7px; color:#aeb2b7; background:#111318; cursor:pointer; touch-action:none; }
  button:hover:not(:disabled) { border-color:#626975; background:#17191e; }
  button:disabled { cursor:not-allowed; opacity:.45; }
  .d-pad button,.face { transform:rotate(45deg); }
  .d-pad button:active,.d-pad button.pressed,.face:active,.face.pressed { transform:rotate(45deg) translate(1px,1px); border-color:#4cc9f0; background:#0d0f12; box-shadow:inset 0 0 0 1px rgba(76,201,240,.18); }
  .switch { position:absolute; left:50%; top:27px; display:grid; width:18px; height:14px; translate:-50% 0; place-items:center; color:#858b95; background:transparent; }
  .switch::before { display:none; }
  .switch > span { color:#858b95; font:700 9px/1 var(--mono); }
  kbd { position:absolute; left:50%; top:8px; display:grid; min-width:18px; height:14px; translate:-50% 0; place-items:center; border:0; color:#4cc9f0; background:transparent; font:600 11px/1 var(--mono); }
  em { position:absolute; left:50%; bottom:8px; translate:-50% 0; color:#777c86; font:500 7px/1 var(--mono); letter-spacing:.07em; font-style:normal; }
  .d-pad .switch,.d-pad kbd,.face kbd,.face em { rotate:-45deg; }
  .face .switch { display:none; }
  .d-pad { position:absolute; left:23px; top:32px; width:142px; height:126px; }
  .d-pad .up{left:47px;top:0}.d-pad .left{left:8px;top:39px}.d-pad .down{left:47px;top:78px}.d-pad .right{left:86px;top:39px}
  .face-buttons { position:absolute; right:12px; top:40px; width:126px; height:126px; }
  .face.enter{left:67px;top:0}.face.edit{left:28px;top:39px}
  .bottom-buttons { position:absolute; left:107px; bottom:0; width:106px; height:48px; }
  .bottom-buttons .switch { display:none; }
  .bottom-buttons button:active,.bottom-buttons button.pressed { transform:translateY(1px); border-color:#4cc9f0; background:#0d0f12; box-shadow:inset 0 0 0 1px rgba(76,201,240,.18); }
  .bottom-buttons button:first-child{left:0}.bottom-buttons button:last-child{right:0}
</style>
