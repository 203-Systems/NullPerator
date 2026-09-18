// Android owns the ports and MIDI byte transport. Poll only route metadata for
// the shared settings UI; audio/MIDI timing never depends on WebView frames.
export function createAndroidMidiStore(sendNative, options = {}) {
  const schedule = options.setTimeout ?? globalThis.setTimeout
  const cancel = options.clearTimeout ?? globalThis.clearTimeout
  const listeners = new Set()
  let snapshot = Object.freeze({ state: 'idle', error: null, inputs: [], outputs: [],
    selectedInputId: null, selectedOutputId: null, inputConnected: false, outputConnected: false })
  let timer = null
  let generation = 0
  let queue = Promise.resolve()
  const publish = next => {
    snapshot = Object.freeze({ ...snapshot, ...next })
    for (const listener of listeners) listener(snapshot)
  }
  const enqueue = work => { const result = queue.then(work, work); queue = result.catch(() => {}); return result }
  const command = async (name, payload = {}, epoch = generation) => {
    const result = await sendNative(name, payload)
    if (epoch === generation) publish(result)
    return snapshot
  }
  const poll = epoch => {
    timer = schedule(() => {
      timer = null
      void enqueue(async () => {
        if (epoch !== generation) return
        try { await command('midiRefresh', {}, epoch) }
        catch (error) { if (epoch === generation) publish({ error: error.message }) }
        if (epoch === generation) poll(epoch)
      })
    }, 500)
  }
  return Object.freeze({
    subscribe(listener) { listeners.add(listener); listener(snapshot); return () => listeners.delete(listener) },
    snapshot: () => snapshot,
    requestMidiAccess() {
      const epoch = ++generation
      if (timer !== null) cancel(timer)
      timer = null
      return enqueue(async () => {
        if (epoch !== generation) return snapshot
        publish({ state: 'requesting', error: null })
        try {
          await command('midiAccess', {}, epoch)
          if (epoch === generation) poll(epoch)
          return snapshot
        } catch (error) { if (epoch === generation) publish({ state: 'failed', error: error.message }); throw error }
      })
    },
    selectMidiInput(id) { return enqueue(() => command('midiSelectInput', { port: id })) },
    selectMidiOutput(id) { return enqueue(() => command('midiSelectOutput', { port: id })) },
    stop() {
      ++generation
      if (timer !== null) cancel(timer)
      timer = null
      return enqueue(async () => { await command('midiStop'); publish({ state: 'idle', inputConnected: false, outputConnected: false }) })
    },
  })
}
