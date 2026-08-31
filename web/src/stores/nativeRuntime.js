import { createMidiStore } from './midi.js'

function postNative(command, payload = {}) {
  const handler = globalThis.webkit?.messageHandlers?.nullPeratorNative
  if (!handler) return Promise.reject(new Error('Native core bridge is unavailable'))
  return Promise.resolve(handler.postMessage({ command, ...payload }))
}

function createNativeMidiBridge(sendNative) {
  const emptyDrain = () => ({ packets: [], droppedNormal: 0, droppedRealtime: 0 })
  let bufferedDrain = emptyDrain()
  let drainRequest = null

  const requestDrain = () => {
    if (drainRequest) return
    drainRequest = sendNative('nativeMidiDrain')
      .then((result) => {
        bufferedDrain = {
          packets: (result?.packets ?? []).map((packet, index) => ({
            sequence: Number(packet.sequence ?? index),
            timestamp: 0,
            bytes: Uint8Array.from(packet.bytes ?? []),
          })),
          droppedNormal: Number(result?.droppedNormal ?? 0),
          droppedRealtime: Number(result?.droppedRealtime ?? 0),
        }
      })
      .catch((error) => console.error('[NativeCore] MIDI drain failed', error))
      .finally(() => { drainRequest = null })
  }

  return Object.freeze({
    submitInput(input, timestamp) {
      const bytes = Array.from(input instanceof Uint8Array ? input : new Uint8Array(input))
      void sendNative('nativeMidiInput', { bytes, timestamp }).catch((error) => {
        console.error('[NativeCore] MIDI input failed', error)
      })
      return true
    },
    drainOutput() {
      const drained = bufferedDrain
      bufferedDrain = emptyDrain()
      requestDrain()
      return drained
    },
    disconnect(directions) {
      void sendNative('nativeMidiDisconnect', { directions }).catch(() => {})
    },
    setOutputConnected(connected) {
      void sendNative('nativeMidiOutputConnected', { connected }).catch(() => {})
    },
  })
}

function createValueStore(initial) {
  const listeners = new Set()
  let snapshot = Object.freeze({ ...initial })
  return Object.freeze({
    subscribe(listener) {
      listeners.add(listener)
      listener(snapshot)
      return () => listeners.delete(listener)
    },
    snapshot: () => snapshot,
    set(next) {
      snapshot = Object.freeze({ ...snapshot, ...next })
      for (const listener of listeners) listener(snapshot)
    },
  })
}

function createNativeInput(sendNative) {
  let mask = 0
  let generation = 0
  let lastAction = -1
  const send = (action, pressed, repeat = false) => {
    if (!Number.isInteger(action) || action < 0 || action > 15) return false
    const bit = 1 << action
    if (!repeat) mask = pressed ? (mask | bit) : (mask & ~bit)
    generation += 1
    lastAction = action
    void sendNative('nativeAction', { action, pressed, repeat }).catch((error) => {
      console.error('[NativeCore] input failed', error)
    })
    return true
  }
  return Object.freeze({
    pressAction: (action) => send(action, true),
    repeatAction: (action) => send(action, true, true),
    releaseAction: (action) => send(action, false),
    releaseAllActions() {
      mask = 0
      generation += 1
      void sendNative('nativeReleaseAll').catch(() => {})
    },
    getActionMask: () => mask,
    getActionGeneration: () => generation,
    getLastAction: () => lastAction,
  })
}

export function createNativeRuntimeManager(options = {}) {
  const sendNative = options.postNative ?? postNative
  const listeners = new Set()
  const battery = createValueStore({ percentage: 0, charging: false, available: false })
  const audioState = createValueStore({ state: 'native', error: null, metrics: null, capability: { available: true, reason: 'native-core' } })
  const audio = Object.freeze({
    subscribe: audioState.subscribe,
    snapshot: audioState.snapshot,
    initialize: async () => audioState.snapshot(),
    unlockAudio: async () => audioState.snapshot(),
    stop: async () => {},
  })
  const input = createNativeInput(sendNative)
  const midi = createMidiStore(createNativeMidiBridge(sendNative), options.midiOptions)
  let snapshot = Object.freeze({
    state: 'idle',
    error: null,
    buildMetadata: { runtime: 'native-cpp', platform: 'ios', version: 1 },
    frameContent: 'native',
    input: null,
    audio: null,
    battery: null,
    storage: null,
    files: null,
    hostFolder: null,
    midi: null,
    logs: null,
    trace: null,
  })
  let operation = Promise.resolve()

  const publish = (next) => {
    snapshot = Object.freeze({ ...snapshot, ...next })
    for (const listener of listeners) listener(snapshot)
  }
  const enqueue = (work) => {
    const result = operation.then(work, work)
    operation = result.catch(() => {})
    return result
  }

  async function startNow() {
    if (snapshot.state === 'ready') return snapshot
    publish({ state: 'booting', error: null })
    try {
      const metadata = await sendNative('nativeReady')
      publish({
        state: 'ready',
        buildMetadata: metadata ?? snapshot.buildMetadata,
        frameContent: 'native',
        input,
        audio,
        battery: Object.freeze({
          subscribe: battery.subscribe,
          snapshot: battery.snapshot,
          setState: (state) => battery.set(state),
        }),
        midi,
      })
      return snapshot
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error)
      publish({ state: 'failed', error: message, input: null, audio: null, battery: null })
      throw error
    }
  }

  async function stopNow() {
    input.releaseAllActions()
    await midi.stop()
    publish({ state: 'idle', error: null, input: null, audio: null, battery: null, midi: null })
  }

  return Object.freeze({
    subscribe(listener) {
      listeners.add(listener)
      listener(snapshot)
      return () => listeners.delete(listener)
    },
    getSnapshot: () => snapshot,
    start: () => enqueue(startNow),
    stop: () => enqueue(stopNow),
    restart: () => enqueue(async () => { await stopNow(); return startNow() }),
  })
}

export const nativeRuntimeStore = createNativeRuntimeManager()
