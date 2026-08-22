/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmMutex.h"

bool WasmMutex::Lock() {
  mutex_.lock();
  return true;
}

void WasmMutex::Unlock() { mutex_.unlock(); }
