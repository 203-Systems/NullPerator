#include "BufferedTrace.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace {
std::uint64_t DefaultNow() {
  using Clock = std::chrono::steady_clock;
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          Clock::now().time_since_epoch())
          .count());
}

template <typename Value> void Store(std::uint8_t *destination, Value value) {
  static_assert(std::is_trivially_copyable_v<Value>);
  std::memcpy(destination, &value, sizeof(value));
}

template <std::size_t Size>
std::size_t CopyText(std::array<char, Size> &destination, const char *source,
                     std::size_t length) {
  const std::size_t copied = std::min(length, destination.size());
  if (copied != 0)
    std::memcpy(destination.data(), source, copied);
  return copied;
}

bool StartsWith(const char *text, std::size_t length, const char *prefix) {
  const std::size_t prefixLength = std::strlen(prefix);
  return length >= prefixLength && std::memcmp(text, prefix, prefixLength) == 0;
}

void SaturatingIncrement(std::atomic<std::uint64_t> &value) {
  std::uint64_t current = value.load(std::memory_order_relaxed);
  while (current != std::numeric_limits<std::uint64_t>::max() &&
         !value.compare_exchange_weak(current, current + 1,
                                      std::memory_order_relaxed)) {
  }
}
} // namespace

std::atomic<BufferedTrace *> BufferedTrace::instance_{nullptr};

BufferedTrace::BufferedTrace(NowFunction now) noexcept
    : now_(now == nullptr ? DefaultNow : now) {
  instance_.store(this, std::memory_order_release);
}

BufferedTrace::~BufferedTrace() {
  FlushLine();
  BufferedTrace *expected = this;
  (void)instance_.compare_exchange_strong(expected, nullptr,
                                          std::memory_order_acq_rel);
}

BufferedTrace *BufferedTrace::Instance() noexcept {
  return instance_.load(std::memory_order_acquire);
}
std::uint64_t BufferedTrace::Now() const noexcept {
  return now_ == nullptr ? 0 : now_();
}

void BufferedTrace::PutChar(int character) noexcept {
  if (character == '\r')
    return;
  if (character == '\n') {
    PublishLine();
    return;
  }
  if (lineLength_ + 1 < line_.size())
    line_[lineLength_++] = static_cast<char>(character);
  else
    lineTruncated_ = true;
}

void BufferedTrace::FlushLine() noexcept {
  if (lineLength_ != 0 || lineTruncated_)
    PublishLine();
}

void BufferedTrace::PublishLine() noexcept {
  if (lineLength_ == 0 && !lineTruncated_)
    return;
  const char *cursor = line_.data();
  std::size_t remaining = lineLength_;
  LogRecord record{};
  record.sequence = nextSequence_++;
  record.monotonicUs = Now();
  record.truncated = lineTruncated_;
  const char *category = "CONSOLE";
  std::size_t categoryLength = 7;

  if (StartsWith(cursor, remaining, "[*ERROR*]")) {
    record.severity = LogSeverity::Error;
    cursor += 9;
    remaining -= 9;
  }
  while (remaining && *cursor == ' ') {
    ++cursor;
    --remaining;
  }
  if (remaining >= 3 && *cursor == '[') {
    const char *close =
        static_cast<const char *>(std::memchr(cursor + 1, ']', remaining - 1));
    if (close != nullptr) {
      category = cursor + 1;
      categoryLength = static_cast<std::size_t>(close - category);
      const std::size_t consumed = static_cast<std::size_t>(close - cursor) + 1;
      cursor += consumed;
      remaining -= consumed;
    }
  }
  while (remaining && *cursor == ' ') {
    ++cursor;
    --remaining;
  }
  if (record.severity != LogSeverity::Error) {
    if (categoryLength == 3 && std::memcmp(category, "-D-", 3) == 0)
      record.severity = LogSeverity::Debug;
    else if ((categoryLength == 4 && std::memcmp(category, "WARN", 4) == 0) ||
             (categoryLength == 7 && std::memcmp(category, "WARNING", 7) == 0))
      record.severity = LogSeverity::Warn;
  }
  record.categoryLength = static_cast<std::uint8_t>(
      CopyText(record.category, category, categoryLength));
  if (record.categoryLength < categoryLength)
    record.truncated = true;
  record.threadLength =
      static_cast<std::uint8_t>(CopyText(record.thread, "application", 11));
  record.messageLength =
      static_cast<std::uint16_t>(CopyText(record.message, cursor, remaining));
  if (record.messageLength < remaining)
    record.truncated = true;
  (void)Push(record);
  lineLength_ = 0;
  lineTruncated_ = false;
}

bool BufferedTrace::Push(const LogRecord &record) noexcept {
  const std::uint64_t write = writePosition_.load(std::memory_order_relaxed);
  const std::uint64_t read = readPosition_.load(std::memory_order_acquire);
  if (write - read >= QueueCapacity) {
    SaturatingIncrement(dropped_);
    return false;
  }
  queue_[write & (QueueCapacity - 1)] = record;
  writePosition_.store(write + 1, std::memory_order_release);
  return true;
}

bool BufferedTrace::TryPop(LogRecord &record) noexcept {
  const std::uint64_t read = readPosition_.load(std::memory_order_relaxed);
  if (read == writePosition_.load(std::memory_order_acquire))
    return false;
  record = queue_[read & (QueueCapacity - 1)];
  readPosition_.store(read + 1, std::memory_order_release);
  return true;
}

std::uint64_t BufferedTrace::Dropped() const noexcept {
  return dropped_.load(std::memory_order_relaxed);
}

std::uintptr_t BufferedTrace::Drain() noexcept {
  std::array<LogRecord, DrainCapacity> records{};
  std::uint32_t count = 0;
  while (count < records.size() && TryPop(records[count]))
    ++count;
  Store<std::uint32_t>(drain_.data(), 1);
  Store<std::uint32_t>(drain_.data() + 4, DrainHeaderBytes);
  Store<std::uint32_t>(drain_.data() + 8, DrainRecordBytes);
  Store<std::uint32_t>(drain_.data() + 12, count);
  Store<std::uint64_t>(drain_.data() + 16, Dropped());
  Store<std::uint64_t>(drain_.data() + 24,
                       0); // reserved; producer state is not read cross-thread
  for (std::uint32_t index = 0; index < count; ++index) {
    const LogRecord &source = records[index];
    std::uint8_t *target =
        drain_.data() + DrainHeaderBytes + index * DrainRecordBytes;
    Store<std::uint64_t>(target, source.sequence);
    Store<std::uint64_t>(target + 8, source.monotonicUs);
    target[16] = static_cast<std::uint8_t>(source.severity);
    target[17] = source.truncated ? 1 : 0;
    target[18] = source.categoryLength;
    target[19] = source.threadLength;
    Store<std::uint16_t>(target + 20, source.messageLength);
    Store<std::uint16_t>(target + 22, 0);
    std::memcpy(target + 24, source.category.data(), source.category.size());
    std::memcpy(target + 48, source.thread.data(), source.thread.size());
    std::memcpy(target + 64, source.message.data(), source.message.size());
  }
  return reinterpret_cast<std::uintptr_t>(drain_.data());
}
