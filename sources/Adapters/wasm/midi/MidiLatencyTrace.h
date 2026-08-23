/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_MIDI_LATENCY_TRACE_H
#define PICOTRACKER_WASM_MIDI_LATENCY_TRACE_H

#include "Adapters/wasm/midi/MidiByteQueue.h"

#include <cstdint>

class MidiLatencyTrace {
public:
  using NowFunction = double (*)();

  struct Ticket {
    double timestampMilliseconds = 0.0;
    std::uint32_t generation = 0U;
    std::uint16_t correlation = 0U;
  };

  // Capture does not call the clock when MIDI tracing is disabled. Input and
  // output use independent non-zero 16-bit correlation spaces.
  [[nodiscard]] static Ticket CaptureInput(NowFunction now) noexcept;
  [[nodiscard]] static Ticket CaptureOutput(NowFunction now) noexcept;

  static void PublishInputAccepted(const Ticket &ticket) noexcept;
  static void PublishOutputQueued(const Ticket &ticket) noexcept;
  static void InputProcessed(const MidiByteRecord &record,
                             NowFunction now) noexcept;
  static void OutputDrained(const MidiPacket &packet,
                            NowFunction now) noexcept;

  [[nodiscard]] static std::uint64_t
  TimestampMicroseconds(double timestampMilliseconds) noexcept;
  [[nodiscard]] static std::uint32_t
  LatencyMicroseconds(double startMilliseconds,
                      double endMilliseconds) noexcept;

  static void ResetCorrelationsForTesting(std::uint32_t inputSeed = 0U,
                                          std::uint32_t outputSeed = 0U)
      noexcept;
};

#endif
