#include "../sources/Application/Player/AtomicTelemetrySnapshot.h"
#include "../sources/Application/Player/TablePlayback.h"

#include "doctest/doctest.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

TEST_CASE("audio telemetry snapshots retain partial final words") {
  using FiveBytes = std::array<std::uint8_t, 5U>;
  AtomicTelemetrySnapshot<FiveBytes> telemetry;
  const FiveBytes published{1U, 2U, 3U, 4U, 0xA5U};
  telemetry.Publish(published);
  CHECK(telemetry.Capture() == published);
}

TEST_CASE("table playback telemetry keeps table identity with all cursors") {
  AtomicTelemetrySnapshot<TablePlaybackSnapshot> telemetry;
  TablePlaybackSnapshot first{};
  first.table = reinterpret_cast<Table *>(std::uintptr_t{0x1000U});
  first.position[0] = 3;
  first.position[1] = 7;
  first.position[2] = 15;
  TablePlaybackSnapshot second{};
  second.table = reinterpret_cast<Table *>(std::uintptr_t{0x2000U});
  second.position[0] = 2;
  second.position[1] = 6;
  second.position[2] = 14;
  telemetry.Publish(first);

  const auto matches = [&](const TablePlaybackSnapshot &captured,
                           const TablePlaybackSnapshot &expected) {
    return captured.table == expected.table &&
           captured.position[0] == expected.position[0] &&
           captured.position[1] == expected.position[1] &&
           captured.position[2] == expected.position[2];
  };

  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (std::uint32_t generation = 0U; generation < 50000U; ++generation)
      telemetry.Publish((generation & 1U) == 0U ? second : first);
    done.store(true, std::memory_order_release);
  });

  bool consistent = true;
  std::uint32_t reads = 0U;
  do {
    const TablePlaybackSnapshot captured = telemetry.Capture();
    if (!matches(captured, first) && !matches(captured, second)) {
      consistent = false;
      break;
    }
    ++reads;
  } while (!done.load(std::memory_order_acquire));

  writer.join();
  CHECK(consistent);
  CHECK(reads > 0U);
}
