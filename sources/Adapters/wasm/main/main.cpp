#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Adapters/wasm/system/WasmSystem.h"

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>

int main() {
  if (!WasmSystem::InstallPlatformServices()) {
    PicoTracker_Wasm_Fail("Failed to install browser platform services");
    emscripten_exit_with_live_runtime();
    return 1;
  }
  PicoTracker_Wasm_MarkReady();
  while (PicoTracker_Wasm_GetState() ==
         static_cast<std::uint32_t>(WasmRuntimeState::Ready)) {
    emscripten_thread_sleep(10.0);
  }
  if (PicoTracker_Wasm_GetState() ==
      static_cast<std::uint32_t>(WasmRuntimeState::Stopping)) {
    WasmSystem::ShutdownPlatformServices();
    PicoTracker_Wasm_MarkStopped();
  }
  emscripten_exit_with_live_runtime();
  return 0;
}
