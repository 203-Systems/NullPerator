/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Model/Phrase.h"
#include "Application/Model/Song.h"

#include <cstdint>

namespace player_storage {

// Phrase 0xFF is an on-disk/UI sentinel, not the final Phrase storage slot.
// Returning -1 keeps callers from turning it into a one-past-end array offset.
constexpr int PhraseStepOffset(std::uint8_t phrase, int step) {
  if (phrase >= PHRASE_COUNT || step < 0 || step >= STEPS_PER_PHRASE)
    return -1;
  return static_cast<int>(phrase) * STEPS_PER_PHRASE + step;
}

constexpr int SongCellOffset(int row, int channel) {
  if (row < 0 || row >= SONG_ROW_COUNT || channel < 0 ||
      channel >= SONG_CHANNEL_COUNT) {
    return -1;
  }
  return row * SONG_CHANNEL_COUNT + channel;
}

} // namespace player_storage
