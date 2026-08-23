/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "OutputResampler.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#ifdef __EMSCRIPTEN__
#include <emscripten/webaudio.h>
#endif

class WasmAudioDriver;

struct WasmAudioRenderOracle {
  static constexpr std::uint32_t Version = 1U;
  std::uint32_t version = Version;
  std::uint32_t size = sizeof(WasmAudioRenderOracle);
  std::uint32_t destinationRate = 0U;
  std::uint32_t producedFrames = 0U;
  std::uint32_t sampleHash = 0U;
  std::uint32_t peakQ15 = 0U;
};

// Fixed-storage pull helper used by the Emscripten worklet callback and host
// tests. Its Render method is deliberately free of allocation, logging,
// locking, filesystem operations and waits.
class WasmAudioWorkletRenderer {
public:
  static constexpr std::size_t MaxFramesPerCallback = 128U;
  static constexpr std::size_t SourceScratchFrames = 512U;

  WasmAudioWorkletRenderer(WasmAudioDriver &driver,
                           std::uint32_t destinationRate) noexcept;

  void SetDestinationRate(std::uint32_t destinationRate) noexcept;
  [[nodiscard]] bool Render(float *left, float *right,
                            std::size_t frames) noexcept;
  void RecordCallback(double callbackMilliseconds,
                      std::size_t frames) noexcept;
  // Browser-callable deterministic oracle. It uses the production boundary
  // resampler implementation but never touches the live driver or callback.
  [[nodiscard]] static WasmAudioRenderOracle RenderOracle(
      std::uint32_t destinationRate) noexcept;

private:
  [[nodiscard]] bool Refill() noexcept;

  WasmAudioDriver *driver_ = nullptr;
  OutputResampler resampler_{44100U, 44100U};
  std::array<StereoF32, SourceScratchFrames> source_{};
  std::size_t sourceOffset_ = 0U;
  std::size_t sourceCount_ = 0U;
};

#ifdef __EMSCRIPTEN__
extern "C" bool PicoTracker_Wasm_AudioWorkletProcess(
    int numInputs, const AudioSampleFrame *inputs, int numOutputs,
    AudioSampleFrame *outputs, int numParams, const AudioParamFrame *params,
    void *userData) noexcept;
#endif
