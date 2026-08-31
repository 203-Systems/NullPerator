#include "Application/Player/PlayerMixerTelemetry.h"

#include "doctest/doctest.h"

#include <atomic>
#include <cstdint>

TEST_CASE("player mixer telemetry keeps note slice and running state packed") {
  using namespace PlayerMixerTelemetry;

  for (std::int8_t slice = 0; slice < 16; ++slice) {
    const PlayerMixerChannelTelemetry decoded =
        Decode(Pack(static_cast<std::uint8_t>(48 + slice), slice, true));
    CHECK(decoded.note == static_cast<std::uint8_t>(48 + slice));
    CHECK(decoded.slice == slice);
    CHECK(decoded.playing);
  }

  std::atomic<std::uint32_t> telemetry{Pack(0xFFU, -1, false)};
  telemetry.fetch_or(PlayingBit, std::memory_order_relaxed);
  PlayerMixerChannelTelemetry decoded =
      Decode(telemetry.load(std::memory_order_relaxed));
  CHECK(decoded.note == 0xFFU);
  CHECK(decoded.slice == -1);
  CHECK(decoded.playing);

  telemetry.fetch_and(~PlayingBit, std::memory_order_relaxed);
  decoded = Decode(telemetry.load(std::memory_order_relaxed));
  CHECK_FALSE(decoded.playing);
}
