/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_SHARED_MIDI_OUT_DEVICE_H
#define PICOTRACKER_SHARED_MIDI_OUT_DEVICE_H

#include "Adapters/common/midi/MidiByteQueue.h"
#include "Adapters/common/midi/MidiLatencyTrace.h"
#include "Services/Midi/MidiOutDevice.h"

#include <atomic>
#include <cstddef>
#include <span>

class QueuedMidiOutDevice final : public MidiOutDevice {
public:
  static constexpr std::size_t NormalCapacity = 1024U;
  static constexpr std::size_t RealtimeCapacity = 256U;
  using Queue = MidiPacketQueue<NormalCapacity, RealtimeCapacity>;
  using NowFunction = double (*)();

  explicit QueuedMidiOutDevice(Queue &queue, NowFunction now = nullptr,
                               const char *name = "MIDI output");

  bool Init() override;
  void Close() override;
  bool Start() override;
  void Stop() override;
  void SendMessage(MidiMessage &message) override;
  [[nodiscard]] bool SendMessageAt(MidiMessage &message,
                                   double timestampMilliseconds);

  [[nodiscard]] static MidiPacket Encode(const MidiMessage &message,
                                         double timestampMilliseconds);

  [[nodiscard]] NowFunction Clock() const noexcept { return now_; }

private:
  static double DefaultNowMilliseconds();

  Queue &queue_;
  NowFunction now_;
  std::atomic<bool> running_{false};
};

#endif
