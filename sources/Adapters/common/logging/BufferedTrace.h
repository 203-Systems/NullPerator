/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef PICOTRACKER_SHARED_TRACE_H
#define PICOTRACKER_SHARED_TRACE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

enum class LogSeverity : std::uint8_t {
  Debug = 0,
  Info = 1,
  Warn = 2,
  Error = 3
};

struct LogRecord {
  std::uint64_t sequence = 0;
  std::uint64_t monotonicUs = 0;
  LogSeverity severity = LogSeverity::Info;
  bool truncated = false;
  std::uint8_t categoryLength = 0;
  std::uint8_t threadLength = 0;
  std::uint16_t messageLength = 0;
  std::array<char, 24> category{};
  std::array<char, 16> thread{};
  std::array<char, 256> message{};
};

static_assert(std::is_trivially_copyable_v<LogRecord>);

class BufferedTrace {
public:
  using NowFunction = std::uint64_t (*)();
  static constexpr std::size_t QueueCapacity = 128;
  static constexpr std::size_t DrainCapacity = 64;
  static constexpr std::size_t DrainHeaderBytes = 32;
  static constexpr std::size_t DrainRecordBytes = 320;

  explicit BufferedTrace(NowFunction now = nullptr) noexcept;
  ~BufferedTrace();

  static BufferedTrace *Instance() noexcept;
  void PutChar(int character) noexcept;
  void FlushLine() noexcept;
  [[nodiscard]] bool TryPop(LogRecord &record) noexcept;
  [[nodiscard]] std::uint64_t Dropped() const noexcept;
  [[nodiscard]] std::uintptr_t Drain() noexcept;

private:
  void PublishLine() noexcept;
  bool Push(const LogRecord &record) noexcept;
  std::uint64_t Now() const noexcept;

  NowFunction now_ = nullptr;
  std::array<char, 384> line_{};
  std::size_t lineLength_ = 0;
  bool lineTruncated_ = false;
  std::array<LogRecord, QueueCapacity> queue_{};
  alignas(64) std::atomic<std::uint64_t> writePosition_{0};
  alignas(64) std::atomic<std::uint64_t> readPosition_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::uint64_t nextSequence_ = 1;
  std::array<std::uint8_t, DrainHeaderBytes + DrainCapacity * DrainRecordBytes>
      drain_{};
  static std::atomic<BufferedTrace *> instance_;
};

#endif
