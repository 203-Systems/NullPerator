<script>
  export let runtime
</script>

<div class="device-panel">
  <div class="device-frame">
    <!-- SDL2 2.32 hard-codes #canvas for its passive browser event hooks. -->
    <canvas id="canvas" aria-hidden="true" tabindex="-1"></canvas>
    <canvas
      id="picotracker-canvas"
      width="240"
      height="240"
      tabindex="0"
      aria-label="PicoTracker display"
      data-frame-content={runtime.frameContent}
    ></canvas>
  </div>
  <div class="device-caption">
    <span>240 × 240 logical pixels</span>
    <span>{runtime.error ?? (runtime.state === 'ready' ? 'Tracker UI running' : 'Starting tracker UI')}</span>
  </div>
</div>
