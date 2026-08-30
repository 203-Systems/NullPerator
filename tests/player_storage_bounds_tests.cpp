/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/Player/PlayerStorageBounds.h"

#include "doctest/doctest.h"

TEST_CASE("Player rejects empty phrase sentinels before storage access") {
  CHECK(player_storage::PhraseStepOffset(0U, 0) == 0);
  CHECK(player_storage::PhraseStepOffset(PHRASE_COUNT - 1U,
                                         STEPS_PER_PHRASE - 1) ==
        PHRASE_COUNT * STEPS_PER_PHRASE - 1);

  CHECK(player_storage::PhraseStepOffset(0xFFU, 0) == -1);
  CHECK(player_storage::PhraseStepOffset(0U, -1) == -1);
  CHECK(player_storage::PhraseStepOffset(0U, STEPS_PER_PHRASE) == -1);
}

TEST_CASE("Player rejects the song row after the final addressable row") {
  CHECK(player_storage::SongCellOffset(0, 0) == 0);
  CHECK(player_storage::SongCellOffset(SONG_ROW_COUNT - 1,
                                       SONG_CHANNEL_COUNT - 1) ==
        SONG_ROW_COUNT * SONG_CHANNEL_COUNT - 1);

  CHECK(player_storage::SongCellOffset(SONG_ROW_COUNT, 0) == -1);
  CHECK(player_storage::SongCellOffset(-1, 0) == -1);
  CHECK(player_storage::SongCellOffset(0, SONG_CHANNEL_COUNT) == -1);
}
