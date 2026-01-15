/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file is part of the esp32 firmware
 */

#include "SamplePool.h"

#include <cstring>
#include <cstdlib>
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
    freeSampleBuffer(static_cast<WavFile *>(wav_[i]));
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
    Trace::Error("Failed to load sample:%s", name);
    return false;
  }

  uint32_t fileSize = wave.value()->GetDiskSize(-1);
  if (!CheckSampleFits(static_cast<int>(fileSize))) {
    Trace::Error("Not enough heap for sample (%u bytes)", fileSize);
    delete wave.value();
    return false;
  }

  // Allocate buffer and copy sample data
  auto buffer = allocSampleBuffer(fileSize);
  if (!buffer.has_value()) {
    Trace::Error("Heap alloc failed for sample (%u bytes)", fileSize);
    delete wave.value();
    return false;
  }

  wave.value()->SetSampleBuffer(static_cast<short *>(buffer.value()));

  uint32_t br = 0;
  uint32_t offset = 0;
  wave.value()->Rewind();
  do {
    wave.value()->Read(static_cast<uint8_t *>(buffer.value()) + offset, BUFFER_SIZE,
                       &br);
    offset += br;
  } while (br > 0);

  wav_[count_] = wave.value();
  names_[count_] = static_cast<char *>(malloc(strlen(name) + 1));
  strcpy(names_[count_], name);
  count_++;

  wave.value()->Close();
  return true;
}

bool NodeSamplePool::unloadSample(int index) {
  if (index < 0 || index >= count_) {
    return false;
  }

  freeSampleBuffer(static_cast<WavFile *>(wav_[index]));
  std::free(names_[index]);

  // shift remaining entries down
  for (int j = index; j < count_ - 1; ++j) {
    wav_[j] = wav_[j + 1];
    names_[j] = names_[j + 1];
  }

  wav_[count_ - 1] = nullptr;
  names_[count_ - 1] = nullptr;
  --count_;
  return true;
}
