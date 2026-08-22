/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/system/WasmSamplePool.h"

#include "System/Console/Trace.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace {
constexpr std::uint32_t SampleHeapBudget = 32U * 1024U * 1024U;
}

WasmSamplePool::~WasmSamplePool() { Reset(); }

void WasmSamplePool::ReleaseSample(std::uint32_t index) {
  if (index >= MAX_SAMPLES) {
    return;
  }
  delete[] buffers_[index];
  buffers_[index] = nullptr;
  wav_[index].SetSampleBuffer(nullptr);
  wav_[index].Close();
  nameStore_[index][0] = '\0';
}

void WasmSamplePool::Reset() {
  for (std::uint32_t index = 0; index < MAX_SAMPLES; ++index) {
    ReleaseSample(index);
  }
  count_ = 0;
}

std::uint32_t WasmSamplePool::GetAvailableSampleStorageSpace() {
  std::uint64_t used = 0;
  for (std::uint32_t index = 0; index < count_; ++index) {
    used += wav_[index].GetDiskSize(-1);
  }
  return used >= SampleHeapBudget
             ? 0
             : static_cast<std::uint32_t>(SampleHeapBudget - used);
}

bool WasmSamplePool::CheckSampleFits(int sampleSize) {
  return sampleSize > 0 &&
         static_cast<std::uint32_t>(sampleSize) <= GetAvailableSampleStorageSpace();
}

bool WasmSamplePool::loadSample(const char *name) {
  if (name == nullptr || count_ >= MAX_SAMPLES) {
    return false;
  }

  WavFile &wave = wav_[count_];
  const auto opened = wave.Open(name);
  if (!opened) {
    Trace::Error("SAMPLEPOOL", "Failed to open sample: %s", name);
    return false;
  }

  const std::uint32_t sampleBytes = wave.GetDiskSize(-1);
  if (sampleBytes == 0) {
    Trace::Error("SAMPLEPOOL", "Sample contains no audio data: %s", name);
    wave.Close();
    return false;
  }
  if (!CheckSampleFits(static_cast<int>(sampleBytes))) {
    Trace::Error("SAMPLEPOOL", "Not enough WASM heap for sample: %s", name);
    wave.Close();
    return false;
  }

  auto *buffer = new (std::nothrow) std::uint8_t[sampleBytes];
  if (buffer == nullptr) {
    Trace::Error("SAMPLEPOOL", "Allocation failed for sample: %s", name);
    wave.Close();
    return false;
  }

  std::uint32_t offset = 0;
  while (offset < sampleBytes) {
    std::uint32_t bytesRead = 0;
    const std::uint32_t bytesToRead =
        std::min<std::uint32_t>(BUFFER_SIZE, sampleBytes - offset);
    if (!wave.Read(buffer + offset, bytesToRead, &bytesRead) ||
        bytesRead == 0) {
      delete[] buffer;
      wave.Close();
      Trace::Error("SAMPLEPOOL", "Failed reading sample: %s", name);
      return false;
    }
    offset += bytesRead;
  }

  buffers_[count_] = buffer;
  wave.SetSampleBuffer(reinterpret_cast<short *>(buffer));
  std::strncpy(nameStore_[count_], name, MAX_INSTRUMENT_FILENAME_LENGTH);
  nameStore_[count_][MAX_INSTRUMENT_FILENAME_LENGTH] = '\0';
  ++count_;
  wave.Close();
  return true;
}

bool WasmSamplePool::unloadSample(std::uint32_t index) {
  if (index >= count_) {
    return false;
  }
  ReleaseSample(index);
  for (std::uint32_t next = index + 1; next < count_; ++next) {
    const std::uint32_t destination = next - 1;
    wav_[destination] = std::move(wav_[next]);
    wav_[next].SetSampleBuffer(nullptr);
    buffers_[destination] = buffers_[next];
    buffers_[next] = nullptr;
    std::memcpy(nameStore_[destination], nameStore_[next],
                MAX_INSTRUMENT_FILENAME_LENGTH + 1);
  }
  const std::uint32_t last = count_ - 1;
  wav_[last].SetSampleBuffer(nullptr);
  wav_[last].Close();
  nameStore_[last][0] = '\0';
  --count_;
  return true;
}
