/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file is part of the node firmware
 */

#ifndef _NODE_SAMPLEPOOL_H_
#define _NODE_SAMPLEPOOL_H_

#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/WavFile.h"
#include "System/Console/Trace.h"
#include <esp_heap_caps.h>
#include <optional>

class NodeSamplePool : public SamplePool {
public:
  NodeSamplePool();
  ~NodeSamplePool() override = default;

  void Reset() override;
  bool CheckSampleFits(int sampleSize) override;
  uint32_t GetAvailableSampleStorageSpace() override;
  bool unloadSample(uint32_t index) override;

protected:
  bool loadSample(const char *name) override;

private:
  void freeSampleBuffer(WavFile &wave);
  std::optional<void *> allocSampleBuffer(size_t bytes);
};

#endif
