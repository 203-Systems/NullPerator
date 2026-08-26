/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef PICOTRACKER_WASM_TRACE_RECORD_H
#define PICOTRACKER_WASM_TRACE_RECORD_H

#include <cstdint>
#include <type_traits>

enum class WasmTraceCategory : std::uint16_t {
  Ui = 1U << 0, Input = 1U << 1, Audio = 1U << 2, Mixer = 1U << 3,
  Instrument = 1U << 4, Player = 1U << 5, Files = 1U << 6,
  Storage = 1U << 7, Midi = 1U << 8, Benchmark = 1U << 9,
};

enum class WasmTraceName : std::uint16_t {
  Frame = 1, UiUpdate, ClockTick, InputRetry, InputDispatch, AudioProducer,
  AudioSnapshot, MixerRender, InstrumentRender, PlayerTick, FileOpen, FileRead,
  FileWrite, FileScan, StorageMutation, MidiPoll, MidiInput, MidiOutput,
  BenchmarkBlock, AudioCallbackCount = 20, AudioUnderrunFrames = 21,
  AudioOverrunFrames = 22, StorageSync = 23, InputAccepted = 24,
  InputPresented = 25, InputToFrameLatencyUs = 26,
  InputLatencyDropped = 27, AudioRenderDurationUs = 28,
  AudioCallbackDurationUs = 29, AudioCallbackMaxDurationUs = 30,
  AudioCallbackDeadlineUs = 31,
  AudioCallbackProcessingDeadlineMisses = 32,
  MidiInputAccepted = 33,
  MidiInputLatencyUs = 34,
  MidiOutputQueued = 35,
  MidiOutputLatencyUs = 36,
};

enum class WasmTraceFlag : std::uint16_t {
  None = 0,
  Success = 1U << 0,
  Failure = 1U << 1,
  Populate = 1U << 2,
  // Input event flags are name-specific. Low four bits retain the semantic
  // TrackerAction id; these high bits explain why an accepted latency ticket
  // was retired.
  InputOverflow = 1U << 8,
  InputNoPresentation = 1U << 9,
  InputCoalesced = 1U << 10,
};

enum class WasmTracePhase : std::uint8_t { Begin = 0, End = 1, Instant = 2, Counter = 3 };
enum class WasmTraceThread : std::uint8_t { Application = 1, Browser = 2, AudioWorklet = 3 };

struct TraceRecord {
  std::uint64_t sequence = 0;
  std::uint64_t timestampUs = 0;
  std::uint32_t value = 0;
  std::uint32_t generation = 0;
  WasmTraceCategory category = WasmTraceCategory::Ui;
  WasmTraceName name = WasmTraceName::Frame;
  WasmTracePhase phase = WasmTracePhase::Instant;
  WasmTraceThread thread = WasmTraceThread::Application;
  std::uint16_t flags = 0;
};

static_assert(sizeof(TraceRecord) == 32);
static_assert(std::is_trivially_copyable_v<TraceRecord>);

#endif
