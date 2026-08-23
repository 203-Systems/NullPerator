export const TRACE_ABI_VERSION = 1
export const TRACE_CATEGORIES = Object.freeze({
  1: 'ui', 2: 'input', 4: 'audio', 8: 'mixer', 16: 'instrument',
  32: 'player', 64: 'files', 128: 'storage', 256: 'midi', 512: 'benchmark',
})
export const TRACE_NAMES = Object.freeze({
  1: 'frame', 2: 'ui.update', 3: 'ui.clock_tick', 4: 'input.retry',
  5: 'input.dispatch', 6: 'audio.producer', 7: 'audio.snapshot',
  8: 'mixer.render', 9: 'instrument.render', 10: 'player.tick',
  11: 'file.open', 12: 'file.read', 13: 'file.write', 14: 'file.scan',
  15: 'storage.mutation', 16: 'midi.poll', 17: 'midi.input',
  18: 'midi.output', 19: 'benchmark.block', 20: 'audio.callback_count',
  21: 'audio.underrun_frames', 22: 'audio.overrun_frames',
  23: 'storage.sync',
  24: 'input.accepted', 25: 'input.presented',
  26: 'input.to_frame_latency_us', 27: 'input.latency_dropped',
  28: 'audio.render_duration_us', 29: 'audio.callback_duration_us',
  30: 'audio.callback_max_duration_us', 31: 'audio.callback_deadline_us',
  32: 'audio.callback_processing_deadline_misses',
  33: 'midi.input_accepted', 34: 'midi.input_latency_us',
  35: 'midi.output_queued', 36: 'midi.output_latency_us',
})
export const TRACE_THREADS = Object.freeze({ 1: 'application', 2: 'browser', 3: 'audio-worklet' })
export const TRACE_PHASES = Object.freeze({ 0: 'B', 1: 'E', 2: 'i', 3: 'C' })
export const TRACE_ALL_MASK = 1023
export const TRACE_STORAGE_MASK = 128
export const TRACE_STORAGE_SYNC_FLAGS = Object.freeze({
  Success: 1,
  Failure: 2,
  Populate: 4,
})
export const TRACE_INPUT_ACTION_MASK = 0x000f
export const TRACE_INPUT_DROP_FLAGS = Object.freeze({
  Overflow: 1 << 8,
  NoPresentation: 1 << 9,
  Coalesced: 1 << 10,
})
export const TRACE_INPUT_TO_FRAME_LATENCY_NAME = 26
export const TRACE_MIDI_INSTANT_NAMES = Object.freeze([33, 35])
export const TRACE_MIDI_LATENCY_NAMES = Object.freeze([34, 36])
// Fixture v1 renders every byte of 32 128-frame stereo blocks through FNV-1a.
// Keep this value shared by the bridge, UI, and acceptance tests so an output
// change cannot be mistaken for a timing-only benchmark regression.
export const TRACE_BENCHMARK_FIXTURE_GOLDEN_32 = 0xc45e4b1c

export function validateTraceRecord(record) {
  if (!TRACE_CATEGORIES[record.category] || !TRACE_NAMES[record.name] ||
      !TRACE_THREADS[record.thread] || !TRACE_PHASES[record.phase]) {
    throw new Error('Trace record contains an unknown registry id')
  }
  return record
}
