/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "AudioWorklet.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <span>

WasmAudioRenderOracle WasmAudioWorkletRenderer::RenderOracle(
    std::uint32_t destinationRate) noexcept {
  WasmAudioRenderOracle oracle{};
  oracle.destinationRate = destinationRate;
  if (destinationRate == 0U) {
    return oracle;
  }
  std::array<StereoF32, 128U> source{};
  for (std::size_t index = 0U; index < source.size(); ++index) {
    const auto value = static_cast<float>(static_cast<int>(index * 509U % 32768U) -
                                          16384) /
                       32768.0F;
    source[index] = {value, -value};
  }
  std::array<StereoF32, 160U> output{};
  OutputResampler resampler(44100U, destinationRate);
  const auto first = resampler.Process(source, output);
  std::size_t produced = first.outputFramesProduced;
  const auto tail = resampler.Flush(
      std::span<StereoF32>(output.data() + produced, output.size() - produced));
  produced += tail.outputFramesProduced;
  oracle.producedFrames = static_cast<std::uint32_t>(produced);
  std::uint32_t hash = 2166136261U;
  float peak = 0.0F;
  for (std::size_t index = 0U; index < produced; ++index) {
    hash = (hash ^ std::bit_cast<std::uint32_t>(output[index].left)) * 16777619U;
    hash = (hash ^ std::bit_cast<std::uint32_t>(output[index].right)) * 16777619U;
    peak = std::max(peak, std::max(std::abs(output[index].left),
                                   std::abs(output[index].right)));
  }
  oracle.sampleHash = hash;
  oracle.peakQ15 = static_cast<std::uint32_t>(peak * 32768.0F + 0.5F);
  return oracle;
}
