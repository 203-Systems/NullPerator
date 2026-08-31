<script>
  export let input
  export let disabled = false
  export let heldActions = []
  export let compact = false

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
    input?.press(action, clickSource); input?.release(action, clickSource)
  }

</script>

<div class="operator-controls" class:compact aria-label="PicoTracker virtual controls">
  <div class="d-pad" aria-label="Directional controls">
    {#each [['up','Up','W','▲'],['left','Left','A','◀'],['down','Down','S','▼'],['right','Right','D','▶']] as [action,label,key,glyph]}
      <button type="button" class={action} class:pressed={heldActions.includes(action)} aria-label={label} aria-pressed={heldActions.includes(action)} data-action={action} {disabled}
        onpointerdown={(event) => press(event, action)} onpointerup={(event) => release(event, action, true)}
        onpointercancel={(event) => cancel(event, action)} onlostpointercapture={(event) => release(event, action)} onclick={(event) => activate(event, action)}>
        <span class="switch"><span>{glyph}</span></span><kbd>{key}</kbd>
      </button>
    {/each}
  </div>

  <div class="face-buttons">
    <button type="button" class="face enter" class:pressed={heldActions.includes('edit')} aria-label="EDIT" aria-pressed={heldActions.includes('edit')} data-action="edit" {disabled}
      onpointerdown={(e)=>press(e,'edit')} onpointerup={(e)=>release(e,'edit',true)} onpointercancel={(e)=>cancel(e,'edit')} onlostpointercapture={(e)=>release(e,'edit')} onclick={(e)=>activate(e,'edit')}>
      <span class="switch"><span>↵</span></span><kbd>K</kbd><em>EDIT</em>
    </button>
    <button type="button" class="face edit" class:pressed={heldActions.includes('option')} aria-label="OPTION" aria-pressed={heldActions.includes('option')} data-action="option" {disabled}
      onpointerdown={(e)=>press(e,'option')} onpointerup={(e)=>release(e,'option',true)} onpointercancel={(e)=>cancel(e,'option')} onlostpointercapture={(e)=>release(e,'option')} onclick={(e)=>activate(e,'option')}>
      <span class="switch"><span>✦</span></span><kbd>J</kbd><em>OPTION</em>
    </button>
  </div>

  <div class="bottom-buttons">
    <button type="button" class:pressed={heldActions.includes('shift')} aria-label="SHIFT" aria-pressed={heldActions.includes('shift')} data-action="shift" {disabled}
      onpointerdown={(e)=>press(e,'shift')} onpointerup={(e)=>release(e,'shift',true)} onpointercancel={(e)=>cancel(e,'shift')} onlostpointercapture={(e)=>release(e,'shift')} onclick={(e)=>activate(e,'shift')}>
      <span class="switch"></span><kbd>X</kbd><em>SHIFT</em>
    </button>
    <button type="button" class:pressed={heldActions.includes('play')} aria-label="PLAY" aria-pressed={heldActions.includes('play')} data-action="play" {disabled}
      onpointerdown={(e)=>press(e,'play')} onpointerup={(e)=>release(e,'play',true)} onpointercancel={(e)=>cancel(e,'play')} onlostpointercapture={(e)=>release(e,'play')} onclick={(e)=>activate(e,'play')}>
      <span class="switch"><span>▶</span></span><kbd>C</kbd><em>PLAY</em>
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
  .operator-controls.compact kbd { display:none; }
  .operator-controls.compact { height:166px; margin-top:10px; }
  .operator-controls.compact::before { display:none; }
  .compact button,.compact .d-pad button,.compact .face { width:52px; height:52px; border-radius:14px; transform:none; }
  .compact button:active,.compact button.pressed,.compact .d-pad button:active,.compact .d-pad button.pressed,.compact .face:active,.compact .face.pressed { transform:translateY(1px); }
  .compact .d-pad { left:0; top:4px; width:174px; height:156px; }
  .compact .d-pad .up{left:61px;top:0}.compact .d-pad .left{left:7px;top:52px}.compact .d-pad .down{left:61px;top:104px}.compact .d-pad .right{left:115px;top:52px}
  .compact .d-pad .switch { top:18px; }
  .compact .d-pad .switch,.compact .d-pad kbd,.compact .face kbd,.compact .face em { rotate:0deg; }
  .compact .d-pad .switch > span { font-size:13px; }
  .compact .face-buttons { right:0; top:4px; width:120px; height:120px; }
  .compact .face.enter{left:64px;top:0}.compact .face.edit{left:4px;top:0}
  .compact .face .switch { display:grid; top:11px; }
  .compact .face .switch > span { font-size:12px; }
  .compact .bottom-buttons { left:auto; right:0; top:64px; bottom:auto; width:120px; height:52px; }
  .compact .bottom-buttons button:first-child{left:4px}.compact .bottom-buttons button:last-child{right:4px}
  .compact .bottom-buttons em { top:21px; bottom:auto; }
  .compact .face.enter,.compact .bottom-buttons button:last-child { border-color:rgba(76,201,240,.3); }
  @media(orientation:portrait) and (max-height:499px){
    .operator-controls.compact{margin-top:5px}
  }
  @media(orientation:landscape) and (max-height:539px){
    .operator-controls.compact{width:280px;height:156px}
    .compact button,.compact .d-pad button,.compact .face{width:48px;height:48px;border-radius:12px}
    .compact .d-pad{left:0;top:4px;width:148px;height:144px}
    .compact .d-pad .up{left:50px;top:0}.compact .d-pad .left{left:0;top:48px}.compact .d-pad .down{left:50px;top:96px}.compact .d-pad .right{left:100px;top:48px}
    .compact .face-buttons{right:0;top:4px;width:116px;height:116px}
    .compact .face.enter{left:60px;top:0}.compact .face.edit{left:0;top:0}
    .compact .bottom-buttons{right:0;top:60px;width:116px;height:48px}
    .compact .bottom-buttons button:first-child{left:0}.compact .bottom-buttons button:last-child{right:0}
  }
  @media(orientation:landscape) and (max-height:539px) and (max-width:567px){
    .operator-controls.compact{width:240px;height:136px}
    .compact button,.compact .d-pad button,.compact .face{width:44px;height:44px;border-radius:11px}
    .compact .d-pad{left:0;top:2px;width:132px;height:132px}
    .compact .d-pad .up{left:44px;top:0}.compact .d-pad .left{left:0;top:44px}.compact .d-pad .down{left:44px;top:88px}.compact .d-pad .right{left:88px;top:44px}
    .compact .face-buttons{right:0;top:20px;width:100px;height:96px}
    .compact .face.enter{left:56px;top:0}.compact .face.edit{left:0;top:0}
    .compact .bottom-buttons{right:0;top:72px;width:100px;height:44px}
  }
</style>
