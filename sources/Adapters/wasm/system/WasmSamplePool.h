/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Application/Instruments/SamplePool.h"

#include <array>
#include <cstdint>

// Browser samples live in the WebAssembly heap for the lifetime of the loaded
// project. MEMFS retains the source files; the player reads the decoded sample
// data from these stable heap allocations.
class WasmSamplePool final : public SamplePool {
public:
  WasmSamplePool() = default;
  ~WasmSamplePool() override;

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
