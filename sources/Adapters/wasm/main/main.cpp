#include "Adapters/wasm/platform/wasm_bridge.h"

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>

int main() {
  PicoTracker_Wasm_MarkReady();
  while (PicoTracker_Wasm_GetState() ==
         static_cast<std::uint32_t>(WasmRuntimeState::Ready)) {
    emscripten_thread_sleep(10.0);
  }
  if (PicoTracker_Wasm_GetState() ==
      static_cast<std::uint32_t>(WasmRuntimeState::Stopping)) {
    PicoTracker_Wasm_MarkStopped();
  }
  emscripten_exit_with_live_runtime();
  return 0;
}
