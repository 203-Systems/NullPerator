/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_MIDI_IN_DEVICE_H
#define PICOTRACKER_WASM_MIDI_IN_DEVICE_H

#include "Adapters/wasm/midi/MidiByteQueue.h"
#include "Adapters/wasm/midi/MidiLatencyTrace.h"
#include "Services/Midi/MidiInDevice.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

class WasmMidiInDevice final : public MidiInDevice {
public:
  static constexpr std::size_t QueueCapacity = 4096U;

  explicit WasmMidiInDevice(
      MidiLatencyTrace::NowFunction now = nullptr,
      const char *name = "Web MIDI input");

  bool Start() override;
  void Stop() override;
  void poll() override;

  [[nodiscard]] bool Submit(std::span<const std::uint8_t> bytes,
                            double timestampMilliseconds,
                            const MidiLatencyTrace::Ticket &trace) noexcept;
  void RequestDisconnect() noexcept;
  [[nodiscard]] std::uint64_t DroppedBytes() const noexcept;
  [[nodiscard]] std::uint64_t ProcessedBytes() const noexcept;
  [[nodiscard]] std::uint8_t LastProcessedByte() const noexcept;
  [[nodiscard]] double LastProcessedTimestamp() const noexcept;
  [[nodiscard]] std::uint32_t ResetGeneration() const noexcept;

protected:
  bool initDriver() override;
  void closeDriver() override;
  bool startDriver() override;
  void stopDriver() override;

private:
  void ResetParserOnApplicationThread();

  MidiByteQueue<QueueCapacity> queue_{};
  MidiLatencyTrace::NowFunction now_;
  std::atomic<bool> disconnectRequested_{false};
  std::atomic<std::uint64_t> processedBytes_{0U};
  std::atomic<std::uint64_t> lastProcessedTimestampBits_{0U};
  std::atomic<std::uint32_t> lastProcessedByte_{0U};
  std::atomic<std::uint32_t> resetGeneration_{0U};
};

#endif
