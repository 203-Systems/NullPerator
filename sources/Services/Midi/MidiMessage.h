/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#pragma once

#include "Foundation/Observable.h"
#include "Foundation/Types/Types.h"
#include <cstddef>

namespace midi_queue_budget {

// A tracker voice can send its base note plus four chord notes. At a playback
// boundary, the current time slice may already contain one realtime clock. A
// full note-off batch, one All Notes Off CC for every MIDI protocol channel,
// and the final transport message must still fit after it.
inline constexpr std::size_t kTrackerChannelCount = 8U;
inline constexpr std::size_t kChordNotesPerTrack = 4U;
inline constexpr std::size_t kBaseNotesPerTrack = 1U;
inline constexpr std::size_t kNotesPerTrack =
    kBaseNotesPerTrack + kChordNotesPerTrack;
inline constexpr std::size_t kFullNoteBatch =
    kTrackerChannelCount * kNotesPerTrack;
inline constexpr std::size_t kMidiProtocolChannelCount = 16U;
inline constexpr std::size_t kRealtimeMessages = 1U;
inline constexpr std::size_t kTransportMessages = 1U;
inline constexpr std::size_t kSetupMessagesPerInstrument = 2U;
inline constexpr std::size_t kPlaybackStartMessages =
    kRealtimeMessages + kFullNoteBatch + kTransportMessages;
inline constexpr std::size_t kPlaybackStopMessages =
    kRealtimeMessages + kFullNoteBatch + kMidiProtocolChannelCount +
    kTransportMessages;
inline constexpr std::size_t kPlaybackBoundaryMessages =
    kPlaybackStartMessages > kPlaybackStopMessages ? kPlaybackStartMessages
                                                   : kPlaybackStopMessages;

static_assert(kPlaybackBoundaryMessages >= kPlaybackStartMessages);
static_assert(kPlaybackBoundaryMessages >= kPlaybackStopMessages);

} // namespace midi_queue_budget

inline constexpr std::size_t MIDI_MAX_MESG_QUEUE =
    midi_queue_budget::kPlaybackBoundaryMessages;

struct MidiMessage : public I_ObservableData {
  enum Type {
    MIDI_NOTE_OFF = 0x80,
    MIDI_NOTE_ON = 0x90,
    MIDI_AFTERTOUCH = 0xA0,
    MIDI_CONTROL_CHANGE = 0xB0,
    MIDI_PROGRAM_CHANGE = 0xC0,
    MIDI_CHANNEL_AFTERTOUCH = 0xD0,
    MIDI_PITCH_BEND = 0xE0,
    MIDI_CHANNEL_PRESSURE = 0xD0,
    MIDI_POLY_PRESSURE = 0xA0,
    MIDI_CLOCK = 0xF8,
    MIDI_START = 0xFA,
    MIDI_CONTINUE = 0xFB,
    MIDI_STOP = 0xFC,
    MIDI_ACTIVE_SENSING = 0xFE,
    MIDI_SYSTEM_RESET = 0xFF,
  };

  static const unsigned char UNUSED_BYTE = 255;

  MidiMessage(unsigned char status = UNUSED_BYTE,
              unsigned char data1 = UNUSED_BYTE,
              unsigned char data2 = UNUSED_BYTE)
      : status_(status), data1_(data1), data2_(data2){};

  //----------------------------------------------------------------------------

  inline MidiMessage::Type GetType() {
    return (MidiMessage::Type)(status_ & 0xF0);
  }

  //----------------------------------------------------------------------------

  unsigned char status_;
  unsigned char data1_;
  unsigned char data2_;
};

// MIDI Control Change numbers used throughout the firmware
enum MidiCC {
  CC_VOLUME = 0x07,
  // All Notes Off (CC 123) – clears any lingering notes on a channel
  CC_ALL_NOTES_OFF = 0x7B
};
