<script>
  export let checked = false
  export let disabled = false
  export let label
  export let onChange = () => {}
  export let developerToggle = false
</script>

<button
  type="button"
  class="toggle-switch"
  class:on={checked}
  aria-label={label}
  aria-pressed={checked}
  {disabled}
  data-developer-toggle={developerToggle ? '' : undefined}
  onclick={() => onChange(!checked)}
>
  <span aria-hidden="true"></span>
</button>

<style>
  .toggle-switch {
    position: relative;
    width: 44px;
    height: 44px;
    flex: 0 0 auto;
    padding: 0;
    border: 0;
    border-radius: var(--radius-card);
    background: transparent;
  }

  .toggle-switch::before {
    content: '';
    position: absolute;
    inset: 10px 1px;
    border: 1px solid var(--border);
    border-radius: 999px;
    background: var(--control-toggle-off);
    transition: background 120ms, border-color 120ms;
  }

  span {
    position: absolute;
    top: 14px;
    left: 5px;
    width: 16px;
    height: 16px;
    border-radius: 50%;
    background: var(--muted);
    transition: translate 120ms, background 120ms;
  }

  .toggle-switch.on::before {
    border-color: var(--accent-border-strong);
    background: var(--accent-fill-strong);
  }

  .toggle-switch.on span {
    translate: 18px 0;
    background: var(--accent);
  }

  .toggle-switch:disabled {
    cursor: not-allowed;
    opacity: .6;
  }

  .toggle-switch:not(:disabled):hover::before {
    border-color: var(--accent-border);
  }
</style>
