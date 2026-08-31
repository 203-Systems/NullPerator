/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "MidiNoteTracker.h"
#include "System/Console/Trace.h"

MidiNoteTracker::MidiNoteTracker() {
  // Initialize all note tracking data
  clear();
}

bool MidiNoteTracker::registerNote(uint8_t note, uint8_t midiChannel,
                                   uint8_t audioChannel) {
  // Validate parameters
  if (note > 127 || midiChannel > 15 ||
      audioChannel >= playingNotes_.size()) {
    Trace::Debug("Invalid parameters in registerNote: note=%d, midiChannel=%d, "
                 "audioChannel=%d",
                 note, midiChannel, audioChannel);
    return false;
  }

  NoteInfo &activeNote = playingNotes_[audioChannel];
  if (activeNote.active) {
    return false;
  }

  activeNote.active = true;
  activeNote.midiNote = note;
  activeNote.midiChannel = midiChannel;

  Trace::Debug("Note %d registered on MIDI channel %d, audio channel %d", note,
               midiChannel, audioChannel);
  return true;
}

int MidiNoteTracker::getNextAvailableChannel() const {
  // Find an inactive audio voice slot.
  for (size_t i = 0; i < playingNotes_.size(); i++) {
    if (!playingNotes_[i].active) {
      return static_cast<int>(i);
    }
  }

  // All audio voice slots are active.
  return -1;
}

uint8_t MidiNoteTracker::activeVoiceMask() const {
  uint8_t mask = 0U;
  for (size_t i = 0; i < playingNotes_.size(); ++i) {
    if (playingNotes_[i].active) {
      mask |= static_cast<uint8_t>(1U << i);
    }
  }
  return mask;
}

int MidiNoteTracker::unregisterNote(uint8_t note, uint8_t midiChannel) {
  // Find the note in the active notes list
  for (size_t i = 0; i < playingNotes_.size(); i++) {
    auto &activeNote = playingNotes_[i];
    if (activeNote.active && activeNote.midiNote == note &&
        activeNote.midiChannel == midiChannel) {
      // Found the note, mark it as inactive
      int audioChannel = static_cast<int>(i); // Audio channel = array index
      activeNote.active = false;
      Trace::Debug("Note %d unregistered from MIDI channel %d, stopping "
                   "audio channel %d",
                   note, midiChannel, audioChannel);
      return audioChannel;
    }
  }

  // Note not found
  Trace::Debug("Note %d not found on MIDI channel %d", note, midiChannel);
  return -1;
}

void MidiNoteTracker::clear() {
  // Mark all notes as inactive
  for (auto &note : playingNotes_) {
    note.active = false;
  }
}
