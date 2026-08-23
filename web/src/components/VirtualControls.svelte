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
  .operator-controls { position:relative; width:100%; height:184px; margin-top:10px; overflow:hidden; border:1px solid #303239; border-radius:10px; background:#181a1f; touch-action:none; user-select:none; }
  button { position:absolute; width:48px; height:48px; padding:0; border:1px solid #353841; border-radius:8px; color:#aeb2b7; background:#202228; cursor:pointer; touch-action:none; }
  button:hover:not(:disabled) { border-color:#555a66; background:#252830; }
  button:disabled { cursor:not-allowed; opacity:.45; }
  .switch { position:absolute; left:9px; top:8px; display:grid; width:28px; height:24px; place-items:center; border-radius:5px; color:#c3c7ce; background:#111318; box-shadow:inset 0 0 0 1px #30333a; }
  .switch::before { content:''; width:14px; height:14px; border-radius:50%; background:#292c33; }
  .switch > span { position:absolute; color:#aab0ba; font:700 9px/1 var(--mono); }
  button:active { transform:translateY(1px); background:#15171b; }
  kbd { position:absolute; right:4px; top:4px; display:grid; min-width:15px; height:15px; place-items:center; border-radius:3px; color:#9edff1; background:#30333a; font:600 8px/1 var(--mono); }
  em { position:absolute; left:50%; bottom:4px; transform:translateX(-50%); color:#717680; font:500 7px/1 var(--mono); letter-spacing:.07em; font-style:normal; }
  .d-pad { position:absolute; left:28px; top:22px; width:132px; height:132px; }
  .d-pad .up{left:42px;top:0}.d-pad .left{left:0;top:42px}.d-pad .down{left:42px;top:84px}.d-pad .right{left:84px;top:42px}
  .face-buttons { position:absolute; right:27px; top:25px; width:96px; height:100px; }
  .face.enter{right:0;top:0}.face.edit{left:0;top:50px}
  .bottom-buttons { position:absolute; right:25px; bottom:10px; width:106px; height:48px; }
  .bottom-buttons button:first-child{left:0}.bottom-buttons button:last-child{right:0}
</style>
