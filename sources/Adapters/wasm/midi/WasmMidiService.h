/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_MIDI_SERVICE_H
#define PICOTRACKER_WASM_MIDI_SERVICE_H

#include "Adapters/wasm/midi/WasmMidiInDevice.h"
#include "Adapters/wasm/midi/WasmMidiOutDevice.h"
#include "Services/Midi/MidiService.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

class WasmMidiService final : public MidiService {
public:
  static constexpr std::size_t InputStagingCapacity = 1024U;
  static constexpr std::size_t DrainCapacity = 128U;
  static constexpr std::size_t DrainHeaderBytes = 24U;
  static constexpr std::size_t DrainRecordBytes = 24U;
  static constexpr std::size_t DiagnosticSnapshotBytes = 32U;
  static constexpr std::uint32_t DisconnectInput = 1U;
  static constexpr std::uint32_t DisconnectOutput = 2U;

  WasmMidiService();
  ~WasmMidiService() override;

  static WasmMidiService *Instance() noexcept;
  void Poll();
  [[nodiscard]] bool SubmitInput(const std::uint8_t *bytes, std::size_t size,
                                 double timestampMilliseconds) noexcept;
  void Disconnect(std::uint32_t directions) noexcept;
  void SetOutputConnected(bool connected) noexcept;

  [[nodiscard]] std::uint8_t *InputStagingData() noexcept;
  [[nodiscard]] std::uintptr_t DrainOutput() noexcept;
  [[nodiscard]] std::uintptr_t DiagnosticSnapshot() noexcept;
  [[nodiscard]] bool RequestDiagnosticOutput(
      std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
      std::uint32_t delayMilliseconds) noexcept;

protected:
  void updateActiveDevicesList(unsigned short config) override;

private:
  void ApplyOutputConnection();
  void ProcessDiagnosticOutput();
  void WriteDrainHeader(std::uint32_t count);

  static std::atomic<WasmMidiService *> instance_;
  WasmMidiInDevice input_{};
  WasmMidiOutDevice::Queue outputQueue_{};
  WasmMidiOutDevice output_;
  alignas(64) std::array<std::uint8_t, InputStagingCapacity> inputStaging_{};
  alignas(8) std::array<std::uint8_t,
                        DrainHeaderBytes + DrainCapacity * DrainRecordBytes>
      drainBuffer_{};
  alignas(8) std::array<std::uint8_t, DiagnosticSnapshotBytes>
      diagnosticSnapshot_{};
  std::atomic<int> requestedOutputConnection_{-1};
  std::atomic<std::uint32_t> diagnosticOutputRequest_{0U};
  bool outputConnected_ = false;
};

extern "C" {
std::uintptr_t PicoTracker_Wasm_MidiInputBuffer();
std::uint32_t PicoTracker_Wasm_MidiInputCapacity();
std::uint32_t PicoTracker_Wasm_MidiInput(const std::uint8_t *bytes,
                                         std::size_t size,
                                         double timestampMilliseconds);
std::uintptr_t PicoTracker_Wasm_MidiDrainOutput();
void PicoTracker_Wasm_MidiDisconnect(std::uint32_t directions);
void PicoTracker_Wasm_MidiSetOutputConnected(std::uint32_t connected);
std::uintptr_t PicoTracker_Wasm_MidiDiagnosticSnapshot();
std::uint32_t PicoTracker_Wasm_MidiDiagnosticOutput(
    std::uint32_t status, std::uint32_t data1, std::uint32_t data2,
    std::uint32_t delayMilliseconds);
}

#endif
