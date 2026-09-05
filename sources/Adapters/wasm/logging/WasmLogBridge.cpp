/* SPDX-License-Identifier: BSD-3-Clause */

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_LOG_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_LOG_EXPORT
#endif

#include "Adapters/common/logging/BufferedTrace.h"

extern "C" WASM_LOG_EXPORT std::uintptr_t PicoTracker_Wasm_LogDrain() {
  BufferedTrace *trace = BufferedTrace::Instance();
  return trace == nullptr ? 0 : trace->Drain();
}
