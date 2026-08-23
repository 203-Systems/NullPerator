#pragma once

#include <cstdint>

#include <emscripten/emscripten.h>

enum class WasmRuntimeState : std::uint32_t {
  Booting = 0,
  Ready = 1,
  Stopping = 2,
  Failed = 3,
  Stopped = 4,
};

extern "C" {
EMSCRIPTEN_KEEPALIVE const char *PicoTracker_Wasm_GetBuildMetadataJson();
EMSCRIPTEN_KEEPALIVE std::uint32_t PicoTracker_Wasm_GetState();
// Must be invoked from Emscripten's browser-main onRuntimeInitialized hook,
// before PROXY_TO_PTHREAD enters the tracker application main.
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_BootstrapAudio();
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_MarkAudioUnavailable();
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_RequestShutdown();
EMSCRIPTEN_KEEPALIVE const char *PicoTracker_Wasm_GetLastError();
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_SetAction(std::uint16_t action,
                                                     bool pressed);
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_ReleaseAllActions();
EMSCRIPTEN_KEEPALIVE std::uint16_t PicoTracker_Wasm_GetActionMask();
EMSCRIPTEN_KEEPALIVE std::uint32_t PicoTracker_Wasm_GetActionGeneration();
EMSCRIPTEN_KEEPALIVE std::uint16_t PicoTracker_Wasm_GetLastAction();
EMSCRIPTEN_KEEPALIVE int PicoTracker_Wasm_UnlockAudio();
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_StopAudio();
EMSCRIPTEN_KEEPALIVE std::uint32_t PicoTracker_Wasm_GetAudioState();
EMSCRIPTEN_KEEPALIVE const char *PicoTracker_Wasm_GetAudioError();
EMSCRIPTEN_KEEPALIVE const void *PicoTracker_Wasm_GetAudioMetrics();
EMSCRIPTEN_KEEPALIVE const std::uint32_t *PicoTracker_Wasm_GetAudioMetricsSnapshot();
EMSCRIPTEN_KEEPALIVE const std::uint32_t *PicoTracker_Wasm_GetAudioErrorSnapshot();
EMSCRIPTEN_KEEPALIVE const void *PicoTracker_Wasm_GetBrowserSnapshots();
}

void PicoTracker_Wasm_MarkReady();
void PicoTracker_Wasm_MarkStopped();
void PicoTracker_Wasm_Fail(const char *message);
