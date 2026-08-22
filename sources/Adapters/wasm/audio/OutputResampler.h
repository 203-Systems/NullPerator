/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_OUTPUT_RESAMPLER_H
#define PICOTRACKER_WASM_OUTPUT_RESAMPLER_H

#include "PcmRingBuffer.h"

#include <cstddef>
#include <cstdint>
#include <span>

struct ResamplerProcessResult {
  std::size_t inputFramesConsumed = 0;
  std::size_t outputFramesProduced = 0;
};

// Deterministic linear stereo resampler with fixed, scalar state only.  It is
// streaming: advance the caller's input span by inputFramesConsumed and retain
// any unconsumed frames for the next Process call.  Flush must be called once
// after the finite source stream to duplicate the final endpoint and emit the
// final fractional destination frames.  It never allocates in Process/Flush.
class OutputResampler {
public:
  OutputResampler(std::uint32_t sourceRate,
                  std::uint32_t destinationRate) noexcept;

  [[nodiscard]] ResamplerProcessResult
  Process(std::span<const StereoF32> input,
          std::span<StereoF32> output) noexcept;

  [[nodiscard]] ResamplerProcessResult
  Flush(std::span<StereoF32> output) noexcept;

  void Reset() noexcept;

  [[nodiscard]] std::uint32_t SourceRate() const noexcept { return sourceRate_; }
  [[nodiscard]] std::uint32_t DestinationRate() const noexcept {
    return destinationRate_;
  }

private:
  [[nodiscard]] bool DrainSegment(std::span<StereoF32> output,
                                  std::size_t &produced) noexcept;
  [[nodiscard]] StereoF32 Interpolate(float fraction) const noexcept;

  std::uint32_t sourceRate_ = 0U;
  std::uint32_t destinationRate_ = 0U;
  std::uint64_t phaseNumerator_ = 0U;
  std::uint64_t previousFrameIndex_ = 0U;
  StereoF32 beforePrevious_{};
  StereoF32 previous_{};
  StereoF32 current_{};
  bool hasBeforePrevious_ = false;
  bool hasPrevious_ = false;
  bool hasSegment_ = false;
  bool tailSegment_ = false;
  bool finished_ = false;
};

#endif
