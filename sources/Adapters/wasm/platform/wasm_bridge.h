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
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_ConfigureAudio(
    std::uint32_t targetFillFrames, std::uint32_t outputGainQ16);
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_RequestShutdown();
EMSCRIPTEN_KEEPALIVE const char *PicoTracker_Wasm_GetLastError();
// Acceptance diagnostic used to prove the runtime-fatal recovery lifecycle.
// It is never called during normal application operation.
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_DiagnosticFail();
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_SetAction(std::uint16_t action,
                                                     bool pressed);
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_ReleaseAllActions();
EMSCRIPTEN_KEEPALIVE std::uint16_t PicoTracker_Wasm_GetActionMask();
EMSCRIPTEN_KEEPALIVE std::uint32_t PicoTracker_Wasm_GetActionGeneration();
EMSCRIPTEN_KEEPALIVE std::uint16_t PicoTracker_Wasm_GetLastAction();
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_SetUi2Enabled(bool enabled);
EMSCRIPTEN_KEEPALIVE bool PicoTracker_Wasm_GetUi2Enabled();
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_RequestDiagnosticView(
    std::uint32_t viewType);
EMSCRIPTEN_KEEPALIVE std::uint32_t PicoTracker_Wasm_GetDiagnosticView();
EMSCRIPTEN_KEEPALIVE std::uint32_t
PicoTracker_Wasm_GetDiagnosticViewGeneration();
EMSCRIPTEN_KEEPALIVE std::uint32_t
PicoTracker_Wasm_GetDiagnosticInputGeneration();
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_RequestDiagnosticModal(
    std::uint32_t modalType);
EMSCRIPTEN_KEEPALIVE std::uint32_t PicoTracker_Wasm_GetDiagnosticModal();
EMSCRIPTEN_KEEPALIVE std::uint32_t
PicoTracker_Wasm_GetDiagnosticModalGeneration();
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
