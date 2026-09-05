/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Application/Instruments/SamplePool.h"

#include <array>
#include <cstdint>

// Decoded samples use stable heap allocations for the loaded project. The
// storage adapter owns source files; the audio renderer only reads this pool.
class HeapSamplePool final : public SamplePool {
public:
  HeapSamplePool() = default;
  ~HeapSamplePool() override;

  void Reset() override;
  bool CheckSampleFits(int sampleSize) override;
  std::uint32_t GetAvailableSampleStorageSpace() override;

protected:
  bool loadSample(const char *name) override;
  bool unloadSample(std::uint32_t index) override;

private:
  void ReleaseSample(std::uint32_t index);

  std::array<std::uint8_t *, MAX_SAMPLES> buffers_{};
};
