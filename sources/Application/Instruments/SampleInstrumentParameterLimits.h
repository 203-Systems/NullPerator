/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Foundation/Types/Types.h"

// Model-owned persistence/edit limits for SampleInstrument. UI2 descriptors
// and project restore both consume these values so a visual control can never
// advertise a wider range than the audio engine can safely render.
namespace SampleInstrumentParameterLimits {
inline constexpr int VolumeMinimum = 0;
inline constexpr int VolumeMaximum = 0xFF;
inline constexpr int PanMinimum = 0;
inline constexpr int PanMaximum = 0xFE;
inline constexpr int RootNoteMinimum = 0;
inline constexpr int RootNoteMaximum = 0x7F;
inline constexpr int FineTuneMinimum = 0;
inline constexpr int FineTuneMaximum = 0xFF;
inline constexpr int DriveMinimum = 0;
inline constexpr int DriveMaximum = 0xFF;
inline constexpr int CrushMinimum = 1;
inline constexpr int CrushMaximum = 16;
inline constexpr int DownsampleMinimum = 0;
inline constexpr int DownsampleMaximum = 8;
inline constexpr int FilterMinimum = 0;
inline constexpr int FilterMaximum = 0xFF;
inline constexpr int PositionMinimum = 0;
inline constexpr int PositionPersistedMaximum = 0x0FFFFFFF;

inline bool TryGetPersistedIntegerRange(FourCC id, int &minimum,
                                        int &maximum) {
  if (id == FourCC::SampleInstrumentVolume) {
    minimum = VolumeMinimum;
    maximum = VolumeMaximum;
  } else if (id == FourCC::SampleInstrumentPan) {
    minimum = PanMinimum;
    maximum = PanMaximum;
  } else if (id == FourCC::SampleInstrumentRootNote) {
    minimum = RootNoteMinimum;
    maximum = RootNoteMaximum;
  } else if (id == FourCC::SampleInstrumentFineTune) {
    minimum = FineTuneMinimum;
    maximum = FineTuneMaximum;
  } else if (id == FourCC::SampleInstrumentCrushVolume) {
    minimum = DriveMinimum;
    maximum = DriveMaximum;
  } else if (id == FourCC::SampleInstrumentCrush) {
    minimum = CrushMinimum;
    maximum = CrushMaximum;
  } else if (id == FourCC::SampleInstrumentDownsample) {
    minimum = DownsampleMinimum;
    maximum = DownsampleMaximum;
  } else if (id == FourCC::SampleInstrumentFilterCutOff ||
             id == FourCC::SampleInstrumentFilterResonance ||
             id == FourCC::SampleInstrumentFilterType) {
    minimum = FilterMinimum;
    maximum = FilterMaximum;
  } else if (id == FourCC::SampleInstrumentStart ||
             id == FourCC::SampleInstrumentLoopStart ||
             id == FourCC::SampleInstrumentEnd) {
    minimum = PositionMinimum;
    maximum = PositionPersistedMaximum;
  } else {
    return false;
  }
  return true;
}
} // namespace SampleInstrumentParameterLimits
