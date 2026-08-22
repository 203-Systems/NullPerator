/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "OutputResampler.h"

#include <algorithm>

OutputResampler::OutputResampler(std::uint32_t sourceRate,
                                 std::uint32_t destinationRate) noexcept
    : sourceRate_(sourceRate), destinationRate_(destinationRate) {}

ResamplerProcessResult
OutputResampler::Process(std::span<const StereoF32> input,
                         std::span<StereoF32> output) noexcept {
  ResamplerProcessResult result{};
  if (finished_ || sourceRate_ == 0U || destinationRate_ == 0U ||
      output.empty()) {
    return result;
  }

  if (sourceRate_ == destinationRate_) {
    const std::size_t copied = std::min(input.size(), output.size());
    for (std::size_t index = 0; index < copied; ++index) {
      output[index] = input[index];
    }
    result.inputFramesConsumed = copied;
    result.outputFramesProduced = copied;
    return result;
  }

  while (result.outputFramesProduced < output.size()) {
    if (hasSegment_) {
      if (!DrainSegment(output, result.outputFramesProduced)) {
        break;
      }
      continue;
    }

    if (result.inputFramesConsumed == input.size()) {
      break;
    }

    if (!hasPrevious_) {
      previous_ = input[result.inputFramesConsumed++];
      hasPrevious_ = true;
      previousFrameIndex_ = 0U;
      continue;
    }

    current_ = input[result.inputFramesConsumed++];
    hasSegment_ = true;
    tailSegment_ = false;
  }
  return result;
}

ResamplerProcessResult
OutputResampler::Flush(std::span<StereoF32> output) noexcept {
  ResamplerProcessResult result{};
  if (finished_ || sourceRate_ == 0U || destinationRate_ == 0U ||
      output.empty() || sourceRate_ == destinationRate_) {
    return result;
  }

  while (result.outputFramesProduced < output.size()) {
    if (hasSegment_) {
      if (!DrainSegment(output, result.outputFramesProduced)) {
        break;
      }
      continue;
    }
    if (!hasPrevious_) {
      finished_ = true;
      break;
    }

    // The final source sample has no natural look-ahead sample.  Extend its
    // final slope by one frame (or duplicate a one-frame stream).  A DC input
    // remains exactly DC, while a changing endpoint retains linear-resampler
    // accuracy through the final fractional destination frame.
    current_ = previous_;
    if (hasBeforePrevious_) {
      current_.left += previous_.left - beforePrevious_.left;
      current_.right += previous_.right - beforePrevious_.right;
    }
    hasSegment_ = true;
    tailSegment_ = true;
  }
  return result;
}

void OutputResampler::Reset() noexcept {
  phaseNumerator_ = 0U;
  previousFrameIndex_ = 0U;
  beforePrevious_ = {};
  previous_ = {};
  current_ = {};
  hasBeforePrevious_ = false;
  hasPrevious_ = false;
  hasSegment_ = false;
  tailSegment_ = false;
  finished_ = false;
}

bool OutputResampler::DrainSegment(std::span<StereoF32> output,
                                   std::size_t &produced) noexcept {
  const std::uint64_t segmentEnd =
      (previousFrameIndex_ + 1U) * static_cast<std::uint64_t>(destinationRate_);
  while (produced < output.size() && phaseNumerator_ < segmentEnd) {
    const float fraction = static_cast<float>(phaseNumerator_ % destinationRate_) /
                           static_cast<float>(destinationRate_);
    output[produced++] = Interpolate(fraction);
    phaseNumerator_ += sourceRate_;
  }

  if (phaseNumerator_ < segmentEnd) {
    return false;
  }

  hasSegment_ = false;
  if (tailSegment_) {
    tailSegment_ = false;
    finished_ = true;
    return true;
  }
  beforePrevious_ = previous_;
  hasBeforePrevious_ = true;
  previous_ = current_;
  ++previousFrameIndex_;
  return true;
}

StereoF32 OutputResampler::Interpolate(float fraction) const noexcept {
  const float left = previous_.left + (current_.left - previous_.left) * fraction;
  const float right = previous_.right + (current_.right - previous_.right) * fraction;
  return {std::clamp(left, -1.0F, 1.0F), std::clamp(right, -1.0F, 1.0F)};
}
