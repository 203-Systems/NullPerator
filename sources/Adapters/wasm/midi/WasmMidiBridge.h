#pragma once
#include <cstddef>
#include <cstdint>
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
std::uint32_t
PicoTracker_Wasm_MidiDiagnosticOutput(std::uint32_t status, std::uint32_t data1,
                                      std::uint32_t data2,
                                      std::uint32_t delayMilliseconds);
}
