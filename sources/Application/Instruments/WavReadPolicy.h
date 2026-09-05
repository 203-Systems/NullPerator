/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "Application/Model/Config.h"
#include "Services/Audio/WavHeader.h"

inline constexpr unsigned TrackerMaximumResampleRatio = 6;

// Format parsing has no configuration dependency. Tracker import/preview
// owns its supported rate policy and supplies it explicitly to the parser.
inline auto ReadTrackerWavHeader(I_File *file) {
  const bool resampling = Config::GetInstance()->GetValue("IMPORTRESAMP") > 0;
  return WavHeaderWriter::ReadHeader(
      file, {44100 / TrackerMaximumResampleRatio,
             resampling ? 44100 * TrackerMaximumResampleRatio : 44100});
}
