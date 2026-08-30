/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file is part of the esp32 firmware
 */

#include "SamplePool.h"

#include <cstdint>
#include <cstring>
#include <utility>

static constexpr uint32_t kDedicatedSampleStoreSize = 8U * 1024U * 1024U;
static constexpr size_t kInternalSampleMaxBytes = 64U * 1024U;
static constexpr size_t kInternalSampleReserveBytes = 128U * 1024U;

static bool has_psram() {
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
}

NodeSamplePool::NodeSamplePool() : SamplePool() {}

NodeSamplePool::~NodeSamplePool() {
  Reset();
  if (sampleStore_ != nullptr) {
    heap_caps_free(sampleStore_);
    sampleStore_ = nullptr;
  }
  dedicatedStoreAttempted_ = false;
  storeLimit_ = 0;
}

bool NodeSamplePool::ensureDedicatedPsramStore() {
  if (sampleStore_ != nullptr) {
    return true;
  }
  if (dedicatedStoreAttempted_) {
    return false;
  }

  dedicatedStoreAttempted_ = true;
  if (!has_psram()) {
    Trace::Log("SAMPLEPOOL", "PSRAM unavailable, using heap-backed samples");
    return false;
  }

  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  const size_t reserveBytes =
      largestBlock >= kDedicatedSampleStoreSize ? kDedicatedSampleStoreSize
                                                : largestBlock;
  if (reserveBytes == 0) {
    Trace::Error("SAMPLEPOOL", "No free PSRAM block available for sample pool");
    return false;
  }

  void *ptr = heap_caps_malloc(reserveBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr == nullptr) {
    Trace::Error("SAMPLEPOOL",
                 "Failed reserving %u bytes of PSRAM for sample pool",
                 static_cast<unsigned>(reserveBytes));
    return false;
  }

  sampleStore_ = static_cast<uint8_t *>(ptr);
  storeLimit_ = static_cast<uint32_t>(reserveBytes);
  writeOffset_ = 0;

  Trace::Log("SAMPLEPOOL", "Reserved %u bytes of PSRAM for sample pool",
             static_cast<unsigned>(storeLimit_));
  return true;
}

bool NodeSamplePool::canUseInternalSampleStorage(size_t bytes) const {
  if (bytes == 0 || bytes > kInternalSampleMaxBytes) {
    return false;
  }

  const size_t freeInternal =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return freeInternal > bytes + kInternalSampleReserveBytes;
}

bool NodeSamplePool::isDedicatedSampleBuffer(const void *buffer) const {
  if (sampleStore_ == nullptr || buffer == nullptr) {
    return false;
  }

  const auto *ptr = static_cast<const uint8_t *>(buffer);
  return ptr >= sampleStore_ && ptr < sampleStore_ + storeLimit_;
}

void NodeSamplePool::freeSampleBuffer(WavFile &wave) {
  void *buffer = wave.GetSampleBuffer(0);
  if (buffer == nullptr) {
    return;
  }

  if (isDedicatedSampleBuffer(buffer)) {
    wave.SetSampleBuffer(nullptr);
    return;
  }

  heap_caps_free(buffer);
  wave.SetSampleBuffer(nullptr);
}

std::optional<void *> NodeSamplePool::allocSampleBuffer(size_t bytes) {
  if (sampleStore_ != nullptr) {
    uint32_t alignedOffset = (writeOffset_ + 3U) & ~3U;
    if ((alignedOffset + bytes) > storeLimit_) {
      return std::nullopt;
    }
    writeOffset_ = alignedOffset;
    return sampleStore_ + alignedOffset;
  }

  // A board with PSRAM must never spill sample audio into the internal heap.
  // Keeping that heap available is essential for DMA, task stacks and the
  // real-time audio path when external memory is fragmented or exhausted.
  if (has_psram()) {
    void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
      return ptr;
    }
    return std::nullopt;
  }

  // PSRAM-less boards retain a tightly bounded compatibility path.
  if (canUseInternalSampleStorage(bytes)) {
    void *ptr =
        heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
      return ptr;
    }
  }
  return std::nullopt;
}

void NodeSamplePool::Reset() {
  for (uint32_t i = 0; i < count_; ++i) {
    freeSampleBuffer(wav_[i]);
    wav_[i].Close();
  }

  for (uint32_t i = 0; i < MAX_SAMPLES; ++i) {
    nameStore_[i][0] = '\0';
    wav_[i].SetSampleBuffer(nullptr);
  }

  count_ = 0;
  writeOffset_ = 0;
}

bool NodeSamplePool::CheckSampleFits(int sampleSize) {
  if (sampleSize <= 0)
    return false;

  if (has_psram()) {
    if (ensureDedicatedPsramStore()) {
      uint32_t alignedOffset = (writeOffset_ + 3U) & ~3U;
      return (alignedOffset + static_cast<uint32_t>(sampleSize)) <=
             storeLimit_;
    }

    // A failed arena reservation may still leave a smaller external block,
    // but internal SRAM is deliberately excluded from this calculation.
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >
           static_cast<size_t>(sampleSize) + 8192U;
  }

  return canUseInternalSampleStorage(static_cast<size_t>(sampleSize));
}

uint32_t NodeSamplePool::GetAvailableSampleStorageSpace() {
  if (sampleStore_ != nullptr) {
    return storeLimit_ - writeOffset_;
  }

  // Reporting capacity must remain side-effect free. Reserving the dedicated
  // arena here made an empty project consume nearly all available PSRAM merely
  // because the UI queried free sample space. The first real fit check/load
  // creates the arena instead.
  size_t freeBytes = 0;
  if (has_psram()) {
    const size_t largestBlock =
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    freeBytes = largestBlock >= kDedicatedSampleStoreSize
                    ? kDedicatedSampleStoreSize
                    : largestBlock;
  } else {
    const size_t freeInternal =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (freeInternal > kInternalSampleReserveBytes) {
      freeBytes = freeInternal - kInternalSampleReserveBytes;
      if (freeBytes > kInternalSampleMaxBytes)
        freeBytes = kInternalSampleMaxBytes;
    }
  }
  return static_cast<uint32_t>(freeBytes);
}

bool NodeSamplePool::loadSample(const char *name) {
  Trace::Log("SAMPLEPOOL", "Loading sample into memory: %s", name);

  if (count_ == MAX_SAMPLES) {
    return false;
  }

  auto res = wav_[count_].Open(name);
  if (!res) {
    Trace::Error("SAMPLEPOOL", "Failed to open sample: %s", name);
    return false;
  }

  WavFile &wav = wav_[count_];
  const uint32_t sampleBytes = wav.GetDiskSize(-1);
  if (sampleBytes == 0) {
    Trace::Error("SAMPLEPOOL", "Sample contains no audio data: %s", name);
    wav.Close();
    return false;
  }
  const uint32_t initialWriteOffset = writeOffset_;
  if (!CheckSampleFits(static_cast<int>(sampleBytes))) {
    Trace::Error("SAMPLEPOOL", "Not enough heap for sample (%u bytes)",
                 sampleBytes);
    wav.Close();
    return false;
  }

  auto buffer = allocSampleBuffer(sampleBytes);
  if (!buffer.has_value()) {
    Trace::Error("SAMPLEPOOL", "Heap alloc failed for sample (%u bytes)",
                 sampleBytes);
    wav.Close();
    return false;
  }

  wav.SetSampleBuffer(static_cast<int16_t *>(buffer.value()));

  uint32_t alignedOffset = initialWriteOffset;
  const bool usingDedicatedStore = isDedicatedSampleBuffer(buffer.value());
  if (usingDedicatedStore) {
    alignedOffset = (initialWriteOffset + 3U) & ~3U;
    writeOffset_ = alignedOffset;
  }

  uint32_t offset = 0;
  uint32_t bytesRead = 0;
  uint32_t totalRead = 0;
  uint32_t writeStep = (9 + sampleBytes) / 10;
  uint32_t subStep = 0;
  wav.Rewind();
  while (offset < sampleBytes) {
    uint32_t toRead = sampleBytes - offset;
    if (toRead > BUFFER_SIZE) {
      toRead = BUFFER_SIZE;
    }
    if (!wav.Read(static_cast<uint8_t *>(buffer.value()) + offset, toRead,
                  &bytesRead)) {
      Trace::Error("SAMPLEPOOL", "Failed reading sample data: %s", name);
      if (usingDedicatedStore) {
        writeOffset_ = initialWriteOffset;
      }
      freeSampleBuffer(wav);
      wav.Close();
      return false;
    }
    if (bytesRead == 0) {
      break;
    }
    offset += bytesRead;
    totalRead += bytesRead;

    while (totalRead >= writeStep) {
      totalRead -= writeStep;
      ++subStep;
      updateStatus(importIndex * 10 + subStep, importCount * 10, "Importing");
    }
  }

  if (offset != sampleBytes) {
    Trace::Error("SAMPLEPOOL", "Truncated sample data: %s (%u/%u bytes)", name,
                 static_cast<unsigned>(offset),
                 static_cast<unsigned>(sampleBytes));
    if (usingDedicatedStore) {
      writeOffset_ = initialWriteOffset;
    }
    freeSampleBuffer(wav);
    wav.Close();
    return false;
  }

  std::strncpy(nameStore_[count_], name, MAX_INSTRUMENT_FILENAME_LENGTH);
  nameStore_[count_][MAX_INSTRUMENT_FILENAME_LENGTH] = '\0';
  count_++;
  if (usingDedicatedStore) {
    writeOffset_ = alignedOffset + offset;
  }

  wav_[count_ - 1].Close();
  return true;
}

bool NodeSamplePool::unloadSample(uint32_t index) {
  if (index >= count_) {
    return false;
  }

  if (sampleStore_ != nullptr &&
      isDedicatedSampleBuffer(wav_[index].GetSampleBuffer(0))) {
    auto *moveDst = static_cast<uint8_t *>(wav_[index].GetSampleBuffer(0));
    if (moveDst == nullptr) {
      Trace::Error("SAMPLEPOOL", "Invalid dedicated sample buffer");
      return false;
    }

    const uint32_t moveDstOffset =
        static_cast<uint32_t>(moveDst - sampleStore_);
    if (moveDstOffset >= writeOffset_) {
      Trace::Error("SAMPLEPOOL", "Invalid sample address while deleting");
      return false;
    }

    uint8_t *moveSrc = nullptr;
    for (uint32_t j = 0; j < count_; ++j) {
      if (j == index) {
        continue;
      }
      auto *candidate = static_cast<uint8_t *>(wav_[j].GetSampleBuffer(0));
      if (candidate != nullptr && candidate > moveDst &&
          candidate < sampleStore_ + writeOffset_) {
        if (moveSrc == nullptr || candidate < moveSrc) {
          moveSrc = candidate;
        }
      }
    }

    uint32_t shift = 0;
    uint32_t bytesToMove = 0;
    if (moveSrc != nullptr) {
      shift = static_cast<uint32_t>(moveSrc - moveDst);
      bytesToMove = writeOffset_ - static_cast<uint32_t>(moveSrc - sampleStore_);
    } else {
      shift = writeOffset_ - moveDstOffset;
    }

    if (bytesToMove > 0) {
      std::memmove(moveDst, moveSrc, bytesToMove);
    }

    writeOffset_ -= shift;

    if (shift > 0 && moveSrc != nullptr) {
      for (uint32_t j = 0; j < count_; ++j) {
        if (j == index) {
          continue;
        }
        auto *buf = static_cast<uint8_t *>(wav_[j].GetSampleBuffer(0));
        if (buf != nullptr && buf >= moveSrc && buf < sampleStore_ + storeLimit_) {
          wav_[j].SetSampleBuffer(reinterpret_cast<int16_t *>(buf - shift));
        }
      }
    }

    for (uint32_t j = index; j < count_ - 1; ++j) {
      wav_[j] = std::move(wav_[j + 1]);
      std::memcpy(nameStore_[j], nameStore_[j + 1],
                  MAX_INSTRUMENT_FILENAME_LENGTH + 1);
    }

    wav_[count_ - 1].Close();
    wav_[count_ - 1].SetSampleBuffer(nullptr);
    nameStore_[count_ - 1][0] = '\0';
    --count_;

    SetChanged();
    SamplePoolEvent ev;
    ev.index_ = static_cast<int>(index);
    ev.type_ = SPET_DELETE;
    NotifyObservers(&ev);
    return true;
  }

  freeSampleBuffer(wav_[index]);
  wav_[index].Close();

  for (uint32_t j = index; j < count_ - 1; ++j) {
    wav_[j] = std::move(wav_[j + 1]);
    std::memcpy(nameStore_[j], nameStore_[j + 1],
                MAX_INSTRUMENT_FILENAME_LENGTH + 1);
  }

  wav_[count_ - 1].Close();
  wav_[count_ - 1].SetSampleBuffer(nullptr);
  nameStore_[count_ - 1][0] = '\0';
  --count_;

  SetChanged();
  SamplePoolEvent ev;
  ev.index_ = static_cast<int>(index);
  ev.type_ = SPET_DELETE;
  NotifyObservers(&ev);
  return true;
}
