/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Stable, browser-readable application model snapshot. The application
// pthread is the sole writer. Browser main reads the shared words with
// Atomics.load and retries unless both sequence reads match and are even.
//
// Word layout (little endian):
//   0 sequence (odd while publishing)
//   1 ABI version
//   2 snapshot byte size
//   3 tempo
//   4 loaded SamplePool entry count
//   5 Player::IsRunning (0 or 1)
//   6 packed stereo master level (left high 16, right low 16)
//   7 project-name byte length
//   8..12 UTF-8 project name (20 bytes, NUL padded; at most 16 bytes)
struct WasmApplicationSnapshotValues {
  std::uint32_t sequence = 0U;
  std::uint32_t version = 0U;
  std::uint32_t tempo = 0U;
  std::uint32_t sampleCount = 0U;
  std::uint32_t playerRunning = 0U;
  std::uint32_t masterLevel = 0U;
  std::uint32_t projectNameLength = 0U;
  std::array<char, 17U> projectName{};
};

class WasmApplicationSnapshot {
public:
  static constexpr std::uint32_t Version = 1U;
  static constexpr std::size_t ProjectNameCapacity = 16U;
  static constexpr std::size_t ProjectNameStorageBytes = 20U;
  static constexpr std::size_t ProjectNameWords =
      ProjectNameStorageBytes / sizeof(std::uint32_t);

  static constexpr std::size_t SequenceWord = 0U;
  static constexpr std::size_t VersionWord = 1U;
  static constexpr std::size_t ByteSizeWord = 2U;
  static constexpr std::size_t TempoWord = 3U;
  static constexpr std::size_t SampleCountWord = 4U;
  static constexpr std::size_t PlayerRunningWord = 5U;
  static constexpr std::size_t MasterLevelWord = 6U;
  static constexpr std::size_t ProjectNameLengthWord = 7U;
  static constexpr std::size_t ProjectNameWord = 8U;
  static constexpr std::size_t WordCount = ProjectNameWord + ProjectNameWords;
  static constexpr std::uint32_t ByteSize =
      static_cast<std::uint32_t>(WordCount * sizeof(std::uint32_t));
  static_assert(WordCount == 13U);
  static_assert(ByteSize == 52U);

  WasmApplicationSnapshot() noexcept;

  void Publish(const char *projectName, std::uint32_t tempo,
               std::uint32_t sampleCount, bool playerRunning,
               std::uint32_t masterLevel) noexcept;

  [[nodiscard]] bool Copy(WasmApplicationSnapshotValues &values) const noexcept;
  [[nodiscard]] const std::uint32_t *Address() const noexcept;

private:
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "WASM application snapshots require lock-free 32-bit atomics");
  static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t),
                "WASM application snapshot words must be tightly packed");

  std::array<std::atomic<std::uint32_t>, WordCount> words_{};
};

WasmApplicationSnapshot &Wasm_ApplicationSnapshot() noexcept;
const std::uint32_t *Wasm_ApplicationSnapshotAddress() noexcept;
