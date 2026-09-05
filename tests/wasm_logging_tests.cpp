#include "Adapters/common/logging/BufferedTrace.h"

#include "doctest/doctest.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

namespace {
std::uint64_t testNow = 0;
std::uint64_t Now() { return testNow; }
std::string Text(const char *data, std::size_t size) {
  return std::string(data, size);
}
void Write(BufferedTrace &trace, const std::string &text) {
  for (char value : text)
    trace.PutChar(value);
}
} // namespace

TEST_CASE("WASM logs assemble CRLF and parse category severity and timestamp") {
  BufferedTrace trace(Now);
  testNow = 42;
  Write(trace, "[AUDIO] callback ready\r\n[*ERROR*] [FILES] write "
               "failed\n[-D-] detail\n");
  LogRecord record{};
  REQUIRE(trace.TryPop(record));
  CHECK(record.monotonicUs == 42);
  CHECK(record.severity == LogSeverity::Info);
  CHECK(Text(record.category.data(), record.categoryLength) == "AUDIO");
  CHECK(Text(record.message.data(), record.messageLength) == "callback ready");
  REQUIRE(trace.TryPop(record));
  CHECK(record.severity == LogSeverity::Error);
  CHECK(Text(record.category.data(), record.categoryLength) == "FILES");
  CHECK(Text(record.message.data(), record.messageLength) == "write failed");
  REQUIRE(trace.TryPop(record));
  CHECK(record.severity == LogSeverity::Debug);
}

TEST_CASE("WASM logs flush partial and deterministically truncate long lines") {
  BufferedTrace trace(Now);
  Write(trace, std::string(500, 'x'));
  trace.FlushLine();
  LogRecord record{};
  REQUIRE(trace.TryPop(record));
  CHECK(record.truncated);
  CHECK(record.messageLength == record.message.size());
  CHECK(Text(record.category.data(), record.categoryLength) == "CONSOLE");
}

TEST_CASE("WASM logs are bounded and expose stable drain ABI") {
  BufferedTrace trace(Now);
  for (std::size_t index = 0; index < BufferedTrace::QueueCapacity + 2; ++index)
    Write(trace, "line\n");
  CHECK(trace.Dropped() == 2);
  const auto pointer = trace.Drain();
  REQUIRE(pointer != 0);
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(pointer);
  std::uint32_t version = 0, header = 0, record = 0, count = 0;
  std::uint64_t dropped = 0;
  std::memcpy(&version, bytes, sizeof(version));
  std::memcpy(&header, bytes + 4, sizeof(header));
  std::memcpy(&record, bytes + 8, sizeof(record));
  std::memcpy(&count, bytes + 12, sizeof(count));
  std::memcpy(&dropped, bytes + 16, sizeof(dropped));
  CHECK(version == 1);
  CHECK(header == BufferedTrace::DrainHeaderBytes);
  CHECK(record == BufferedTrace::DrainRecordBytes);
  CHECK(count == BufferedTrace::DrainCapacity);
  CHECK(dropped == 2);
}

TEST_CASE(
    "WASM logs remain ordered under SPSC producer and consumer contention") {
  BufferedTrace trace(Now);
  constexpr std::size_t lineCount = 16'384;
  std::atomic<bool> done{false};
  std::atomic<bool> ordered{true};
  std::size_t consumed = 0;
  std::thread producer([&] {
    for (std::size_t index = 0; index < lineCount; ++index)
      Write(trace, "[TEST] line\n");
    done.store(true, std::memory_order_release);
  });
  std::thread consumer([&] {
    std::uint64_t previous = 0;
    while (!done.load(std::memory_order_acquire) ||
           consumed + trace.Dropped() < lineCount) {
      LogRecord record{};
      if (!trace.TryPop(record)) {
        std::this_thread::yield();
        continue;
      }
      if (record.sequence <= previous)
        ordered.store(false, std::memory_order_relaxed);
      previous = record.sequence;
      ++consumed;
    }
  });
  producer.join();
  consumer.join();
  CHECK(ordered.load(std::memory_order_relaxed));
  CHECK(consumed + trace.Dropped() == lineCount);
}
