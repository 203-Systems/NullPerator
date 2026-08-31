/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _MIDI_NOTE_TRACKER_H_
#define _MIDI_NOTE_TRACKER_H_

#include "Externals/etl/include/etl/array.h"
#include <cstdint>

/**
 * MidiNoteTracker - Tracks active MIDI notes across audio voice slots
 *
 * This class keeps track of which notes are currently active on which channels,
 * allowing for polyphonic note handling where a note is only stopped when
 * all instances of it have been released.
 */
class MidiNoteTracker {
public:
  static constexpr uint8_t kVoiceCount = 8;

  MidiNoteTracker();

  /**
   * Register a note as active on a specific channel
   *
   * @param note The MIDI note number (0-127)
   * @param midiChannel The original MIDI channel (0-15)
   * @param audioChannel The audio voice slot assigned (0-7)
   * @return True if the note was successfully registered
   */
  bool registerNote(uint8_t note, uint8_t midiChannel,
                    uint8_t audioChannel);

  /**
   * Unregister a note on a specific MIDI channel
   *
   * @param note The MIDI note number (0-127)
   * @param midiChannel The MIDI channel (0-15)
   * @return The audio voice slot that should be stopped, or -1 if the note was
   * not tracked
   */
  int unregisterNote(uint8_t note, uint8_t midiChannel);

  /**
   * Get the next available audio voice slot
   *
   * @return The next available audio voice slot, or -1 if all slots are in use
   */
  int getNextAvailableChannel() const;

  /**
   * Return a bit for every audio voice currently owned by live MIDI input.
   *
   * Device lifecycle code uses this snapshot to stop the voices before
   * clearing their note metadata.
   */
  uint8_t activeVoiceMask() const;

  /**
   * Clear all tracked notes
   */
  void clear();

private:
  struct NoteInfo {
    bool active;
    uint8_t midiNote;    // The MIDI note number (0-127)
    uint8_t midiChannel; // The original MIDI channel (0-15)
  };

  // Each player audio voice can own at most one live MIDI note.
  etl::array<NoteInfo, kVoiceCount> playingNotes_;
};

#endif // _MIDI_NOTE_TRACKER_H_
