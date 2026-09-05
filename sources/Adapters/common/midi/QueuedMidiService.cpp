#include "QueuedMidiService.h"

#include "Adapters/common/midi/MidiLatencyTrace.h"
#include "Application/Model/Config.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/Variable.h"
#include "System/Console/Profiler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {
template <typename Value> void Store(std::uint8_t *destination, Value value) {
  static_assert(std::is_trivially_copyable_v<Value>);
  std::memcpy(destination, &value, sizeof(value));
}

std::uint32_t Saturate(std::uint64_t value) {
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      value, std::numeric_limits<std::uint32_t>::max()));
}

double NowMilliseconds() {
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(
             Clock::now().time_since_epoch())
      .count();
}
} // namespace

std::atomic<QueuedMidiService *> QueuedMidiService::instance_{nullptr};

QueuedMidiService::QueuedMidiService(NowFunction now, const char *inputName,
                                     const char *outputName)
    : now_(now ? now : &NowMilliseconds), input_(now_, inputName),
      output_(outputQueue_, now_, outputName) {
  inList_.push_back(&input_);
  outList_.push_back(&output_);
  instance_.store(this, std::memory_order_release);
  WriteDrainHeader(0U);
}

QueuedMidiService::~QueuedMidiService() {
  QueuedMidiService *expected = this;
  (void)instance_.compare_exchange_strong(expected, nullptr,
                                          std::memory_order_acq_rel);
}

QueuedMidiService *QueuedMidiService::Instance() noexcept {
  return instance_.load(std::memory_order_acquire);
}

void QueuedMidiService::updateActiveDevicesList(unsigned short config) {
  MidiService::updateActiveDevicesList(config == 0U ? 0U : 1U);
}

bool QueuedMidiService::SubmitInput(const std::uint8_t *bytes, std::size_t size,
                                    double timestampMilliseconds) noexcept {
  if (bytes == nullptr || size == 0U || size > InputStagingCapacity)
    return false;
  const MidiLatencyTrace::Ticket trace = MidiLatencyTrace::CaptureInput(now_);
  if (!input_.Submit(std::span<const std::uint8_t>(bytes, size),
                     timestampMilliseconds, trace)) {
    return false;
  }
  MidiLatencyTrace::PublishInputAccepted(trace);
  return true;
}

void QueuedMidiService::Disconnect(std::uint32_t directions) noexcept {
  if ((directions & DisconnectInput) != 0U)
    input_.RequestDisconnect();
  if ((directions & DisconnectOutput) != 0U) {
    outputQueue_.DiscardConsumer();
    SetOutputConnected(false);
  }
}

void QueuedMidiService::SetOutputConnected(bool connected) noexcept {
  requestedOutputConnection_.store(connected ? 1 : 0,
                                   std::memory_order_release);
}

void QueuedMidiService::ApplyOutputConnection() {
  const int requested =
      requestedOutputConnection_.exchange(-1, std::memory_order_acq_rel);
  if (requested < 0 || outputConnected_ == (requested != 0))
    return;
  outputConnected_ = requested != 0;
  Config *config = Config::GetInstance();
  Variable *device =
      config == nullptr ? nullptr : config->FindVariable(FourCC::VarMidiDevice);
  if (device != nullptr)
    device->SetInt(outputConnected_ ? 1 : 0);
}

void QueuedMidiService::Poll() {
  PROFILE_SCOPE(TraceCategory::Midi, TraceName::MidiPoll);
  ApplyOutputConnection();
  ProcessDiagnosticOutput();
  input_.poll();
}

bool QueuedMidiService::RequestDiagnosticOutput(
    std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
    std::uint32_t delayMilliseconds) noexcept {
  if (status < 0x80U || delayMilliseconds > 1000U)
    return false;
  const std::uint32_t request =
      static_cast<std::uint32_t>(status) |
      (static_cast<std::uint32_t>(data1 & 0x7FU) << 8U) |
      (static_cast<std::uint32_t>(data2 & 0x7FU) << 15U) |
      (delayMilliseconds << 22U);
  std::uint32_t expected = 0U;
  return diagnosticOutputRequest_.compare_exchange_strong(
      expected, request, std::memory_order_release, std::memory_order_relaxed);
}

void QueuedMidiService::ProcessDiagnosticOutput() {
  const std::uint32_t request =
      diagnosticOutputRequest_.exchange(0U, std::memory_order_acq_rel);
  if (request == 0U || !outputConnected_)
    return;
  MidiMessage message;
  message.status_ = static_cast<std::uint8_t>(request & 0xFFU);
  message.data1_ = static_cast<std::uint8_t>((request >> 8U) & 0x7FU);
  message.data2_ = static_cast<std::uint8_t>((request >> 15U) & 0x7FU);
  const auto delayMilliseconds = static_cast<double>(request >> 22U);
  (void)output_.SendMessageAt(message, now_() + delayMilliseconds);
}

std::uint8_t *QueuedMidiService::InputStagingData() noexcept {
  return inputStaging_.data();
}

void QueuedMidiService::WriteDrainHeader(std::uint32_t count) {
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

std::uintptr_t QueuedMidiService::DrainOutput() noexcept {
  std::array<MidiPacket, DrainCapacity> packets{};
  const std::size_t count = outputQueue_.Pop(packets);
  WriteDrainHeader(static_cast<std::uint32_t>(count));
  for (std::size_t index = 0; index < count; ++index) {
    const MidiPacket &packet = packets[index];
    // Settle queue hand-off before the host submits the packet. The
    // packet's future scheduled timestamp is deliberately not consulted.
    MidiLatencyTrace::OutputDrained(packet, now_);
    std::uint8_t *record =
        drainBuffer_.data() + DrainHeaderBytes + index * DrainRecordBytes;
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

std::uintptr_t QueuedMidiService::DiagnosticSnapshot() noexcept {
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
