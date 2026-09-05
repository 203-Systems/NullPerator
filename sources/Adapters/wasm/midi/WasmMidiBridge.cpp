/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_MIDI_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_MIDI_EXPORT
#endif

#include "Adapters/common/midi/QueuedMidiService.h"

extern "C" WASM_MIDI_EXPORT std::uintptr_t PicoTracker_Wasm_MidiInputBuffer() {
  auto *service = QueuedMidiService::Instance();
  return service == nullptr
             ? 0U
             : reinterpret_cast<std::uintptr_t>(service->InputStagingData());
}

extern "C" WASM_MIDI_EXPORT std::uint32_t PicoTracker_Wasm_MidiInputCapacity() {
  return static_cast<std::uint32_t>(QueuedMidiService::InputStagingCapacity);
}

extern "C" WASM_MIDI_EXPORT std::uint32_t
PicoTracker_Wasm_MidiInput(const std::uint8_t *bytes, std::size_t size,
                           double timestampMilliseconds) {
  auto *service = QueuedMidiService::Instance();
  return service != nullptr &&
                 service->SubmitInput(bytes, size, timestampMilliseconds)
             ? 1U
             : 0U;
}

extern "C" WASM_MIDI_EXPORT std::uintptr_t PicoTracker_Wasm_MidiDrainOutput() {
  auto *service = QueuedMidiService::Instance();
  return service == nullptr ? 0U : service->DrainOutput();
}

extern "C" WASM_MIDI_EXPORT void
PicoTracker_Wasm_MidiDisconnect(std::uint32_t directions) {
  if (auto *service = QueuedMidiService::Instance())
    service->Disconnect(directions);
}

extern "C" WASM_MIDI_EXPORT void
PicoTracker_Wasm_MidiSetOutputConnected(std::uint32_t connected) {
  if (auto *service = QueuedMidiService::Instance()) {
    service->SetOutputConnected(connected != 0U);
  }
}

extern "C" WASM_MIDI_EXPORT std::uintptr_t
PicoTracker_Wasm_MidiDiagnosticSnapshot() {
  auto *service = QueuedMidiService::Instance();
  return service == nullptr ? 0U : service->DiagnosticSnapshot();
}

extern "C" WASM_MIDI_EXPORT std::uint32_t
PicoTracker_Wasm_MidiDiagnosticOutput(std::uint32_t status, std::uint32_t data1,
                                      std::uint32_t data2,
                                      std::uint32_t delayMilliseconds) {
  auto *service = QueuedMidiService::Instance();
  return service != nullptr &&
                 service->RequestDiagnosticOutput(
                     static_cast<std::uint8_t>(status),
                     static_cast<std::uint8_t>(data1),
                     static_cast<std::uint8_t>(data2), delayMilliseconds)
             ? 1U
             : 0U;
}
