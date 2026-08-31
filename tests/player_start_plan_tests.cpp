#include "Application/Player/PlayerStartPlan.h"
#include "Application/Player/PlayerTransportPolicy.h"

#include <doctest/doctest.h>
#include <limits>

namespace {
constexpr int kChannelCount = 8;
constexpr int kChainPositionCount = 16;
} // namespace

TEST_CASE("context playback prefers the requested track and chain row") {
  const PlayerStartPlan plan =
      ResolvePlayerStartPlan<kChannelCount, kChainPositionCount>(
          PM_PHRASE, false, false, 2, 11, /*fallbackChannel=*/6,
          /*fallbackChainPosition=*/4);

  CHECK(plan.mode == PM_PHRASE);
  CHECK_FALSE(plan.resumeLastSongPosition);
  CHECK_FALSE(plan.stopAtEnd);
  CHECK(plan.contextChannel == 2);
  CHECK(plan.contextChainPosition == 11);
}

TEST_CASE("context playback falls back only when no context was supplied") {
  const PlayerStartPlan plan =
      ResolvePlayerStartPlan<kChannelCount, kChainPositionCount>(
          PM_CHAIN, false, false, -1, -1, /*fallbackChannel=*/6,
          /*fallbackChainPosition=*/4);

  CHECK(plan.mode == PM_CHAIN);
  CHECK(plan.contextChannel == 6);
  CHECK(plan.contextChainPosition == 4);
}

TEST_CASE("start planning clamps all context to embedded storage bounds") {
  const PlayerStartPlan plan =
      ResolvePlayerStartPlan<kChannelCount, kChainPositionCount>(
          PM_AUDITION, false, false, 99, 99, /*fallbackChannel=*/-3,
          /*fallbackChainPosition=*/-8);

  CHECK(plan.contextChannel == kChannelCount - 1);
  CHECK(plan.contextChainPosition == kChainPositionCount - 1);
}

TEST_CASE("context start forwarding retains stop-at-end and clamps location") {
  const PlayerStartPlan plan =
      ResolveContextStartPlan<kChannelCount, kChainPositionCount>(
          PM_PHRASE, false, true, /*from=*/99U, /*chainPosition=*/0xFFU);

  CHECK(plan.mode == PM_PHRASE);
  CHECK_FALSE(plan.resumeLastSongPosition);
  CHECK(plan.stopAtEnd);
  CHECK(plan.contextChannel == kChannelCount - 1);
  CHECK(plan.contextChainPosition == kChainPositionCount - 1);
}

TEST_CASE("context start clamps before narrowing an unsigned track") {
  const PlayerStartPlan plan =
      ResolveContextStartPlan<kChannelCount, kChainPositionCount>(
          PM_CHAIN, false, false,
          /*from=*/std::numeric_limits<unsigned int>::max(),
          /*chainPosition=*/0U);

  CHECK(plan.contextChannel == kChannelCount - 1);
}

TEST_CASE("resume requests keep stop-at-end while resolving to song mode") {
  const PlayerStartPlan plan =
      ResolveContextStartPlan<kChannelCount, kChainPositionCount>(
          PM_CHAIN, true, true, /*from=*/3U, /*chainPosition=*/7U);

  CHECK(plan.mode == PM_SONG);
  CHECK(plan.resumeLastSongPosition);
  CHECK(plan.stopAtEnd);
  CHECK(plan.contextChannel == 3);
  CHECK(plan.contextChainPosition == 7);
}

TEST_CASE("song transport bounds and orders its inclusive track selection") {
  const SongTrackRange unchanged =
      NormalizeSongTrackRange<kChannelCount>(2U, 5U);
  CHECK(unchanged.first == 2U);
  CHECK(unchanged.last == 5U);

  const SongTrackRange reversed =
      NormalizeSongTrackRange<kChannelCount>(6U, 1U);
  CHECK(reversed.first == 1U);
  CHECK(reversed.last == 6U);

  const SongTrackRange oversized = NormalizeSongTrackRange<kChannelCount>(
      std::numeric_limits<unsigned int>::max(), 99U);
  CHECK(oversized.first == kChannelCount - 1U);
  CHECK(oversized.last == kChannelCount - 1U);
}
