/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Fixed.h"
#include <cstdint>

// Output calibration, not a compressor or per-preset loudness normalizer.
// A full-scale oscillator at VOL FF targets the same centered per-channel
// amplitude as a full-scale mono Sample: approximately 32767 / sqrt(2).
// Preserve each synth's existing PAN law and waveform/envelope dynamics.
namespace SynthLevel {
inline constexpr int CenteredPcmPeak = 23170;
inline constexpr std::int64_t CenteredFixedPeak =
    std::int64_t(CenteredPcmPeak) * FP_ONE;

// GB's integrated DAC is +/-15 in Q8, followed by the byte volume. Keep
// fractional PCM precision; the old factor 32 made GB about 28 dB too quiet.
inline constexpr int GbQ8Gain = CenteredFixedPeak / (15 * 256 * 255);
// The DC blocker can approach twice the nominal DAC amplitude. This still
// fits fixed; do not clip individual voices before the master gain is applied.
static_assert(std::int64_t(30 * 256) * 255 * GbQ8Gain < INT32_MAX);

// Coping's centered, envelope-scaled oscillator has nominal full-scale
// amplitude 2^27 in fixed units (4096 PCM), not full-scale 16-bit PCM.
inline fixed Coping(fixed sample) {
  return static_cast<fixed>((std::int64_t(sample) * CenteredPcmPeak) / 4096);
}

// Stack sums five signed-16-bit oscillators, multiplies by byte level and
// then by eight. Normalize their coherent peak, not their changing RMS.
inline constexpr std::int64_t StackNominalPeak =
    std::int64_t(32767) * 5 * 255 * 8;
inline constexpr int StackGainQ16 =
    (CenteredFixedPeak * 65536) / StackNominalPeak;
inline fixed Stack(fixed sample) {
  return static_cast<fixed>((std::int64_t(sample) * StackGainQ16) / 65536);
}
} // namespace SynthLevel
