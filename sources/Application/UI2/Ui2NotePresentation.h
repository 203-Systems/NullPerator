/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Model/Song.h"
#include "Application/UI2/Ui2FixedText.h"
#include "Application/Utils/char.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ui2 {

// Format the tracker note cell without invoking printf in the 30 Hz capture
// path. HIGHEST_NOTE limits the octave to one digit, including the sign for
// the two negative octaves, so the fixed five-byte cell is always sufficient.
inline void FormatUiNote(std::uint8_t value, std::array<char, 5> &text) {
  text.fill('\0');
  if (value == NO_NOTE) {
    text = {'-', '-', '-', '-', '\0'};
    return;
  }
  if (value == NOTE_OFF) {
    text = {'O', 'F', 'F', '\0', '\0'};
    return;
  }
  if (value > HIGHEST_NOTE) {
    text = {'?', '?', '?', '?', '\0'};
    return;
  }

  const char *pitch = noteNames[value % 12U];
  std::size_t cursor = 0U;
  text[cursor++] = pitch[0];
  if (pitch[1] != ' ') text[cursor++] = pitch[1];
  int octave = static_cast<int>(value / 12U) - 2;
  if (octave < 0) {
    text[cursor++] = '-';
    octave = -octave;
  }
  text[cursor] = static_cast<char>('0' + octave);
}

// Capture bottom-bar notes from the numeric mixer state. Player's legacy
// GetPlayedNote/GetPlayedOctive methods return the same shared character
// buffer, so retaining the first pointer across the second call corrupts the
// pitch. Reading the numeric value is both allocation-free and race-equivalent
// to those accessors without the aliasing hazard.
template <typename PlayerLike, typename Notes>
void CaptureUiTrackNotes(PlayerLike *player, bool playing, Notes &notes) {
  for (std::size_t track = 0U; track < notes.size(); ++track) {
    if (!playing || player == nullptr || player->IsChannelMuted(track)) {
      CopyUiText(notes[track], "--");
      continue;
    }
    const int value = player->GetPlayedNoteValue(track);
    if (value < 0 || value > HIGHEST_NOTE) {
      CopyUiText(notes[track], "--");
      continue;
    }
    FormatUiNote(static_cast<std::uint8_t>(value), notes[track]);
  }
}

// Mixer note telemetry is intentionally empty when audio rendering is
// disabled, but Player still publishes coherent transport state. Fill only
// missing Song Live cells from that snapshot: inactive, empty-chain, muted or
// non-note tracks must stay blank so stale transport bytes cannot appear as
// ghost notes.
template <typename PlayerLike, typename TransportLike, typename Notes>
void CaptureUiLiveTransportFallback(PlayerLike *player, bool liveMode,
                                    bool playing,
                                    const TransportLike &transport,
                                    Notes &notes) {
  if (!liveMode || !playing || player == nullptr)
    return;
  const std::size_t count =
      std::min<std::size_t>(notes.size(), SONG_CHANNEL_COUNT);
  for (std::size_t track = 0U; track < count; ++track) {
    if (notes[track][0] != '-' || player->IsChannelMuted(track) ||
        !transport.IsChannelPlaying(track) || transport.chain[track] == 0xFFU)
      continue;
    const std::uint8_t note = transport.note[track];
    if (note <= HIGHEST_NOTE)
      FormatUiNote(note, notes[track]);
  }
}

} // namespace ui2
