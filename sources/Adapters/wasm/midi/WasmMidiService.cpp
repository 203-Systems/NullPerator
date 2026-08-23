/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_MIDI_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_MIDI_EXPORT
#endif

#include "WasmMidiService.h"

#include "Adapters/wasm/midi/MidiLatencyTrace.h"
#include "Adapters/wasm/tracing/WasmProfiler.h"
#include "Application/Model/Config.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/Variable.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {
template <typename Value>
void Store(std::uint8_t *destination, Value value) {
  static_assert(std::is_trivially_copyable_v<Value>);
  std::memcpy(destination, &value, sizeof(value));
}

std::uint32_t Saturate(std::uint64_t value) {
  return static_cast<std::uint32_t>(
      std::min<std::uint64_t>(value,
                              std::numeric_limits<std::uint32_t>::max()));
}

double NowMilliseconds() {
#ifdef __EMSCRIPTEN__
  return emscripten_get_now();
#else
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(
             Clock::now().time_since_epoch())
      .count();
#endif
}
} // namespace

std::atomic<WasmMidiService *> WasmMidiService::instance_{nullptr};

WasmMidiService::WasmMidiService()
    : input_(&NowMilliseconds), output_(outputQueue_, &NowMilliseconds) {
  inList_.push_back(&input_);
  outList_.push_back(&output_);
  instance_.store(this, std::memory_order_release);
  WriteDrainHeader(0U);
}

WasmMidiService::~WasmMidiService() {
  WasmMidiService *expected = this;
  (void)instance_.compare_exchange_strong(expected, nullptr,
                                          std::memory_order_acq_rel);
}

WasmMidiService *WasmMidiService::Instance() noexcept {
  return instance_.load(std::memory_order_acquire);
}

void WasmMidiService::updateActiveDevicesList(unsigned short config) {
  MidiService::updateActiveDevicesList(config == 0U ? 0U : 1U);
}

bool WasmMidiService::SubmitInput(const std::uint8_t *bytes, std::size_t size,
                                  double timestampMilliseconds) noexcept {
  if (bytes == nullptr || size == 0U || size > InputStagingCapacity) return false;
  const MidiLatencyTrace::Ticket trace =
      MidiLatencyTrace::CaptureInput(&NowMilliseconds);
  if (!input_.Submit(std::span<const std::uint8_t>(bytes, size),
                     timestampMilliseconds, trace)) {
    return false;
  }
  MidiLatencyTrace::PublishInputAccepted(trace);
  return true;
}

void WasmMidiService::Disconnect(std::uint32_t directions) noexcept {
  if ((directions & DisconnectInput) != 0U) input_.RequestDisconnect();
  if ((directions & DisconnectOutput) != 0U) {
    outputQueue_.DiscardConsumer();
    SetOutputConnected(false);
  }
}

void WasmMidiService::SetOutputConnected(bool connected) noexcept {
  requestedOutputConnection_.store(connected ? 1 : 0,
                                   std::memory_order_release);
}

void WasmMidiService::ApplyOutputConnection() {
  const int requested = requestedOutputConnection_.exchange(
      -1, std::memory_order_acq_rel);
  if (requested < 0 || outputConnected_ == (requested != 0)) return;
  outputConnected_ = requested != 0;
  Config *config = Config::GetInstance();
  Variable *device = config == nullptr
                         ? nullptr
                         : config->FindVariable(FourCC::VarMidiDevice);
  if (device != nullptr) device->SetInt(outputConnected_ ? 1 : 0);
}

void WasmMidiService::Poll() {
  WASM_TRACE_SCOPE(WasmTraceCategory::Midi, WasmTraceName::MidiPoll);
  ApplyOutputConnection();
  ProcessDiagnosticOutput();
  input_.poll();
}

bool WasmMidiService::RequestDiagnosticOutput(
    std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
    std::uint32_t delayMilliseconds) noexcept {
  if (status < 0x80U || delayMilliseconds > 1000U) return false;
  const std::uint32_t request =
      static_cast<std::uint32_t>(status) |
      (static_cast<std::uint32_t>(data1 & 0x7FU) << 8U) |
      (static_cast<std::uint32_t>(data2 & 0x7FU) << 15U) |
      (delayMilliseconds << 22U);
  std::uint32_t expected = 0U;
  return diagnosticOutputRequest_.compare_exchange_strong(
      expected, request, std::memory_order_release, std::memory_order_relaxed);
}

void WasmMidiService::ProcessDiagnosticOutput() {
  const std::uint32_t request =
      diagnosticOutputRequest_.exchange(0U, std::memory_order_acq_rel);
  if (request == 0U || !outputConnected_) return;
  MidiMessage message;
  message.status_ = static_cast<std::uint8_t>(request & 0xFFU);
  message.data1_ = static_cast<std::uint8_t>((request >> 8U) & 0x7FU);
  message.data2_ = static_cast<std::uint8_t>((request >> 15U) & 0x7FU);
  const auto delayMilliseconds = static_cast<double>(request >> 22U);
  (void)output_.SendMessageAt(message, NowMilliseconds() + delayMilliseconds);
}

std::uint8_t *WasmMidiService::InputStagingData() noexcept {
  return inputStaging_.data();
}

void WasmMidiService::WriteDrainHeader(std::uint32_t count) {
  Store<std::uint32_t>(drainBuffer_.data() + 0U, 1U);
  Store<std::uint32_t>(drainBuffer_.data() + 4U,
                       static_cast<std::uint32_t>(DrainHeaderBytes));
  Store<std::uint32_t>(drainBuffer_.data() + 8U,
                       static_cast<std::uint32_t>(DrainRecordBytes));
  Store<std::uint32_t>(drainBuffer_.data() + 12U, count);
  Store<std::uint32_t>(drainBuffer_.data() + 16U,
                       Saturate(outputQueue_.DroppedNormal()));
  Store<std::uint32_t>(drainBuffer_.data() + 20U,
                       Saturate(outputQueue_.DroppedRealtime()));
}

std::uintptr_t WasmMidiService::DrainOutput() noexcept {
  std::array<MidiPacket, DrainCapacity> packets{};
  const std::size_t count = outputQueue_.Pop(packets);
  WriteDrainHeader(static_cast<std::uint32_t>(count));
  for (std::size_t index = 0; index < count; ++index) {
    const MidiPacket &packet = packets[index];
    // Settle queue hand-off before JavaScript calls MIDIOutput.send(). The
    // packet's future scheduled timestamp is deliberately not consulted.
    MidiLatencyTrace::OutputDrained(packet, &NowMilliseconds);
    std::uint8_t *record = drainBuffer_.data() + DrainHeaderBytes +
                           index * DrainRecordBytes;
    Store<std::uint64_t>(record + 0U, packet.sequence);
    Store<double>(record + 8U, packet.timestampMilliseconds);
    record[16] = packet.length;
    record[17] = packet.bytes[0];
    record[18] = packet.bytes[1];
    record[19] = packet.bytes[2];
    Store<std::uint32_t>(record + 20U, 0U);
  }
  return reinterpret_cast<std::uintptr_t>(drainBuffer_.data());
}

std::uintptr_t WasmMidiService::DiagnosticSnapshot() noexcept {
  Store<std::uint32_t>(diagnosticSnapshot_.data() + 0U, 1U);
  Store<std::uint32_t>(diagnosticSnapshot_.data() + 4U,
                       static_cast<std::uint32_t>(DiagnosticSnapshotBytes));
  Store<std::uint64_t>(diagnosticSnapshot_.data() + 8U,
                       input_.ProcessedBytes());
  Store<double>(diagnosticSnapshot_.data() + 16U,
                input_.LastProcessedTimestamp());
  Store<std::uint32_t>(diagnosticSnapshot_.data() + 24U,
                       input_.LastProcessedByte());
  Store<std::uint32_t>(diagnosticSnapshot_.data() + 28U,
                       input_.ResetGeneration());
  return reinterpret_cast<std::uintptr_t>(diagnosticSnapshot_.data());
}

extern "C" WASM_MIDI_EXPORT std::uintptr_t
PicoTracker_Wasm_MidiInputBuffer() {
  auto *service = WasmMidiService::Instance();
  return service == nullptr
             ? 0U
             : reinterpret_cast<std::uintptr_t>(service->InputStagingData());
}

extern "C" WASM_MIDI_EXPORT std::uint32_t
PicoTracker_Wasm_MidiInputCapacity() {
  return static_cast<std::uint32_t>(WasmMidiService::InputStagingCapacity);
}

extern "C" WASM_MIDI_EXPORT std::uint32_t PicoTracker_Wasm_MidiInput(
    const std::uint8_t *bytes, std::size_t size,
    double timestampMilliseconds) {
  auto *service = WasmMidiService::Instance();
  return service != nullptr &&
                 service->SubmitInput(bytes, size, timestampMilliseconds)
             ? 1U
             : 0U;
}

extern "C" WASM_MIDI_EXPORT std::uintptr_t
PicoTracker_Wasm_MidiDrainOutput() {
  auto *service = WasmMidiService::Instance();
  return service == nullptr ? 0U : service->DrainOutput();
}

extern "C" WASM_MIDI_EXPORT void
PicoTracker_Wasm_MidiDisconnect(std::uint32_t directions) {
  if (auto *service = WasmMidiService::Instance()) service->Disconnect(directions);
}

extern "C" WASM_MIDI_EXPORT void
PicoTracker_Wasm_MidiSetOutputConnected(std::uint32_t connected) {
  if (auto *service = WasmMidiService::Instance()) {
    service->SetOutputConnected(connected != 0U);
  }
}

extern "C" WASM_MIDI_EXPORT std::uintptr_t
PicoTracker_Wasm_MidiDiagnosticSnapshot() {
  auto *service = WasmMidiService::Instance();
  return service == nullptr ? 0U : service->DiagnosticSnapshot();
}

extern "C" WASM_MIDI_EXPORT std::uint32_t
PicoTracker_Wasm_MidiDiagnosticOutput(std::uint32_t status,
                                      std::uint32_t data1,
                                      std::uint32_t data2,
                                      std::uint32_t delayMilliseconds) {
  auto *service = WasmMidiService::Instance();
  return service != nullptr &&
                 service->RequestDiagnosticOutput(
                     static_cast<std::uint8_t>(status),
                     static_cast<std::uint8_t>(data1),
                     static_cast<std::uint8_t>(data2), delayMilliseconds)
             ? 1U
             : 0U;
}
