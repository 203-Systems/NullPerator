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
EMSCRIPTEN_KEEPALIVE void PicoTracker_Wasm_RequestShutdown();
EMSCRIPTEN_KEEPALIVE const char *PicoTracker_Wasm_GetLastError();
}

void PicoTracker_Wasm_MarkReady();
void PicoTracker_Wasm_MarkStopped();
void PicoTracker_Wasm_Fail(const char *message);
