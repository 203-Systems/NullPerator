#include "Adapters/wasm/platform/wasm_bridge.h"

#include "Adapters/wasm/input/InputMap.h"
#include "Adapters/wasm/audio/WasmAudioBridge.h"
#include "Adapters/wasm/platform/WasmBrowserSnapshots.h"

#include <atomic>
#include <cstring>

#define PICOTRACKER_STRINGIFY_INNER(value) #value
#define PICOTRACKER_STRINGIFY(value) PICOTRACKER_STRINGIFY_INNER(value)

namespace {
std::atomic<std::uint32_t> runtimeState{
    static_cast<std::uint32_t>(WasmRuntimeState::Booting)};
char lastError[256] = {};

constexpr char buildMetadata[] =
    "{\"commit\":\"" PICOTRACKER_WASM_BUILD_COMMIT
    "\",\"dirty\":" PICOTRACKER_STRINGIFY(PICOTRACKER_WASM_BUILD_DIRTY)
    ",\"builtAt\":\"" PICOTRACKER_WASM_BUILD_TIME "\"}";
} // namespace

extern "C" const char *PicoTracker_Wasm_GetBuildMetadataJson() {
  return buildMetadata;
}

extern "C" std::uint32_t PicoTracker_Wasm_GetState() {
  return runtimeState.load(std::memory_order_acquire);
}

extern "C" void PicoTracker_Wasm_BootstrapAudio() {
  WasmAudio_BootstrapBrowserMain();
}

extern "C" void PicoTracker_Wasm_MarkAudioUnavailable() {
  WasmAudio_MarkUnavailable();
}

extern "C" void PicoTracker_Wasm_RequestShutdown() {
  PicoTracker_Wasm_ReleaseAllActions();
  // Detach the graph and stop the callback producer before the application
  // thread closes AudioDriver and is allowed to publish Stopped.
  WasmAudio_Stop();
  runtimeState.store(static_cast<std::uint32_t>(WasmRuntimeState::Stopping),
                     std::memory_order_release);
}

extern "C" const char *PicoTracker_Wasm_GetLastError() { return lastError; }

extern "C" void PicoTracker_Wasm_SetAction(std::uint16_t action, bool pressed) {
  InputMap::SetAction(action, pressed);
}

extern "C" void PicoTracker_Wasm_ReleaseAllActions() {
  InputMap::ReleaseAllActions();
}

extern "C" std::uint16_t PicoTracker_Wasm_GetActionMask() {
  return InputMap::GetDispatchedActionMask();
}

extern "C" std::uint32_t PicoTracker_Wasm_GetActionGeneration() {
  return InputMap::GetDispatchGeneration();
}

extern "C" std::uint16_t PicoTracker_Wasm_GetLastAction() {
  return InputMap::GetLastDispatchedAction();
}

extern "C" int PicoTracker_Wasm_UnlockAudio() {
  return WasmAudio_Unlock() ? 1 : 0;
}

extern "C" void PicoTracker_Wasm_StopAudio() { WasmAudio_Stop(); }

extern "C" std::uint32_t PicoTracker_Wasm_GetAudioState() {
  return static_cast<std::uint32_t>(WasmAudio_GetState());
}

extern "C" const char *PicoTracker_Wasm_GetAudioError() {
  return WasmAudio_GetError();
}

extern "C" const void *PicoTracker_Wasm_GetAudioMetrics() {
  return WasmAudio_CopyMetrics();
}

extern "C" const std::uint32_t *PicoTracker_Wasm_GetAudioMetricsSnapshot() {
  return WasmAudio_MetricsSnapshotAddress();
}

extern "C" const std::uint32_t *PicoTracker_Wasm_GetAudioErrorSnapshot() {
  return WasmAudio_ErrorSnapshotAddress();
}

extern "C" const void *PicoTracker_Wasm_GetBrowserSnapshots() {
  return Wasm_BrowserSnapshots();
}

void PicoTracker_Wasm_MarkReady() {
  runtimeState.store(static_cast<std::uint32_t>(WasmRuntimeState::Ready),
                     std::memory_order_release);
}

void PicoTracker_Wasm_MarkStopped() {
  runtimeState.store(static_cast<std::uint32_t>(WasmRuntimeState::Stopped),
                     std::memory_order_release);
}

void PicoTracker_Wasm_Fail(const char *message) {
  if (message == nullptr) {
    message = "Unknown WASM runtime failure";
  }
  std::strncpy(lastError, message, sizeof(lastError) - 1);
  lastError[sizeof(lastError) - 1] = '\0';
  runtimeState.store(static_cast<std::uint32_t>(WasmRuntimeState::Failed),
                     std::memory_order_release);
}
