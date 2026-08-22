/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_MUTEX_H
#define PICOTRACKER_WASM_MUTEX_H

#include "System/Process/SysMutex.h"

#include <mutex>

class WasmMutex final : public SysMutex {
public:
  bool Lock() override;
  void Unlock() override;

private:
  std::recursive_mutex mutex_;
};

#endif
