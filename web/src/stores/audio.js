import { audioMetricsContract } from '../handles/audio.js'

const states = ['unavailable', 'locked', 'starting', 'running', 'suspended', 'failed', 'stopped']
const mapState = (value) => states[value] ?? 'unavailable'

export function createAudioStore(bridge, options = {}) {
  const listeners = new Set()
  let snapshot = Object.freeze({
    state: 'unavailable', error: null, metrics: null,
    capability: bridge?.capability ?? { available: false, reason: 'Native audio capability was not checked.' },
  })
  let unlock = null
  let timer = null
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
  return {
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    snapshot: () => snapshot,
    async initialize() {
      await refresh()
      if (timer === null && options.poll !== false) timer = globalThis.setInterval?.(() => refresh(), options.pollMs ?? 250) ?? null
      return snapshot
    },
    refresh,
    configure(settings) {
      bridge?.configureAudio?.(settings)
      return snapshot
    },
    async unlockAudio() {
      if (unlock) return unlock
      // Calling into Wasm must remain directly inside the click handler: some
      // browsers clear transient user activation before a queued microtask.
      const accepted = bridge?.unlockAudio?.()
      unlock = Promise.resolve().then(async () => {
        if (!accepted) { await refresh(); throw new Error(snapshot.error || 'Audio unlock was rejected.') }
        return refresh()
      }).finally(() => { unlock = null })
      return unlock
    },
    unlock() { return this.unlockAudio() },
    async stop() {
      if (timer !== null) { globalThis.clearInterval?.(timer); timer = null }
      bridge?.stopAudio?.()
      return refresh()
    },
  }
}
