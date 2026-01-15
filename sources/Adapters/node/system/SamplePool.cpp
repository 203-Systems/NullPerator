/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file is part of the esp32 firmware
 */

#include "SamplePool.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <utility>

static bool has_psram() {
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
}

NodeSamplePool::NodeSamplePool() : SamplePool() {}

void NodeSamplePool::freeSampleBuffer(WavFile *wave) {
  if (!wave) {
    return;
  }
  void *buffer = wave->GetSampleBuffer(0);
  if (buffer != nullptr) {
    heap_caps_free(buffer);
    wave->SetSampleBuffer(nullptr);
  }
}

std::optional<void *> NodeSamplePool::allocSampleBuffer(size_t bytes) {
  // Prefer PSRAM if available, fall back to internal heap.
  if (has_psram()) {
    void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
      return ptr;
    }
  }
  void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  if (ptr != nullptr) {
    return ptr;
  }
  return std::nullopt;
}

void NodeSamplePool::Reset() {
  count_ = 0;
  for (int i = 0; i < MAX_SAMPLES; i++) {
    freeSampleBuffer(&wav_[i]);
    wav_[i].Close();
    nameStore_[i][0] = '\0';
  }
}

bool NodeSamplePool::CheckSampleFits(int sampleSize) {
  // Use a conservative estimate of available heap (PSRAM preferred).
  size_t freeBytes = has_psram() ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM)
                                 : heap_caps_get_free_size(MALLOC_CAP_8BIT);
  return freeBytes > static_cast<size_t>(sampleSize + 8192);
}

uint32_t NodeSamplePool::GetAvailableSampleStorageSpace() {
  size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (has_psram()) {
    freeBytes += heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  }
  return static_cast<uint32_t>(freeBytes);
}

bool NodeSamplePool::loadSample(const char *name) {
  Trace::Log("SAMPLEPOOL", "Loading sample into heap: %s", name);

  if (count_ == MAX_SAMPLES) {
    return false;
  }

  auto res = wav_[count_].Open(name);
  if (!res) {
    Trace::Error("SAMPLEPOOL", "Failed to open sample: %s", name);
    return false;
  }

  const uint32_t sampleBytes = wav_[count_].GetDiskSize(-1);
  if (!CheckSampleFits(static_cast<int>(sampleBytes))) {
    Trace::Error("SAMPLEPOOL", "Not enough heap for sample (%u bytes)",
                 sampleBytes);
    wav_[count_].Close();
    return false;
  }

  auto buffer = allocSampleBuffer(sampleBytes);
  if (!buffer.has_value()) {
    Trace::Error("SAMPLEPOOL", "Heap alloc failed for sample (%u bytes)",
                 sampleBytes);
    wav_[count_].Close();
    return false;
  }

  wav_[count_].SetSampleBuffer(static_cast<int16_t *>(buffer.value()));

  uint32_t offset = 0;
  uint32_t bytesRead = 0;
  wav_[count_].Rewind();
  while (offset < sampleBytes) {
    uint32_t toRead = sampleBytes - offset;
    if (toRead > BUFFER_SIZE) {
      toRead = BUFFER_SIZE;
    }
    if (!wav_[count_].Read(static_cast<uint8_t *>(buffer.value()) + offset,
                           toRead, &bytesRead)) {
      Trace::Error("SAMPLEPOOL", "Failed reading sample data: %s", name);
      freeSampleBuffer(&wav_[count_]);
      wav_[count_].Close();
      return false;
    }
    if (bytesRead == 0) {
      break;
    }
    offset += bytesRead;
  }

  strncpy(nameStore_[count_], name, MAX_INSTRUMENT_FILENAME_LENGTH);
  nameStore_[count_][MAX_INSTRUMENT_FILENAME_LENGTH] = '\0';
  count_++;

  wav_[count_ - 1].Close();
  return true;
}

bool NodeSamplePool::unloadSample(uint32_t index) {
  if (index >= count_) {
    return false;
  }

  freeSampleBuffer(&wav_[index]);

  // shift remaining entries down
  for (uint32_t j = index; j < count_ - 1; ++j) {
    wav_[j] = std::move(wav_[j + 1]);
    wav_[j + 1].SetSampleBuffer(nullptr);
    memcpy(nameStore_[j], nameStore_[j + 1], MAX_INSTRUMENT_FILENAME_LENGTH + 1);
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
