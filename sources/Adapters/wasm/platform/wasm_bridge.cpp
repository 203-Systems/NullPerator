#include "Adapters/wasm/platform/wasm_bridge.h"

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

extern "C" void PicoTracker_Wasm_RequestShutdown() {
  runtimeState.store(static_cast<std::uint32_t>(WasmRuntimeState::Stopping),
                     std::memory_order_release);
}

extern "C" const char *PicoTracker_Wasm_GetLastError() { return lastError; }

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
