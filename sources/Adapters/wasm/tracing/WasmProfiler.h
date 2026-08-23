/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef PICOTRACKER_WASM_PROFILER_H
#define PICOTRACKER_WASM_PROFILER_H

#include "TraceRecord.h"
#include "TraceRingBuffer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class WasmProfiler {
public:
  using NowFunction = std::uint64_t (*)();
  static constexpr std::uint32_t AllCategoriesMask = (1U << 10) - 1U;
  static constexpr std::size_t RingCapacity = 4096;
  static constexpr std::size_t DrainCapacity = 256;
  static constexpr std::size_t DrainHeaderBytes = 40;
  static constexpr std::size_t DrainRecordBytes = 32;

  static std::uint32_t Start(std::uint32_t mask) noexcept;
  static std::uint32_t Stop() noexcept;
  static bool CategoryEnabled(WasmTraceCategory category) noexcept;
  static std::uint32_t Generation() noexcept;
  static void Emit(WasmTraceCategory category, WasmTraceName name,
                   WasmTracePhase phase, std::uint32_t value = 0,
                   WasmTraceThread thread = WasmTraceThread::Application,
                   std::uint32_t expectedGeneration = 0,
                   std::uint16_t flags = 0) noexcept;
  // Input acceptance and frame commit must share their actual boundary
  // timestamps. EmitAt avoids a second clock read while retaining the normal
  // category and capture-generation checks.
  static void EmitAt(std::uint64_t timestampUs, WasmTraceCategory category,
                     WasmTraceName name, WasmTracePhase phase,
                     std::uint32_t value = 0,
                     WasmTraceThread thread = WasmTraceThread::Application,
                     std::uint32_t expectedGeneration = 0,
                     std::uint16_t flags = 0) noexcept;
  static std::uint64_t TimestampNow() noexcept;
  static std::uintptr_t Drain() noexcept;
  static void SetClockForTesting(NowFunction now) noexcept;
  static std::uint64_t WrittenForTesting() noexcept;

private:
  static std::uint64_t Now() noexcept;
  static TraceRingBuffer<RingCapacity> ring_;
  static std::atomic<std::uint32_t> mask_;
  static std::atomic<std::uint32_t> generation_;
  static std::atomic<std::uint64_t> droppedBase_;
  static std::atomic<NowFunction> now_;
  static std::array<std::uint8_t, DrainHeaderBytes + DrainCapacity * DrainRecordBytes> drain_;
};

class WasmTraceScope {
public:
  WasmTraceScope(WasmTraceCategory category, WasmTraceName name) noexcept;
  ~WasmTraceScope();
  WasmTraceScope(const WasmTraceScope &) = delete;
  WasmTraceScope &operator=(const WasmTraceScope &) = delete;

private:
  WasmTraceCategory category_;
  WasmTraceName name_;
  std::uint32_t generation_ = 0;
  bool enabled_ = false;
};

#define WASM_TRACE_JOIN_IMPL(left, right) left##right
#define WASM_TRACE_JOIN(left, right) WASM_TRACE_JOIN_IMPL(left, right)
#define WASM_TRACE_SCOPE(category, name) \
  WasmTraceScope WASM_TRACE_JOIN(wasmTraceScope_, __LINE__)(category, name)

#endif
