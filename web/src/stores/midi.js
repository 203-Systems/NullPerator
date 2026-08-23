export const MIDI_INPUT_STORAGE_KEY = 'picotracker.midi.input.v1'
export const MIDI_OUTPUT_STORAGE_KEY = 'picotracker.midi.output.v1'

const messageOf = (error) => error instanceof Error ? error.message : String(error)
const denied = (error) => error?.name === 'NotAllowedError' || error?.name === 'SecurityError'

function portList(map) {
  if (!map || typeof map.values !== 'function') return []
  return [...map.values()].map((port) => Object.freeze({
    id: port.id,
    name: port.name || port.id,
    manufacturer: port.manufacturer || '',
    state: port.state || 'disconnected',
    connection: port.connection || 'closed',
  })).sort((left, right) => left.name.localeCompare(right.name) || left.id.localeCompare(right.id))
}

export function createMidiStore(bridge, options = {}) {
  const navigator = options.navigator ?? globalThis.navigator
  const storage = options.storage ?? globalThis.localStorage
  const requestFrame = options.requestAnimationFrame ?? globalThis.requestAnimationFrame?.bind(globalThis)
  const cancelFrame = options.cancelAnimationFrame ?? globalThis.cancelAnimationFrame?.bind(globalThis)
  const now = options.now ?? (() => globalThis.performance?.now?.() ?? 0)
  const delay = options.delay ?? ((milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds)))
  const supported = typeof navigator?.requestMIDIAccess === 'function'
  const listeners = new Set()
  let access = null
  let selectedInput = null
  let selectedOutput = null
  let frameId = null
  let stopped = false
  let snapshot = Object.freeze({
    state: supported ? 'idle' : 'unsupported',
    error: supported ? null : 'Web MIDI is unsupported in this browser.',
    inputs: [], outputs: [],
    selectedInputId: null, selectedOutputId: null,
    inputConnected: false, outputConnected: false,
    droppedInputBytes: 0, droppedNormal: 0, droppedRealtime: 0,
  })

  const publish = (next) => {
    snapshot = Object.freeze({ ...snapshot, ...next })
    for (const listener of listeners) listener(snapshot)
  }
  const remember = (key, id) => {
    if (!storage) return
    if (id) storage.setItem(key, id)
    else storage.removeItem(key)
  }
  const selectedId = (key) => storage?.getItem?.(key) || null

  async function waitForInputReset(generation) {
    if (!Number.isInteger(generation) || typeof bridge.inputResetGeneration !== 'function') return
    const deadline = now() + (options.disconnectTimeoutMs ?? 2_000)
    while (bridge.inputResetGeneration() !== generation) {
      if (now() >= deadline) throw new Error('Timed out waiting for WASM MIDI input reset')
      await delay(10)
    }
  }

  async function bindInput(id, disconnect = true) {
    if (selectedInput) {
      selectedInput.onmidimessage = null
      if (selectedInput.id !== id) await selectedInput.close?.()
    }
    if (disconnect) await waitForInputReset(bridge.disconnect(1))
    selectedInput = null
    const candidate = id ? access?.inputs?.get?.(id) : null
    if (candidate?.state === 'connected') {
      await candidate.open?.()
      candidate.onmidimessage = (event) => {
        const data = event?.data instanceof Uint8Array ? event.data : new Uint8Array(event?.data ?? [])
        const timestamp = Number.isFinite(event?.timeStamp) ? event.timeStamp : now()
        if (!bridge.submitInput(data, timestamp)) publish({ droppedInputBytes: snapshot.droppedInputBytes + data.byteLength })
      }
      selectedInput = candidate
    }
    publish({ selectedInputId: id, inputConnected: Boolean(selectedInput) })
  }

  async function bindOutput(id, disconnect = true) {
    if (selectedOutput && selectedOutput.id !== id) await selectedOutput.close?.()
    if (disconnect) bridge.disconnect(2)
    selectedOutput = null
    const candidate = id ? access?.outputs?.get?.(id) : null
    if (candidate?.state === 'connected') {
      await candidate.open?.()
      selectedOutput = candidate
    }
    bridge.setOutputConnected(Boolean(selectedOutput))
    publish({ selectedOutputId: id, outputConnected: Boolean(selectedOutput) })
  }

  async function refreshPorts() {
    if (!access) return
    const inputId = snapshot.selectedInputId
    const outputId = snapshot.selectedOutputId
    publish({ inputs: portList(access.inputs), outputs: portList(access.outputs) })
    if (inputId && (selectedInput !== access.inputs?.get?.(inputId) || selectedInput?.state !== 'connected')) await bindInput(inputId)
    if (outputId && (selectedOutput !== access.outputs?.get?.(outputId) || selectedOutput?.state !== 'connected')) await bindOutput(outputId)
  }

  function pumpOutput() {
    if (stopped || !access) return
    try {
      const drained = bridge.drainOutput()
      publish({ droppedNormal: drained.droppedNormal, droppedRealtime: drained.droppedRealtime })
      if (selectedOutput?.state === 'connected') {
        const current = now()
        for (const packet of drained.packets) {
          if (packet.timestamp > current) selectedOutput.send(packet.bytes, packet.timestamp)
          else selectedOutput.send(packet.bytes)
        }
      }
      frameId = requestFrame?.(pumpOutput) ?? null
    } catch (error) {
      frameId = null
      publish({ state: 'failed', error: messageOf(error) })
    }
  }

  return Object.freeze({
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    snapshot: () => snapshot,
    async requestMidiAccess() {
      if (!supported) throw new Error('Web MIDI is unsupported in this browser')
      if (access) return snapshot
      publish({ state: 'requesting', error: null })
      try {
        stopped = false
        access = await navigator.requestMIDIAccess({ sysex: false })
        access.onstatechange = () => refreshPorts().catch((error) => publish({ state: 'failed', error: messageOf(error) }))
        publish({
          inputs: portList(access.inputs), outputs: portList(access.outputs),
          selectedInputId: selectedId(MIDI_INPUT_STORAGE_KEY),
          selectedOutputId: selectedId(MIDI_OUTPUT_STORAGE_KEY),
        })
        await bindInput(snapshot.selectedInputId, false)
        await bindOutput(snapshot.selectedOutputId, false)
        publish({ state: 'ready', error: null })
        if (frameId === null) frameId = requestFrame?.(pumpOutput) ?? null
        return snapshot
      } catch (error) {
        access = null
        publish({ state: denied(error) ? 'denied' : 'failed', error: messageOf(error) })
        throw error
      }
    },
    async selectMidiInput(id) {
      if (!access) throw new Error('Request Web MIDI access before selecting an input')
      if (id !== null && !access.inputs?.has?.(id)) throw new Error('Unknown Web MIDI input')
      remember(MIDI_INPUT_STORAGE_KEY, id)
      await bindInput(id)
    },
    async selectMidiOutput(id) {
      if (!access) throw new Error('Request Web MIDI access before selecting an output')
      if (id !== null && !access.outputs?.has?.(id)) throw new Error('Unknown Web MIDI output')
      remember(MIDI_OUTPUT_STORAGE_KEY, id)
      await bindOutput(id)
    },
    async stop() {
      stopped = true
      if (frameId !== null) cancelFrame?.(frameId)
      frameId = null
      if (access) access.onstatechange = null
      if (selectedInput) selectedInput.onmidimessage = null
      await selectedInput?.close?.()
      await selectedOutput?.close?.()
      selectedInput = null
      selectedOutput = null
      bridge.disconnect(3)
      bridge.setOutputConnected(false)
      access = null
      publish({
        state: supported ? 'idle' : 'unsupported', error: null,
        inputs: [], outputs: [], inputConnected: false, outputConnected: false,
      })
    },
  })
}
