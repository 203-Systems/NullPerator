#include "Adapters/wasm/platform/WasmApplicationSnapshot.h"

#include "doctest/doctest.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <thread>

TEST_CASE("WASM application snapshot exposes the stable browser ABI layout") {
  WasmApplicationSnapshot snapshot;
  snapshot.Publish("oneCycAc", 164U, 23U, true, 0x12345678U);

  WasmApplicationSnapshotValues values{};
  REQUIRE(snapshot.Copy(values));
  CHECK((values.sequence & 1U) == 0U);
  CHECK(values.version == WasmApplicationSnapshot::Version);
  CHECK(values.tempo == 164U);
  CHECK(values.sampleCount == 23U);
  CHECK(values.playerRunning == 1U);
  CHECK(values.masterLevel == 0x12345678U);
  CHECK(values.projectNameLength == 8U);
  CHECK(std::string_view(values.projectName.data(), values.projectNameLength) ==
        "oneCycAc");

  const std::uint32_t *words = snapshot.Address();
  CHECK(words[WasmApplicationSnapshot::VersionWord] == 1U);
  CHECK(words[WasmApplicationSnapshot::ByteSizeWord] == 52U);
  CHECK(words[WasmApplicationSnapshot::TempoWord] == 164U);
  CHECK(words[WasmApplicationSnapshot::SampleCountWord] == 23U);
  CHECK(words[WasmApplicationSnapshot::PlayerRunningWord] == 1U);
  CHECK(words[WasmApplicationSnapshot::MasterLevelWord] == 0x12345678U);
  CHECK(words[WasmApplicationSnapshot::ProjectNameLengthWord] == 8U);
}

TEST_CASE("WASM application snapshot truncates and NUL terminates project names") {
  WasmApplicationSnapshot snapshot;
  snapshot.Publish("1234567890abcdefghijklmnop", 138U, 0U, false, 0U);

  WasmApplicationSnapshotValues values{};
  REQUIRE(snapshot.Copy(values));
  CHECK(values.projectNameLength == 16U);
  CHECK(std::string_view(values.projectName.data(), values.projectNameLength) ==
        "1234567890abcdef");
  CHECK(values.projectName[16] == '\0');
}

TEST_CASE("WASM application snapshot seqlock never mixes publications") {
  WasmApplicationSnapshot snapshot;
  snapshot.Publish("odd", 1U, 3U, true, 0xA5A50001U);

  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (std::uint32_t value = 2U; value <= 20000U; ++value) {
      snapshot.Publish((value & 1U) != 0U ? "odd" : "even", value,
                       value * 3U, (value & 1U) != 0U,
                       0xA5A50000U | value);
    }
    done.store(true, std::memory_order_release);
  });

  bool consistent = true;
  std::uint32_t reads = 0U;
  WasmApplicationSnapshotValues failed{};
  do {
    WasmApplicationSnapshotValues values{};
    if (!snapshot.Copy(values)) {
      consistent = false;
      break;
    }
    const bool odd = (values.tempo & 1U) != 0U;
    const std::string_view expected = odd ? "odd" : "even";
    const std::string_view name(values.projectName.data(),
                                values.projectNameLength);
    if (values.sampleCount != values.tempo * 3U ||
        values.playerRunning != static_cast<std::uint32_t>(odd) ||
        values.masterLevel != (0xA5A50000U | values.tempo) ||
        name != expected || (values.sequence & 1U) != 0U) {
      consistent = false;
      failed = values;
      break;
    }
    ++reads;
  } while (!done.load(std::memory_order_acquire));

  writer.join();
  INFO("failed sequence=" << failed.sequence << " tempo=" << failed.tempo
                           << " samples=" << failed.sampleCount
                           << " running=" << failed.playerRunning
                           << " master=" << failed.masterLevel
                           << " name-length=" << failed.projectNameLength
                           << " chars="
                           << static_cast<int>(failed.projectName[0]) << ","
                           << static_cast<int>(failed.projectName[1]) << ","
                           << static_cast<int>(failed.projectName[2]) << ","
                           << static_cast<int>(failed.projectName[3]));
  CHECK(consistent);
  CHECK(reads > 0U);
}
