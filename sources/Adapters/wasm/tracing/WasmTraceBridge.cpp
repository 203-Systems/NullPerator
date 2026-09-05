/* SPDX-License-Identifier: BSD-3-Clause */

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_TRACE_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_TRACE_EXPORT
#endif

#include "System/Console/Profiler.h"

extern "C" WASM_TRACE_EXPORT std::uint32_t
PicoTracker_Wasm_TraceStart(std::uint32_t mask) {
#ifdef __EMSCRIPTEN__
  Profiler::SetClock(+[] {
    return static_cast<std::uint64_t>(emscripten_get_now() * 1000.0);
  });
#endif
  return Profiler::Start(mask);
}
extern "C" WASM_TRACE_EXPORT std::uint32_t PicoTracker_Wasm_TraceStop() {
  return Profiler::Stop();
}
extern "C" WASM_TRACE_EXPORT std::uintptr_t PicoTracker_Wasm_TraceDrain() {
  return Profiler::Drain();
}
extern "C" WASM_TRACE_EXPORT void
PicoTracker_Wasm_TraceStorageSync(std::uint32_t phase, std::uint32_t syncId,
                                  std::uint32_t flags,
                                  std::uint32_t expectedGeneration) {
  if (phase > static_cast<std::uint32_t>(TracePhase::End) || syncId == 0)
    return;
  constexpr std::uint32_t AllowedFlags =
      static_cast<std::uint32_t>(TraceFlag::Success) |
      static_cast<std::uint32_t>(TraceFlag::Failure) |
      static_cast<std::uint32_t>(TraceFlag::Populate);
  Profiler::Emit(TraceCategory::Storage, TraceName::StorageSync,
                 static_cast<TracePhase>(phase), syncId, TraceThread::Browser,
                 expectedGeneration,
                 static_cast<std::uint16_t>(flags & AllowedFlags));
}
