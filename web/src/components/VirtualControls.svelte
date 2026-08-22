<script>
  export let input
  export let disabled = false

  const pointerClickSuppression = new WeakSet()

  const controls = [
    ['alt', 'ALT'],
    ['edit', 'EDIT'],
    ['enter', 'ENTER'],
    ['nav', 'NAV'],
    ['play', 'PLAY'],
    ['select', 'SELECT'],
    ['power', 'POWER'],
  ]

  function source(event) {
    return `pointer:${event.pointerId}`
  }

  function press(event, action) {
    event.preventDefault()
    try {
      event.currentTarget.setPointerCapture?.(event.pointerId)
    } catch {
      // Synthetic browser events do not always have an active pointer to capture.
    }
    input?.press(action, source(event))
  }

  function release(event, action, suppressClick = false) {
    if (suppressClick) pointerClickSuppression.add(event.currentTarget)
    input?.release(action, source(event))
  }

  function cancel(event) {
    event.preventDefault()
    input?.releaseAll()
  }

  function activate(event, action) {
    // Native keyboard button activation has detail === 0. It must never be
    // mistaken for the compatibility click generated after a pointer release.
    const keyboardActivation = event.detail === 0
    if (!keyboardActivation && pointerClickSuppression.delete(event.currentTarget)) {
      event.preventDefault()
      return
    }
    if (keyboardActivation) pointerClickSuppression.delete(event.currentTarget)
    event.preventDefault()
    const clickSource = `virtual-click:${action}`
    input?.press(action, clickSource)
    input?.release(action, clickSource)
  }
</script>

<div class="virtual-controls" aria-label="PicoTracker virtual controls">
  <div class="d-pad" aria-label="Directional controls">
    <button type="button" class="up" aria-label="Up" data-action="up" {disabled}
      onpointerdown={(event) => press(event, 'up')}
      onpointerup={(event) => release(event, 'up', true)}
      onpointercancel={cancel}
      onlostpointercapture={(event) => release(event, 'up')}
      onclick={(event) => activate(event, 'up')}>▲</button>
    <button type="button" class="left" aria-label="Left" data-action="left" {disabled}
      onpointerdown={(event) => press(event, 'left')}
      onpointerup={(event) => release(event, 'left', true)}
      onpointercancel={cancel}
      onlostpointercapture={(event) => release(event, 'left')}
      onclick={(event) => activate(event, 'left')}>◀</button>
    <button type="button" class="down" aria-label="Down" data-action="down" {disabled}
      onpointerdown={(event) => press(event, 'down')}
      onpointerup={(event) => release(event, 'down', true)}
      onpointercancel={cancel}
      onlostpointercapture={(event) => release(event, 'down')}
      onclick={(event) => activate(event, 'down')}>▼</button>
    <button type="button" class="right" aria-label="Right" data-action="right" {disabled}
      onpointerdown={(event) => press(event, 'right')}
      onpointerup={(event) => release(event, 'right', true)}
      onpointercancel={cancel}
      onlostpointercapture={(event) => release(event, 'right')}
      onclick={(event) => activate(event, 'right')}>▶</button>
  </div>
  <div class="action-buttons">
    {#each controls as [action, label]}
      <button type="button" aria-label={label} data-action={action} {disabled}
        onpointerdown={(event) => press(event, action)}
        onpointerup={(event) => release(event, action, true)}
        onpointercancel={cancel}
        onlostpointercapture={(event) => release(event, action)}
        onclick={(event) => activate(event, action)}>{label}</button>
    {/each}
  </div>
</div>

<style>
  .virtual-controls {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 1rem;
    padding: 1rem;
    touch-action: none;
    user-select: none;
  }

  button {
    min-height: 2.4rem;
    border: 1px solid #665784;
    border-radius: .35rem;
    background: #272135;
    color: #eee9ff;
    font: inherit;
    font-size: .7rem;
    font-weight: 700;
    letter-spacing: .04em;
    touch-action: none;
  }

  button:active { background: #5c4077; }
  .d-pad { display: grid; grid-template-columns: repeat(3, 2.4rem); grid-template-rows: repeat(3, 2.4rem); gap: .15rem; }
  .d-pad button { font-size: .9rem; }
  .up { grid-column: 2; }
  .left { grid-column: 1; grid-row: 2; }
  .down { grid-column: 2; grid-row: 2; }
  .right { grid-column: 3; grid-row: 2; }
  .action-buttons { display: flex; flex-wrap: wrap; gap: .35rem; max-width: 25rem; }
  .action-buttons button { padding: 0 .65rem; }
</style>
