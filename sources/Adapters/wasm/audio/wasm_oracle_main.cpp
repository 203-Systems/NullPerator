/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "AudioWorklet.h"

#include <emscripten/emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE const WasmAudioRenderOracle *
PicoTracker_Wasm_GetRenderOracle(std::uint32_t destinationRate) {
  static WasmAudioRenderOracle oracle{};
  oracle = WasmAudioWorkletRenderer::RenderOracle(destinationRate);
  return &oracle;
}
