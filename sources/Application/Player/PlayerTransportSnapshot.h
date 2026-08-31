/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Model/Song.h"
#include "Application/Session/PlayMode.h"

#include <cstddef>
#include <cstdint>

enum SequencerMode : std::uint8_t { SM_SONG, SM_LIVE };

enum QueueingMode : std::uint8_t {
  QM_NONE,
  QM_CHAINSTART,
  QM_PHRASESTART,
  QM_CHAINSTOP,
  QM_PHRASESTOP,
  QM_TICKSTART
};

// Fixed-size, allocation-free transport state published by Player. UI and
// diagnostics consume one coherent value instead of walking live
// Song/Chain/Phrase cursors across the application/audio core boundary.
struct PlayerTransportSnapshot final {
  bool running = false;
  std::uint8_t channelPlayingMask = 0U;
  SequencerMode sequencerMode = SM_SONG;
  // Keep the object representation padding-free for atomic-word publication.
  std::uint8_t reserved = 0U;
  PlayMode mode = PM_SONG;
  std::uint32_t liveQueueElapsedMs = 0U;
  int songRow[SONG_CHANNEL_COUNT]{};
  unsigned char note[SONG_CHANNEL_COUNT]{};
  unsigned char chain[SONG_CHANNEL_COUNT]{};
  std::int8_t chainRow[SONG_CHANNEL_COUNT]{};
  unsigned char phrase[SONG_CHANNEL_COUNT]{};
  std::int8_t phraseRow[SONG_CHANNEL_COUNT]{};
  QueueingMode queueMode[SONG_CHANNEL_COUNT]{};
  unsigned char queueSongRow[SONG_CHANNEL_COUNT]{};
  unsigned char queueChainRow[SONG_CHANNEL_COUNT]{};

  [[nodiscard]] bool IsChannelPlaying(std::size_t channel) const noexcept {
    return channel < SONG_CHANNEL_COUNT &&
           (channelPlayingMask & (std::uint8_t{1U} << channel)) != 0U;
  }
};

static_assert(SONG_CHANNEL_COUNT <= 8,
              "channel-playing mask must cover every player channel");
static_assert(sizeof(PlayerTransportSnapshot) <= 128U,
              "transport snapshot must remain embedded-friendly");
