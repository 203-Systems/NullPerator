/* Bounded mono capture. The committed prefix is immutable until disposal. */
class TrackerMicrophone extends AudioWorkletProcessor {
  constructor(options) {
    super()
    this.samples = new Float32Array(options.processorOptions.samples)
    this.control = new Int32Array(options.processorOptions.control)
    this.count = 0
  }
  process(inputs) {
    if (Atomics.load(this.control, 2)) return false
    const channels = inputs[0]
    if (!channels?.length) return true
    const count = Math.min(channels[0].length, this.samples.length - this.count)
    let peak = 0
    for (let frame = 0; frame < count; frame++) {
      let value = 0
      for (let channel = 0; channel < channels.length; channel++) value += channels[channel][frame]
      value = Math.max(-1, Math.min(1, value / channels.length))
      this.samples[this.count + frame] = value
      peak = Math.max(peak, Math.abs(value))
    }
    this.count += count
    Atomics.store(this.control, 1, Math.round(peak * 32767))
    Atomics.store(this.control, 0, this.count)
    if (this.count === this.samples.length) {
      this.port.postMessage('full')
      return false
    }
    return true
  }
}
registerProcessor('tracker-microphone', TrackerMicrophone)
