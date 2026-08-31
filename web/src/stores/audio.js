import { audioMetricsContract } from '../handles/audio.js'

const states = ['unavailable', 'locked', 'starting', 'running', 'suspended', 'failed', 'stopped']
const mapState = (value) => states[value] ?? 'unavailable'
const gestureRecoverableStates = new Set(['locked', 'starting', 'running', 'suspended'])

export function createAudioStore(bridge, options = {}) {
  const listeners = new Set()
  let snapshot = Object.freeze({
    state: 'unavailable', error: null, metrics: null,
    capability: bridge?.capability ?? { available: false, reason: 'Native audio capability was not checked.' },
  })
  let unlock = null
  let timer = null
  let detachGestureRecovery = null
  const publish = (next) => {
    snapshot = Object.freeze({ ...snapshot, ...next })
    for (const listener of listeners) listener(snapshot)
  }
  const readMetrics = () => {
    const metrics = bridge?.getAudioMetrics?.() ?? null
    if (!metrics) return { metrics: null, error: 'Audio metrics are unavailable.' }
    if (metrics.version !== audioMetricsContract.version || metrics.size !== audioMetricsContract.size) {
      return { metrics: null, error: 'Audio metrics contract is incompatible.' }
    }
    return { metrics, error: null }
  }
  async function refresh() {
    const state = mapState(bridge?.getAudioState?.())
    const metricState = readMetrics()
    const error = state === 'failed' ? bridge?.getAudioError?.() || 'Audio startup failed.' : metricState.error
    publish({ state, ...metricState, error })
    return snapshot
  }
  async function unlockAudio() {
    if (unlock) return unlock
    // Calling into Wasm must remain directly inside the trusted event handler:
    // some browsers clear transient user activation before a queued microtask.
    const accepted = bridge?.unlockAudio?.()
    unlock = Promise.resolve().then(async () => {
      if (!accepted) { await refresh(); throw new Error(snapshot.error || 'Audio unlock was rejected.') }
      return refresh()
    }).finally(() => { unlock = null })
    return unlock
  }
  function attachGestureRecovery(target = options.gestureTarget ?? globalThis.window) {
    detachGestureRecovery?.()
    if (!target?.addEventListener) return () => {}
    let attached = true
    const recover = (event) => {
      if (!attached || event?.isTrusted !== true || event.repeat ||
          !gestureRecoverableStates.has(snapshot.state)) return
      // Do not await: the bridge call above is synchronous and therefore stays
      // in the activation task; refresh/error publication can finish later.
      void unlockAudio().catch(() => {})
    }
    target.addEventListener('pointerdown', recover, true)
    target.addEventListener('keydown', recover, true)
    const detach = () => {
      if (!attached) return
      attached = false
      target.removeEventListener?.('pointerdown', recover, true)
      target.removeEventListener?.('keydown', recover, true)
      if (detachGestureRecovery === detach) detachGestureRecovery = null
    }
    detachGestureRecovery = detach
    return detach
  }
  return {
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    snapshot: () => snapshot,
    async initialize() {
      await refresh()
      attachGestureRecovery()
      if (timer === null && options.poll !== false) timer = globalThis.setInterval?.(() => refresh(), options.pollMs ?? 250) ?? null
      return snapshot
    },
    refresh,
    configure(settings) {
      bridge?.configureAudio?.(settings)
      return snapshot
    },
    unlockAudio,
    unlock() { return unlockAudio() },
    attachGestureRecovery,
    async stop() {
      detachGestureRecovery?.()
      if (timer !== null) { globalThis.clearInterval?.(timer); timer = null }
      bridge?.stopAudio?.()
      return refresh()
    },
  }
}
