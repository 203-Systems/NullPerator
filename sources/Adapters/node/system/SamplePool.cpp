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
  for (uint32_t i = 0; i < MAX_SAMPLES; ++i) {
    if (wav_[i] != nullptr) {
      freeSampleBuffer(static_cast<WavFile *>(wav_[i]));
    }
    SAFE_DELETE(wav_[i]);
    SAFE_FREE(names_[i]);
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

  auto wave = WavFile::Open(name);
  if (!wave) {
    Trace::Error("SAMPLEPOOL", "Failed to open sample: %s", name);
    return false;
  }

  WavFile *wav = wave.value();

  const uint32_t sampleBytes = wav->GetDiskSize(-1);
  if (!CheckSampleFits(static_cast<int>(sampleBytes))) {
    Trace::Error("SAMPLEPOOL", "Not enough heap for sample (%u bytes)",
                 sampleBytes);
    wav->Close();
    delete wav;
    return false;
  }

  auto buffer = allocSampleBuffer(sampleBytes);
  if (!buffer.has_value()) {
    Trace::Error("SAMPLEPOOL", "Heap alloc failed for sample (%u bytes)",
                 sampleBytes);
    wav->Close();
    delete wav;
    return false;
  }

  wav->SetSampleBuffer(static_cast<int16_t *>(buffer.value()));

  uint32_t offset = 0;
  uint32_t bytesRead = 0;
  wav->Rewind();
  while (offset < sampleBytes) {
    uint32_t toRead = sampleBytes - offset;
    if (toRead > BUFFER_SIZE) {
      toRead = BUFFER_SIZE;
    }
    if (!wav->Read(static_cast<uint8_t *>(buffer.value()) + offset, toRead,
                   &bytesRead)) {
      Trace::Error("SAMPLEPOOL", "Failed reading sample data: %s", name);
      freeSampleBuffer(wav);
      wav->Close();
      delete wav;
      return false;
    }
    if (bytesRead == 0) {
      break;
    }
    offset += bytesRead;
  }

  wav_[count_] = wav;

  names_[count_] = static_cast<char *>(SYS_MALLOC(strlen(name) + 1));
  if (names_[count_] == nullptr) {
    freeSampleBuffer(wav);
    SAFE_DELETE(wav_[count_]);
    return false;
  }
  strcpy(names_[count_], name);

  count_++;

  wav->Close();
  return true;
}

bool NodeSamplePool::unloadSample(uint32_t index) {
  if (index >= count_) {
    return false;
  }

  if (wav_[index] != nullptr) {
    freeSampleBuffer(static_cast<WavFile *>(wav_[index]));
  }
  SAFE_DELETE(wav_[index]);
  SAFE_FREE(names_[index]);

  // shift remaining entries down
  for (uint32_t j = index; j < count_ - 1; ++j) {
    wav_[j] = wav_[j + 1];
    names_[j] = names_[j + 1];
  }

  --count_;
  wav_[count_] = nullptr;
  names_[count_] = nullptr;

  SetChanged();
  SamplePoolEvent ev;
  ev.index_ = static_cast<int>(index);
  ev.type_ = SPET_DELETE;
  NotifyObservers(&ev);
  return true;
}
