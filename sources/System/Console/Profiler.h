/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef PICOTRACKER_SHARED_PROFILER_H
#define PICOTRACKER_SHARED_PROFILER_H

#include "System/Console/TraceRecord.h"
#include "System/Console/TraceRingBuffer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class Profiler {
public:
  using NowFunction = std::uint64_t (*)();
  static constexpr std::uint32_t AllCategoriesMask = (1U << 10) - 1U;
  static constexpr std::size_t RingCapacity = 4096;
  static constexpr std::size_t DrainCapacity = 256;
  static constexpr std::size_t DrainHeaderBytes = 40;
  static constexpr std::size_t DrainRecordBytes = 32;

  static std::uint32_t Start(std::uint32_t mask) noexcept;
  static std::uint32_t Stop() noexcept;
  static bool CategoryEnabled(TraceCategory category) noexcept;
  static std::uint32_t Generation() noexcept;
  static void Emit(TraceCategory category, TraceName name, TracePhase phase,
                   std::uint32_t value = 0,
                   TraceThread thread = TraceThread::Application,
                   std::uint32_t expectedGeneration = 0,
                   std::uint16_t flags = 0) noexcept;
  // Input acceptance and frame commit must share their actual boundary
  // timestamps. EmitAt avoids a second clock read while retaining the normal
  // category and capture-generation checks.
  static void EmitAt(std::uint64_t timestampUs, TraceCategory category,
                     TraceName name, TracePhase phase, std::uint32_t value = 0,
                     TraceThread thread = TraceThread::Application,
                     std::uint32_t expectedGeneration = 0,
                     std::uint16_t flags = 0) noexcept;
  static std::uint64_t TimestampNow() noexcept;
  static std::uintptr_t Drain() noexcept;
  // Configure the timestamp domain before starting a capture. nullptr restores
  // the host monotonic clock. Platform bridges and tests use the same contract.
  static void SetClock(NowFunction now) noexcept;
  static std::uint64_t WrittenForTesting() noexcept;

private:
  static std::uint64_t Now() noexcept;
  static TraceRingBuffer<RingCapacity> ring_;
  static std::atomic<std::uint32_t> mask_;
  static std::atomic<std::uint32_t> generation_;
  static std::atomic<std::uint64_t> droppedBase_;
  static std::atomic<NowFunction> now_;
  static std::array<std::uint8_t,
                    DrainHeaderBytes + DrainCapacity * DrainRecordBytes>
      drain_;
};

class ProfileScope {
public:
  ProfileScope(TraceCategory category, TraceName name) noexcept;
  ~ProfileScope();
  ProfileScope(const ProfileScope &) = delete;
  ProfileScope &operator=(const ProfileScope &) = delete;

private:
  TraceCategory category_;
  TraceName name_;
  std::uint32_t generation_ = 0;
  bool enabled_ = false;
};

#define PROFILE_JOIN_IMPL(left, right) left##right
#define PROFILE_JOIN(left, right) PROFILE_JOIN_IMPL(left, right)
#define PROFILE_SCOPE(category, name)                                          \
  ProfileScope PROFILE_JOIN(profileScope_, __LINE__)(category, name)

#endif
