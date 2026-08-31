#include "Application/Audio/RecordStreamer.h"

#include "doctest/doctest.h"

#include <array>
#include <cstdint>

TEST_CASE("record streamer rejects buffers that cannot be rendered") {
  RecordStreamer streamer;
  std::array<std::uint16_t, 4> source{};

  CHECK_FALSE(streamer.Start(nullptr, source.size(), false));
  CHECK_FALSE(streamer.IsPlaying());
  CHECK_FALSE(streamer.Start(source.data(), 0U, false));
  CHECK_FALSE(streamer.IsPlaying());
}

TEST_CASE("record streamer accepts a non-empty buffer") {
  RecordStreamer streamer;
  std::array<std::uint16_t, 4> source{};

  REQUIRE(streamer.Start(source.data(), source.size(), true));
  CHECK(streamer.IsPlaying());
  streamer.Stop();
  CHECK_FALSE(streamer.IsPlaying());
}
