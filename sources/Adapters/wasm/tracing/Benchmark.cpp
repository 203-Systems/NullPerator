/* SPDX-License-Identifier: BSD-3-Clause */

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_BENCHMARK_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_BENCHMARK_EXPORT
#endif

#include "Benchmark.h"
#include "System/Console/Profiler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>

namespace {
std::uint64_t DefaultNow() {
#ifdef __EMSCRIPTEN__
  const double micros = emscripten_get_now() * 1000.0;
  return micros > 0.0 ? static_cast<std::uint64_t>(micros) : 0;
#else
  using Clock = std::chrono::steady_clock;
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          Clock::now().time_since_epoch())
          .count());
#endif
}

constexpr std::array<std::uint8_t, 64> FixtureNotes = [] {
  std::array<std::uint8_t, 64> notes{};
  for (std::size_t index = 0; index < notes.size(); ++index)
    notes[index] = static_cast<std::uint8_t>(36 + (index * 7 + index / 4) % 48);
  return notes;
}();

std::int16_t Clamp16(std::int64_t value) {
  return static_cast<std::int16_t>(
      std::clamp<std::int64_t>(value, -32768, 32767));
}

void RenderBlock(std::uint32_t block, std::array<std::uint32_t, 8> &phases,
                 std::uint32_t &hash) {
  for (std::uint32_t frame = 0; frame < 128; ++frame) {
    std::int64_t left = 0, right = 0;
    const std::uint32_t row = ((block * 128 + frame) / 32) & 63U;
    for (std::uint32_t channel = 0; channel < phases.size(); ++channel) {
      const std::uint32_t note = FixtureNotes[(row + channel * 5) & 63U];
      phases[channel] += (note + 1U) * (17U + channel * 2U) * 257U;
      const std::int32_t triangle =
          static_cast<std::int32_t>(
              ((phases[channel] >> 16) ^
               ((phases[channel] & 0x80000000U) ? 0xffffU : 0U)) &
              0xffffU) -
          32768;
      left += static_cast<std::int64_t>(triangle) * (8 - channel);
      right += static_cast<std::int64_t>(triangle) * (channel + 1);
    }
    const std::uint16_t left16 = static_cast<std::uint16_t>(Clamp16(left / 20));
    const std::uint16_t right16 =
        static_cast<std::uint16_t>(Clamp16(right / 20));
    hash = (hash ^ (left16 & 0xffU)) * 16777619U;
    hash = (hash ^ (left16 >> 8)) * 16777619U;
    hash = (hash ^ (right16 & 0xffU)) * 16777619U;
    hash = (hash ^ (right16 >> 8)) * 16777619U;
  }
}

std::uint64_t Rank(const std::uint64_t *samples, std::uint32_t count,
                   std::uint32_t numerator) {
  if (count == 0)
    return 0;
  const std::uint32_t rank = std::max(1U, (count * numerator + 99U) / 100U);
  return samples[std::min(rank, count) - 1U];
}
} // namespace

WasmBenchmarkResult WasmBenchmark::Run(WasmBenchmarkConfig config,
                                       NowFunction now) noexcept {
  WasmBenchmarkResult result{};
  if (config.version != WasmBenchmarkConfig::Version)
    return result;
  config.iterations = std::clamp(config.iterations, 1U, MaximumIterations);
  config.warmupIterations = std::min(config.warmupIterations, 64U);
  if (now == nullptr)
    now = DefaultNow;
  result.iterations = config.iterations;
  result.warmupIterations = config.warmupIterations;
  result.sampleCount = config.iterations;
  std::array<std::uint32_t, 8> phases{0x12345678U, 0x23456789U, 0x3456789aU,
                                      0x456789abU, 0x56789abcU, 0x6789abcdU,
                                      0x789abcdeU, 0x89abcdefU};
  std::uint32_t warmupHash = 2166136261U;
  for (std::uint32_t block = 0; block < config.warmupIterations; ++block)
    RenderBlock(block, phases, warmupHash);
  // Reset all fixture state so warmup count cannot change rendered output.
  phases = {0x12345678U, 0x23456789U, 0x3456789aU, 0x456789abU,
            0x56789abcU, 0x6789abcdU, 0x789abcdeU, 0x89abcdefU};
  std::array<std::uint64_t, MaximumIterations> samples{};
  std::uint32_t hash = 2166136261U;
  for (std::uint32_t block = 0; block < config.iterations; ++block) {
    const std::uint64_t start = now();
    const std::uint32_t generation = Profiler::Generation();
    Profiler::Emit(TraceCategory::Benchmark, TraceName::BenchmarkBlock,
                   TracePhase::Begin, block, TraceThread::Browser, generation);
    RenderBlock(block, phases, hash);
    Profiler::Emit(TraceCategory::Benchmark, TraceName::BenchmarkBlock,
                   TracePhase::End, block, TraceThread::Browser, generation);
    const std::uint64_t end = now();
    const std::uint64_t duration = end >= start ? end - start : 0;
    samples[block] = duration;
    result.totalUs += duration;
    if (config.deadlineUs != 0 && duration > config.deadlineUs)
      ++result.deadlineMisses;
  }
  std::sort(samples.begin(), samples.begin() + config.iterations);
  result.medianUs = Rank(samples.data(), config.iterations, 50);
  result.p95Us = Rank(samples.data(), config.iterations, 95);
  result.p99Us = Rank(samples.data(), config.iterations, 99);
  result.maximumUs = samples[config.iterations - 1];
  result.fixtureHash = hash;
  result.totalWork = config.iterations * 128U * 8U;
  return result;
}

extern "C" WASM_BENCHMARK_EXPORT const WasmBenchmarkResult *
PicoTracker_Wasm_RunBenchmark(std::uint32_t iterations, std::uint32_t warmup,
                              std::uint32_t deadlineUs) {
  static WasmBenchmarkResult result{};
  result = WasmBenchmark::Run(
      {WasmBenchmarkConfig::Version, iterations, warmup, deadlineUs});
  return &result;
}
