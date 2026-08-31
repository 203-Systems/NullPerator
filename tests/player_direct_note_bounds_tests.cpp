#include "Application/Player/PlayerDirectNoteBounds.h"

#include "doctest/doctest.h"

TEST_CASE("direct notes reject targets outside fixed player storage") {
  using player_direct_note::IsPlayableTarget;

  CHECK(IsPlayableTarget(0U, 0U));
  CHECK(IsPlayableTarget(MAX_INSTRUMENT_COUNT - 1U, 0x7FU));
  CHECK_FALSE(IsPlayableTarget(MAX_INSTRUMENT_COUNT, 60U));
  CHECK_FALSE(IsPlayableTarget(0xFFFFU, 60U));
  CHECK_FALSE(IsPlayableTarget(0U, 0x80U));
  CHECK_FALSE(IsPlayableTarget(0U, 0xFFU));
}
