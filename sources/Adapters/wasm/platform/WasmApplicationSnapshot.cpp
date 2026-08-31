/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmApplicationSnapshot.h"

#include <algorithm>

WasmApplicationSnapshot::WasmApplicationSnapshot() noexcept {
  words_[VersionWord].store(Version, std::memory_order_release);
  words_[ByteSizeWord].store(ByteSize, std::memory_order_release);
}

void WasmApplicationSnapshot::Publish(const char *projectName,
                                      std::uint32_t tempo,
                                      std::uint32_t sampleCount,
                                      bool playerRunning,
                                      std::uint32_t masterLevel,
                                      std::uint32_t playingTrackMask) noexcept {
  std::array<std::uint8_t, ProjectNameStorageBytes> nameBytes{};
  std::size_t nameLength = 0U;
  if (projectName != nullptr) {
    while (nameLength < ProjectNameCapacity && projectName[nameLength] != '\0') {
      nameBytes[nameLength] = static_cast<std::uint8_t>(projectName[nameLength]);
      ++nameLength;
    }
  }

  const std::uint32_t writing =
      words_[SequenceWord].fetch_add(1U, std::memory_order_acq_rel) + 1U;
  // Release/acquire on payload words is intentional. Cross-location relaxed
  // atomics could expose a new payload word while a reader still observes the
  // previous even sequence. Observing any new payload now happens-after the
  // writer's odd sequence transition, forcing the final sequence check to
  // reject a mixed publication.
  words_[VersionWord].store(Version, std::memory_order_release);
  words_[ByteSizeWord].store(ByteSize, std::memory_order_release);
  words_[TempoWord].store(tempo, std::memory_order_release);
  words_[SampleCountWord].store(sampleCount, std::memory_order_release);
  words_[PlayerRunningWord].store(playerRunning ? 1U : 0U,
                                  std::memory_order_release);
  words_[MasterLevelWord].store(masterLevel, std::memory_order_release);
  words_[ProjectNameLengthWord].store(static_cast<std::uint32_t>(nameLength),
                                      std::memory_order_release);
  for (std::size_t index = 0; index < ProjectNameWords; ++index) {
    const std::size_t offset = index * sizeof(std::uint32_t);
    const std::uint32_t word =
        static_cast<std::uint32_t>(nameBytes[offset]) |
        (static_cast<std::uint32_t>(nameBytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(nameBytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(nameBytes[offset + 3U]) << 24U);
    words_[ProjectNameWord + index].store(word, std::memory_order_release);
  }
  words_[PlayingTrackMaskWord].store(playingTrackMask,
                                     std::memory_order_release);
  words_[SequenceWord].store(writing + 1U, std::memory_order_release);
}

bool WasmApplicationSnapshot::Copy(
    WasmApplicationSnapshotValues &values) const noexcept {
  std::array<std::uint32_t, WordCount> copy{};
  for (;;) {
    const std::uint32_t before =
        words_[SequenceWord].load(std::memory_order_acquire);
    if ((before & 1U) != 0U) {
      continue;
    }
    for (std::size_t index = VersionWord; index < WordCount; ++index) {
      copy[index] = words_[index].load(std::memory_order_acquire);
    }
    const std::uint32_t after =
        words_[SequenceWord].load(std::memory_order_acquire);
    if (before == after && (after & 1U) == 0U) {
      copy[SequenceWord] = after;
      break;
    }
  }

  if (copy[VersionWord] != Version || copy[ByteSizeWord] != ByteSize) {
    return false;
  }

  WasmApplicationSnapshotValues result{};
  result.sequence = copy[SequenceWord];
  result.version = copy[VersionWord];
  result.tempo = copy[TempoWord];
  result.sampleCount = copy[SampleCountWord];
  result.playerRunning = copy[PlayerRunningWord];
  result.masterLevel = copy[MasterLevelWord];
  result.projectNameLength = std::min<std::uint32_t>(
      copy[ProjectNameLengthWord], ProjectNameCapacity);
  for (std::size_t index = 0; index < result.projectNameLength; ++index) {
    const std::uint32_t word =
        copy[ProjectNameWord + index / sizeof(std::uint32_t)];
    result.projectName[index] = static_cast<char>(
        (word >> ((index % sizeof(std::uint32_t)) * 8U)) & 0xFFU);
  }
  result.projectName[result.projectNameLength] = '\0';
  result.playingTrackMask = copy[PlayingTrackMaskWord];
  values = result;
  return true;
}

const std::uint32_t *WasmApplicationSnapshot::Address() const noexcept {
  return reinterpret_cast<const std::uint32_t *>(words_.data());
}

WasmApplicationSnapshot &Wasm_ApplicationSnapshot() noexcept {
  static WasmApplicationSnapshot snapshot;
  return snapshot;
}

const std::uint32_t *Wasm_ApplicationSnapshotAddress() noexcept {
  return Wasm_ApplicationSnapshot().Address();
}
