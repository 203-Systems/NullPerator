/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "AudioWorklet.h"

#include "WasmAudioBridge.h"
#include "WasmAudioDriver.h"

#include <algorithm>
#include <array>
#include <span>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

WasmAudioWorkletRenderer::WasmAudioWorkletRenderer(
    WasmAudioDriver &driver, std::uint32_t destinationRate) noexcept
    : driver_(&driver), resampler_(44100U, destinationRate) {
  driver_->SetDestinationRate(destinationRate);
}

void WasmAudioWorkletRenderer::SetDestinationRate(
    std::uint32_t destinationRate) noexcept {
  resampler_ = OutputResampler(44100U, destinationRate);
  if (driver_ != nullptr) {
    driver_->SetDestinationRate(destinationRate);
  }
  sourceOffset_ = 0U;
  sourceCount_ = 0U;
}

void WasmAudioWorkletRenderer::RecordCallback(
    double callbackMilliseconds, std::size_t frames) noexcept {
  driver_->RecordCallback(callbackMilliseconds, frames);
}

bool WasmAudioWorkletRenderer::Render(float *left, float *right,
                                      std::size_t frames) noexcept {
  if (left == nullptr || right == nullptr || frames > MaxFramesPerCallback ||
      driver_ == nullptr) {
    return false;
  }
  if (!driver_->IsStarted() || !driver_->IsActive()) {
    std::fill_n(left, frames, 0.0F);
    std::fill_n(right, frames, 0.0F);
    return true;
  }

  std::array<StereoF32, MaxFramesPerCallback> rendered{};
  std::size_t produced = 0U;
  while (produced < frames) {
    if (sourceOffset_ == sourceCount_ && !Refill()) {
      break;
    }
    const auto result = resampler_.Process(
        std::span<const StereoF32>(source_.data() + sourceOffset_,
                                   sourceCount_ - sourceOffset_),
        std::span<StereoF32>(rendered.data() + produced, frames - produced));
    sourceOffset_ += result.inputFramesConsumed;
    produced += result.outputFramesProduced;
    if (result.inputFramesConsumed == 0U && result.outputFramesProduced == 0U) {
      break;
    }
  }
  const float gain = static_cast<float>(driver_->OutputGainQ16()) /
                     static_cast<float>(WasmAudioDriver::UnityGainQ16);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const StereoF32 value = frame < produced ? rendered[frame] : StereoF32{};
    left[frame] = value.left * gain;
    right[frame] = value.right * gain;
  }
  return true;
}

bool WasmAudioWorkletRenderer::Refill() noexcept {
  sourceOffset_ = 0U;
  sourceCount_ = driver_->ReadFrames(source_);
  // PcmRingBuffer intentionally zero-fills an underflow. Keep that entire
  // fixed buffer as source so the resampler's phase stays deterministic.
  if (sourceCount_ < source_.size()) {
    sourceCount_ = source_.size();
  }
  return sourceCount_ != 0U;
}

#ifdef __EMSCRIPTEN__
extern "C" bool PicoTracker_Wasm_AudioWorkletProcess(
    int numInputs, const AudioSampleFrame *inputs, int numOutputs,
    AudioSampleFrame *outputs, int numParams, const AudioParamFrame *params,
    void *userData) noexcept {
  (void)numInputs;
  (void)inputs;
  (void)numParams;
  (void)params;
  auto *renderer = static_cast<WasmAudioWorkletRenderer *>(userData);
  if (renderer == nullptr || numOutputs != 1 || outputs == nullptr ||
      outputs[0].numberOfChannels < 2 || outputs[0].samplesPerChannel < 0) {
    return false;
  }
  const std::size_t frames = static_cast<std::size_t>(outputs[0].samplesPerChannel);
  float *const planar = outputs[0].data;
  if (planar == nullptr) {
    return false;
  }
  const double callbackStart = emscripten_get_now();
  if (!renderer->Render(planar, planar + frames, frames)) {
    return false;
  }
  WasmAudio_MarkRunning();
  const double callbackEnd = emscripten_get_now();
  renderer->RecordCallback(callbackEnd - callbackStart, frames);
  return true;
}
#endif
