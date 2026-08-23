/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef PICOTRACKER_WASM_BENCHMARK_H
#define PICOTRACKER_WASM_BENCHMARK_H

#include <cstdint>

struct WasmBenchmarkConfig {
  static constexpr std::uint32_t Version = 1;
  std::uint32_t version = Version;
  std::uint32_t iterations = 256;
  std::uint32_t warmupIterations = 8;
  std::uint32_t deadlineUs = 3'000;
};

struct WasmBenchmarkResult {
  static constexpr std::uint32_t Version = 1;
  static constexpr std::uint32_t FixtureVersion = 1;
  std::uint32_t version = Version;
  std::uint32_t size = sizeof(WasmBenchmarkResult);
  std::uint32_t fixtureVersion = FixtureVersion;
  std::uint32_t configVersion = WasmBenchmarkConfig::Version;
  std::uint32_t iterations = 0;
  std::uint32_t warmupIterations = 0;
  std::uint32_t sampleCount = 0;
  std::uint32_t deadlineMisses = 0;
  std::uint64_t medianUs = 0;
  std::uint64_t p95Us = 0;
  std::uint64_t p99Us = 0;
  std::uint64_t maximumUs = 0;
  std::uint64_t totalUs = 0;
  std::uint32_t fixtureHash = 0;
  std::uint32_t totalWork = 0;
};

static_assert(sizeof(WasmBenchmarkResult) == 80);

class WasmBenchmark {
public:
  using NowFunction = std::uint64_t (*)();
  static constexpr std::uint32_t MaximumIterations = 2048;
  static WasmBenchmarkResult Run(WasmBenchmarkConfig config, NowFunction now = nullptr) noexcept;
};

#endif
