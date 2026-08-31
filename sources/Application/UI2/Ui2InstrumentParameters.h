/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/SampleInstrumentParameterLimits.h"
#include "Application/UI2/Controllers/Ui2InstrumentController.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace ui2 {

// One fixed descriptor table is shared by native state capture and mutation.
// Keeping layout, formatting and edit bounds together prevents the renderer
// from showing one semantic value while input writes an unrelated generic
// 00..FF range (the original UI2 Instrument regression).
enum class Ui2InstrumentValueFormat : std::uint8_t {
  None,
  Hex,
  Decimal,
  DecimalOneBased,
  Note,
  Boolean,
  Choice,
  OffHex,
  Bitmask,
  UserText,
  SliceCount,
  SampleFilter,
  SidOscillator,
  SidFilter,
  SidWaveform,
  OpalAlgorithm,
  OpalWave,
  OpalKeyscale,
  SampleLoop,
};

struct Ui2InstrumentParameterDescriptor {
  const char *label = "";
  FourCC::enum_type primary = FourCC::Default;
  FourCC::enum_type secondary = FourCC::Default;
  std::int16_t minimum = 0;
  std::int32_t maximum = 0;
  std::uint16_t fineStep = 1;
  std::uint16_t coarseStep = 1;
  std::int16_t y = 0;
  std::uint8_t width = 0;
  Ui2InstrumentValueFormat format = Ui2InstrumentValueFormat::None;
  Ui2InstrumentSubfieldMode subfieldMode = Ui2InstrumentSubfieldMode::None;
  std::uint8_t subfieldTextOffset = 0;
  bool wrap = false;
  bool offValue = false;
  bool editable = true;
  bool userData = false;

  [[nodiscard]] constexpr bool Valid() const { return label[0] != '\0'; }
};

enum class Ui2InstrumentEditSideEffect : std::uint8_t {
  None,
  SendMidiProgramChange,
};

enum class Ui2InstrumentSampleOpenOutcome : std::uint8_t {
  Available,
  MissingSample,
  PlayingBlocked,
};

[[nodiscard]] constexpr Ui2InstrumentSampleOpenOutcome
Ui2InstrumentSampleOpenOutcomeFor(int sampleIndex, bool filenameAvailable,
                                  bool playerRunning) {
  if (sampleIndex < 0 || !filenameAvailable)
    return Ui2InstrumentSampleOpenOutcome::MissingSample;
  return playerRunning ? Ui2InstrumentSampleOpenOutcome::PlayingBlocked
                       : Ui2InstrumentSampleOpenOutcome::Available;
}

[[nodiscard]] constexpr const char *Ui2InstrumentSampleOpenFailureText(
    Ui2InstrumentSampleOpenOutcome outcome) {
  switch (outcome) {
  case Ui2InstrumentSampleOpenOutcome::MissingSample:
    return "NO SAMPLE LOADED";
  case Ui2InstrumentSampleOpenOutcome::PlayingBlocked:
    return "NOT WHILE PLAYING";
  case Ui2InstrumentSampleOpenOutcome::Available:
    return "";
  }
  return "SAMPLE OPEN FAILED";
}

namespace detail {

constexpr Ui2InstrumentParameterDescriptor
Parameter(const char *label, FourCC::enum_type primary, std::int16_t minimum,
          std::int32_t maximum, std::uint16_t fineStep,
          std::uint16_t coarseStep, std::int16_t y, std::uint8_t width,
          Ui2InstrumentValueFormat format, bool wrap = false,
          bool offValue = false, bool editable = true,
          FourCC::enum_type secondary = FourCC::Default,
          bool userData = false,
          Ui2InstrumentSubfieldMode subfieldMode =
              Ui2InstrumentSubfieldMode::None,
          std::uint8_t subfieldTextOffset = 0U) {
  return {.label = label,
          .primary = primary,
          .secondary = secondary,
          .minimum = minimum,
          .maximum = maximum,
          .fineStep = fineStep,
          .coarseStep = coarseStep,
          .y = y,
          .width = width,
          .format = format,
          .subfieldMode = subfieldMode,
          .subfieldTextOffset = subfieldTextOffset,
          .wrap = wrap,
          .offValue = offValue,
          .editable = editable,
          .userData = userData};
}

inline constexpr std::array<Ui2InstrumentParameterDescriptor, 17>
    kSampleParameters{
        Parameter("SAMPLE", FourCC::SampleInstrumentSample, 0, 0, 1, 1, 66,
                  0, Ui2InstrumentValueFormat::UserText, false, false, false,
                  FourCC::Default, true),
        Parameter("SLICES", FourCC::Default, 0, 0, 1, 1, 76, 0,
                  Ui2InstrumentValueFormat::SliceCount, false, false, false),
        Parameter("VOLUME", FourCC::SampleInstrumentVolume,
                  SampleInstrumentParameterLimits::VolumeMinimum,
                  SampleInstrumentParameterLimits::VolumeMaximum, 1, 10,
                  86, 2, Ui2InstrumentValueFormat::Hex),
        Parameter("PAN", FourCC::SampleInstrumentPan,
                  SampleInstrumentParameterLimits::PanMinimum,
                  SampleInstrumentParameterLimits::PanMaximum, 1, 0x10, 96, 2,
                  Ui2InstrumentValueFormat::Hex),
        Parameter("ROOT NOTE", FourCC::SampleInstrumentRootNote,
                  SampleInstrumentParameterLimits::RootNoteMinimum,
                  SampleInstrumentParameterLimits::RootNoteMaximum, 1, 0x0C,
                  106, 0, Ui2InstrumentValueFormat::Note),
        Parameter("DETUNE", FourCC::SampleInstrumentFineTune,
                  SampleInstrumentParameterLimits::FineTuneMinimum,
                  SampleInstrumentParameterLimits::FineTuneMaximum, 1, 0x10,
                  116, 2, Ui2InstrumentValueFormat::Hex),
        Parameter("DRIVE", FourCC::SampleInstrumentCrushVolume,
                  SampleInstrumentParameterLimits::DriveMinimum,
                  SampleInstrumentParameterLimits::DriveMaximum, 1, 0x10, 126,
                  2, Ui2InstrumentValueFormat::Hex),
        Parameter("CRUSH", FourCC::SampleInstrumentCrush,
                  SampleInstrumentParameterLimits::CrushMinimum,
                  SampleInstrumentParameterLimits::CrushMaximum, 1, 4, 136, 0,
                  Ui2InstrumentValueFormat::Decimal),
        Parameter("DOWNSAMPLE", FourCC::SampleInstrumentDownsample,
                  SampleInstrumentParameterLimits::DownsampleMinimum,
                  SampleInstrumentParameterLimits::DownsampleMaximum, 1, 4,
                  146, 0, Ui2InstrumentValueFormat::Decimal),
        Parameter("FILTER", FourCC::SampleInstrumentFilterCutOff,
                  SampleInstrumentParameterLimits::FilterMinimum,
                  SampleInstrumentParameterLimits::FilterMaximum, 1, 0x10,
                  156, 2, Ui2InstrumentValueFormat::SampleFilter, false, false,
                  true, FourCC::SampleInstrumentFilterResonance),
        Parameter("LOOP", FourCC::SampleInstrumentLoopMode, 0, 4, 1, 1, 166,
                  0, Ui2InstrumentValueFormat::SampleLoop),
        Parameter("INTERPOLATION", FourCC::SampleInstrumentInterpolation, 0,
                  1, 1, 1, 176, 0, Ui2InstrumentValueFormat::Choice),
        // Position maxima are resolved to sampleSize-1 immediately before
        // mutation. 0x0FFFFFFF is only the seven-digit format ceiling.
        Parameter("START", FourCC::SampleInstrumentStart,
                  SampleInstrumentParameterLimits::PositionMinimum,
                  SampleInstrumentParameterLimits::PositionPersistedMaximum, 1,
                  0x10, 186, 7, Ui2InstrumentValueFormat::Hex, false, false,
                  true, FourCC::Default, false,
                  Ui2InstrumentSubfieldMode::HexDigit),
        Parameter("LOOP START", FourCC::SampleInstrumentLoopStart,
                  SampleInstrumentParameterLimits::PositionMinimum,
                  SampleInstrumentParameterLimits::PositionPersistedMaximum, 1,
                  0x10, 196, 7,
                  Ui2InstrumentValueFormat::Hex, false, false, true,
                  FourCC::Default, false,
                  Ui2InstrumentSubfieldMode::HexDigit),
        Parameter("LOOP END", FourCC::SampleInstrumentEnd,
                  SampleInstrumentParameterLimits::PositionMinimum,
                  SampleInstrumentParameterLimits::PositionPersistedMaximum, 1,
                  0x10, 206, 7, Ui2InstrumentValueFormat::Hex, false, false,
                  true, FourCC::Default, false,
                  Ui2InstrumentSubfieldMode::HexDigit),
        Parameter("TABLE", FourCC::SampleInstrumentTable, 0, 0x1F, 1, 0x10,
                  216, 2, Ui2InstrumentValueFormat::OffHex, false, true),
        Parameter("AUTOMATION", FourCC::SampleInstrumentTableAutomation, 0,
                  1, 1, 1, 226, 0, Ui2InstrumentValueFormat::Boolean),
    };

inline constexpr std::array<Ui2InstrumentParameterDescriptor, 6>
    kMidiParameters{
        Parameter("CHANNEL", FourCC::MidiInstrumentChannel, 0, 0x0F, 1, 4,
                  66, 2, Ui2InstrumentValueFormat::DecimalOneBased),
        Parameter("VOLUME", FourCC::MidiInstrumentVolume, 0, 0xFF, 1, 0x10,
                  76, 2, Ui2InstrumentValueFormat::Hex),
        Parameter("LENGTH", FourCC::MidiInstrumentNoteLength, 0, 0xFF, 1,
                  0x10, 86, 2, Ui2InstrumentValueFormat::Hex),
        Parameter("PROGRAM", FourCC::MidiInstrumentProgram, 0, 0x7F, 1, 0x10,
                  96, 2, Ui2InstrumentValueFormat::OffHex, false, true),
        Parameter("AUTOMATION", FourCC::MidiInstrumentTableAutomation, 0, 1,
                  1, 1, 106, 0, Ui2InstrumentValueFormat::Boolean),
        Parameter("TABLE", FourCC::MidiInstrumentTable, 0, TABLE_COUNT - 1, 1,
                  0x10, 116, 2, Ui2InstrumentValueFormat::OffHex, false, true),
    };

inline constexpr std::array<Ui2InstrumentParameterDescriptor, 11>
    kSidParameters{
        Parameter("OSCILLATOR", FourCC::SIDInstrumentOSCNumber, 0, 2, 1, 1,
                  66, 1, Ui2InstrumentValueFormat::Hex),
        Parameter("PULSEWIDTH", FourCC::SIDInstrumentPulseWidth, 0, 0xFFF, 1,
                  0x10, 76, 3, Ui2InstrumentValueFormat::Hex),
        Parameter("WAVEFORM", FourCC::SIDInstrumentWaveform, 0, 8, 1, 1, 86,
                  0, Ui2InstrumentValueFormat::SidWaveform),
        Parameter("OSC SYNC", FourCC::SIDInstrumentVSync, 0, 1, 1, 1, 96, 0,
                  Ui2InstrumentValueFormat::Boolean),
        Parameter("RING MOD", FourCC::SIDInstrumentRingModulator, 0, 1, 1, 1,
                  106, 0, Ui2InstrumentValueFormat::Boolean),
        Parameter("ENV ADSR", FourCC::SIDInstrumentADSR, 0, 0xFFFF, 1, 0x10,
                  116, 4, Ui2InstrumentValueFormat::Hex, true, false, true,
                  FourCC::Default, false,
                  Ui2InstrumentSubfieldMode::HexDigit),
        Parameter("FILTER", FourCC::SIDInstrumentFilterOn, 0, 1, 1, 1, 126,
                  0, Ui2InstrumentValueFormat::Boolean),
        Parameter("CUTOFF", FourCC::SIDInstrument1FilterCut, 0, 0x7FF, 1,
                  0x10, 136, 3, Ui2InstrumentValueFormat::Hex),
        Parameter("RESONANCE", FourCC::SIDInstrument1FilterResonance, 0, 0xF,
                  1, 1, 146, 1, Ui2InstrumentValueFormat::Hex),
        Parameter("MODE", FourCC::SIDInstrument1FilterMode, 0, 3, 1, 1, 156,
                  0, Ui2InstrumentValueFormat::Choice),
        Parameter("VOLUME", FourCC::SIDInstrument1Volume, 0, 0xF, 1, 1, 166,
                  1, Ui2InstrumentValueFormat::Hex),
    };

inline constexpr std::array<Ui2InstrumentParameterDescriptor, 3>
    kOpalParameters{
        Parameter("ALGORITHM", FourCC::OPALInstrumentAlgorithm, 0, 1, 1, 1,
                  82, 0, Ui2InstrumentValueFormat::OpalAlgorithm),
        Parameter("DEEP TREM/VIB", FourCC::OPALInstrumentDeepTremeloVibrato,
                  0, 3, 1, 1, 93, 2, Ui2InstrumentValueFormat::Bitmask,
                  false, false, true, FourCC::Default, false,
                  Ui2InstrumentSubfieldMode::Bit),
        Parameter("FEEDBACK", FourCC::OPALInstrumentFeedback, 0, 7, 1, 1,
                  104, 1, Ui2InstrumentValueFormat::Hex),
    };

inline constexpr std::array<Ui2InstrumentParameterDescriptor, 6>
    kOpalOperator1{
        Parameter("LEVEL", FourCC::OPALInstrumentOp1Level, 0, 63, 1, 1, 144,
                  2, Ui2InstrumentValueFormat::Hex),
        Parameter("MULTIPLIER", FourCC::OPALInstrumentOp1Multiplier, 0, 15, 1,
                  1, 153, 1, Ui2InstrumentValueFormat::Hex),
        Parameter("A/D/S/R", FourCC::OPALInstrumentOp1ADSR, 0, 0xFFFF, 1,
                  0x10, 162, 4, Ui2InstrumentValueFormat::Hex, true, false,
                  true, FourCC::Default, false,
                  Ui2InstrumentSubfieldMode::HexDigit),
        Parameter("SHAPE", FourCC::OPALInstrumentOp1WaveShape, 0, 7, 1, 1,
                  171, 0, Ui2InstrumentValueFormat::OpalWave),
        Parameter("TR/VB/SU/KSR", FourCC::OPALInstrumentOp1TremVibSusKSR, 0,
                  15, 1, 1, 180, 4, Ui2InstrumentValueFormat::Bitmask, false,
                  false, true, FourCC::Default, false,
                  Ui2InstrumentSubfieldMode::Bit),
        Parameter("KEYSCALE", FourCC::OPALInstrumentOp1KeyScaleLevel, 0, 3, 1,
                  1, 189, 0, Ui2InstrumentValueFormat::OpalKeyscale),
    };

inline constexpr std::array<Ui2InstrumentParameterDescriptor, 6>
    kOpalOperator2{
        Parameter("LEVEL", FourCC::OPALInstrumentOp2Level, 0, 63, 1, 1, 144,
                  2, Ui2InstrumentValueFormat::Hex),
        Parameter("MULTIPLIER", FourCC::OPALInstrumentOp2Multiplier, 0, 15, 1,
                  1, 153, 1, Ui2InstrumentValueFormat::Hex),
        Parameter("A/D/S/R", FourCC::OPALInstrumentOp2ADSR, 0, 0xFFFF, 1,
                  0x10, 162, 4, Ui2InstrumentValueFormat::Hex, true, false,
                  true, FourCC::Default, false,
                  Ui2InstrumentSubfieldMode::HexDigit),
        Parameter("SHAPE", FourCC::OPALInstrumentOp2WaveShape, 0, 7, 1, 1,
                  171, 0, Ui2InstrumentValueFormat::OpalWave),
        Parameter("TR/VB/SU/KSR", FourCC::OPALInstrumentOp2TremVibSusKSR, 0,
                  15, 1, 1, 180, 4, Ui2InstrumentValueFormat::Bitmask, false,
                  false, true, FourCC::Default, false,
                  Ui2InstrumentSubfieldMode::Bit),
        Parameter("KEYSCALE", FourCC::OPALInstrumentOp2KeyScaleLevel, 0, 3, 1,
                  1, 189, 0, Ui2InstrumentValueFormat::OpalKeyscale),
    };

static_assert(kSampleParameters.size() <= kUiInstrumentMaximumFields);
static_assert(kMidiParameters.size() <= kUiInstrumentMaximumFields);
static_assert(kSidParameters.size() <= kUiInstrumentMaximumFields);
static_assert(kOpalParameters.size() <= kUiInstrumentMaximumFields);
static_assert(kOpalOperator1.size() <= kUiInstrumentMaximumOperatorRows);
static_assert(kOpalOperator2.size() <= kUiInstrumentMaximumOperatorRows);

inline void CopyText(char *destination, std::size_t capacity,
                     const char *source, bool uppercase = false) {
  if (destination == nullptr || capacity == 0U)
    return;
  destination[0] = '\0';
  if (source == nullptr)
    return;
  std::size_t index = 0U;
  while (index + 1U < capacity && source[index] != '\0') {
    const unsigned char character = static_cast<unsigned char>(source[index]);
    destination[index] = uppercase
                             ? static_cast<char>(std::toupper(character))
                             : static_cast<char>(character);
    ++index;
  }
  destination[index] = '\0';
}

} // namespace detail

[[nodiscard]] constexpr std::uint8_t
Ui2InstrumentFieldCount(InstrumentType type) {
  switch (type) {
  case IT_SAMPLE:
    return detail::kSampleParameters.size();
  case IT_MIDI:
    return detail::kMidiParameters.size();
  case IT_SID:
    return detail::kSidParameters.size();
  case IT_OPAL:
    return detail::kOpalParameters.size();
  case IT_NONE:
  case IT_LAST:
    return 0U;
  }
  return 0U;
}

[[nodiscard]] constexpr std::uint8_t
Ui2InstrumentOperatorCount(InstrumentType type) {
  return type == IT_OPAL ? detail::kOpalOperator1.size() : 0U;
}

[[nodiscard]] constexpr Ui2InstrumentParameterDescriptor
Ui2InstrumentFieldParameter(InstrumentType type, std::uint8_t index,
                            bool sidFirstChip = true) {
  Ui2InstrumentParameterDescriptor descriptor;
  switch (type) {
  case IT_SAMPLE:
    if (index < detail::kSampleParameters.size())
      descriptor = detail::kSampleParameters[index];
    break;
  case IT_MIDI:
    if (index < detail::kMidiParameters.size())
      descriptor = detail::kMidiParameters[index];
    break;
  case IT_SID:
    if (index < detail::kSidParameters.size())
      descriptor = detail::kSidParameters[index];
    if (!sidFirstChip && index == 7U)
      descriptor.primary = FourCC::SIDInstrument2FilterCut;
    else if (!sidFirstChip && index == 8U)
      descriptor.primary = FourCC::SIDInstrument2FilterResonance;
    else if (!sidFirstChip && index == 9U)
      descriptor.primary = FourCC::SIDInstrument2FilterMode;
    else if (!sidFirstChip && index == 10U)
      descriptor.primary = FourCC::SIDInstrument2Volume;
    break;
  case IT_OPAL:
    if (index < detail::kOpalParameters.size())
      descriptor = detail::kOpalParameters[index];
    break;
  case IT_NONE:
  case IT_LAST:
    break;
  }
  return descriptor;
}

[[nodiscard]] constexpr Ui2InstrumentParameterDescriptor
Ui2InstrumentOperatorParameter(std::uint8_t index, bool secondOperator) {
  if (index >= detail::kOpalOperator1.size())
    return {};
  return secondOperator ? detail::kOpalOperator2[index]
                        : detail::kOpalOperator1[index];
}

[[nodiscard]] constexpr Ui2InstrumentParameterDescriptor
Ui2InstrumentCursorParameter(InstrumentType type,
                             Ui2InstrumentCursorPosition cursor,
                             bool sidFirstChip = true) {
  switch (cursor.kind) {
  case Ui2InstrumentCursorKind::Field:
    return Ui2InstrumentFieldParameter(type, cursor.index, sidFirstChip);
  case Ui2InstrumentCursorKind::Operator1:
    return Ui2InstrumentOperatorParameter(cursor.index, false);
  case Ui2InstrumentCursorKind::Operator2:
    return Ui2InstrumentOperatorParameter(cursor.index, true);
  case Ui2InstrumentCursorKind::Name:
  case Ui2InstrumentCursorKind::Type:
    return {};
  }
  return {};
}

struct Ui2InstrumentSubfieldSpec {
  Ui2InstrumentSubfieldMode mode = Ui2InstrumentSubfieldMode::None;
  std::uint8_t count = 0;
  std::uint8_t textOffset = 0;
};

struct Ui2InstrumentAdjustmentSpec {
  bool visible = false;
  std::uint8_t fineStep = 1;
  std::uint8_t coarseStep = 10;
  bool note = false;
};

[[nodiscard]] constexpr Ui2InstrumentSubfieldSpec
Ui2InstrumentSubfields(
    const Ui2InstrumentParameterDescriptor &descriptor) {
  if (!descriptor.Valid() || !descriptor.editable || descriptor.width == 0U ||
      descriptor.subfieldMode == Ui2InstrumentSubfieldMode::None)
    return {};
  return {.mode = descriptor.subfieldMode,
          .count = descriptor.width,
          .textOffset = descriptor.subfieldTextOffset};
}

// Only plain numeric fields use the already-approved coarse/fine legend.
// Digit/bit fields communicate focus in their value bubble; selectors,
// combined values and action rows keep their existing bottom-bar contract.
[[nodiscard]] constexpr Ui2InstrumentAdjustmentSpec
Ui2InstrumentAdjustment(
    const Ui2InstrumentParameterDescriptor &descriptor) {
  if (!descriptor.Valid() || !descriptor.editable ||
      descriptor.subfieldMode != Ui2InstrumentSubfieldMode::None)
    return {};

  const bool note = descriptor.format == Ui2InstrumentValueFormat::Note;
  const bool numeric = descriptor.format == Ui2InstrumentValueFormat::Hex ||
                       descriptor.format == Ui2InstrumentValueFormat::Decimal ||
                       descriptor.format ==
                           Ui2InstrumentValueFormat::DecimalOneBased ||
                       descriptor.format == Ui2InstrumentValueFormat::OffHex;
  if (!note && !numeric)
    return {};

  return {
      .visible = true,
      .fineStep = static_cast<std::uint8_t>(
          std::min<std::uint16_t>(descriptor.fineStep, 0xFFU)),
      .coarseStep = static_cast<std::uint8_t>(
          std::min<std::uint16_t>(descriptor.coarseStep, 0xFFU)),
      .note = note,
  };
}

[[nodiscard]] constexpr bool Ui2IsSamplePositionParameter(
    const Ui2InstrumentParameterDescriptor &descriptor) {
  return descriptor.primary == FourCC::SampleInstrumentStart ||
         descriptor.primary == FourCC::SampleInstrumentLoopStart ||
         descriptor.primary == FourCC::SampleInstrumentEnd;
}

[[nodiscard]] constexpr Ui2InstrumentParameterDescriptor
Ui2ResolveSamplePositionMaximum(Ui2InstrumentParameterDescriptor descriptor,
                                std::int32_t sampleSize) {
  if (Ui2IsSamplePositionParameter(descriptor)) {
    // START and LOOP START address a frame, while END is the exclusive bound
    // consumed by SampleInstrument. Assignment and project restore both keep
    // a full-sample END at sampleSize.
    const bool exclusiveEnd =
        descriptor.primary == FourCC::SampleInstrumentEnd;
    const std::int32_t resolved =
        sampleSize > 0 ? sampleSize - (exclusiveEnd ? 0 : 1) : 0;
    descriptor.maximum = std::clamp<std::int32_t>(
        resolved, descriptor.minimum, descriptor.maximum);
  }
  return descriptor;
}

[[nodiscard]] constexpr int Ui2AdjustInstrumentParameter(
    const Ui2InstrumentParameterDescriptor &descriptor, int current,
    Ui2InstrumentValueDirection direction) {
  if (!descriptor.Valid() || !descriptor.editable ||
      direction == Ui2InstrumentValueDirection::None)
    return current;

  const bool positive = direction == Ui2InstrumentValueDirection::Right ||
                        direction == Ui2InstrumentValueDirection::Up;
  const bool coarse = direction == Ui2InstrumentValueDirection::Up ||
                      direction == Ui2InstrumentValueDirection::Down;
  const int step = coarse ? descriptor.coarseStep : descriptor.fineStep;

  // UIIntVarOffField has an asymmetric transition from OFF: RIGHT selects the
  // minimum while UP selects minimum+coarse. LEFT/DOWN leave OFF untouched.
  if (descriptor.offValue && current < descriptor.minimum) {
    if (!positive)
      return -1;
    return direction == Ui2InstrumentValueDirection::Up
               ? std::min<int>(descriptor.maximum,
                               descriptor.minimum + descriptor.coarseStep)
               : descriptor.minimum;
  }

  const std::int64_t adjusted =
      static_cast<std::int64_t>(current) + (positive ? step : -step);
  if (descriptor.offValue && adjusted < descriptor.minimum)
    return -1;
  const bool selectorWrap =
      descriptor.format == Ui2InstrumentValueFormat::Boolean ||
      descriptor.format == Ui2InstrumentValueFormat::Choice ||
      descriptor.format == Ui2InstrumentValueFormat::SampleLoop ||
      descriptor.format == Ui2InstrumentValueFormat::SidWaveform ||
      descriptor.format == Ui2InstrumentValueFormat::OpalAlgorithm ||
      descriptor.format == Ui2InstrumentValueFormat::OpalWave ||
      descriptor.format == Ui2InstrumentValueFormat::OpalKeyscale;
  if (descriptor.wrap || selectorWrap) {
    const std::int64_t count =
        descriptor.maximum - descriptor.minimum + std::int64_t{1};
    return count <= 0
               ? current
               : static_cast<int>(
                     ((adjusted - descriptor.minimum) % count + count) %
                         count +
                     descriptor.minimum);
  }
  return static_cast<int>(std::clamp<std::int64_t>(
      adjusted, descriptor.minimum, descriptor.maximum));
}

[[nodiscard]] constexpr int Ui2AdjustInstrumentSubfieldParameter(
    const Ui2InstrumentParameterDescriptor &descriptor, int current,
    Ui2InstrumentSubfieldMode mode, std::uint8_t leftToRightSubfield,
    Ui2InstrumentValueDirection direction) {
  const Ui2InstrumentSubfieldSpec spec = Ui2InstrumentSubfields(descriptor);
  if (mode == Ui2InstrumentSubfieldMode::None || mode != spec.mode ||
      leftToRightSubfield >= spec.count ||
      (direction != Ui2InstrumentValueDirection::Up &&
       direction != Ui2InstrumentValueDirection::Down))
    return current;

  const std::uint8_t rightToLeft =
      static_cast<std::uint8_t>(spec.count - leftToRightSubfield - 1U);
  if (mode == Ui2InstrumentSubfieldMode::Bit) {
    const std::uint32_t mask = std::uint32_t{1U} << rightToLeft;
    const auto sanitized = static_cast<std::uint32_t>(std::clamp(
        current, static_cast<int>(descriptor.minimum),
        static_cast<int>(descriptor.maximum)));
    return static_cast<int>(sanitized ^ mask);
  }

  std::int64_t step = 1;
  for (std::uint8_t digit = 0; digit < rightToLeft; ++digit)
    step *= 16;
  std::int64_t adjusted = static_cast<std::int64_t>(current) +
                          (direction == Ui2InstrumentValueDirection::Up
                               ? step
                               : -step);
  if (descriptor.wrap) {
    const std::int64_t count =
        descriptor.maximum - descriptor.minimum + std::int64_t{1};
    if (count > 0)
      adjusted = ((adjusted - descriptor.minimum) % count + count) % count +
                 descriptor.minimum;
  }
  return static_cast<int>(std::clamp<std::int64_t>(
      adjusted, descriptor.minimum, descriptor.maximum));
}

[[nodiscard]] constexpr Ui2InstrumentEditSideEffect
Ui2InstrumentSideEffectFor(
    const Ui2InstrumentParameterDescriptor &descriptor, bool playerRunning,
    bool valueChanged) {
  return playerRunning && valueChanged &&
                 descriptor.primary == FourCC::MidiInstrumentProgram
             ? Ui2InstrumentEditSideEffect::SendMidiProgramChange
             : Ui2InstrumentEditSideEffect::None;
}

template <typename MidiInstrumentLike>
bool Ui2ApplyInstrumentSideEffect(
    const Ui2InstrumentParameterDescriptor &descriptor, bool playerRunning,
    bool valueChanged, MidiInstrumentLike &instrument, int channel,
    int adjusted) {
  if (Ui2InstrumentSideEffectFor(descriptor, playerRunning, valueChanged) !=
      Ui2InstrumentEditSideEffect::SendMidiProgramChange)
    return false;
  instrument.SendProgramChange(channel, adjusted);
  return true;
}

inline void Ui2FormatInstrumentParameter(
    const Ui2InstrumentParameterDescriptor &descriptor, int current,
    int secondary, const char *text, char *destination,
    std::size_t capacity) {
  if (destination == nullptr || capacity == 0U)
    return;
  destination[0] = '\0';
  if (!descriptor.Valid()) {
    detail::CopyText(destination, capacity, "--");
    return;
  }

  switch (descriptor.format) {
  case Ui2InstrumentValueFormat::Hex:
    std::snprintf(destination, capacity, "%0*X", descriptor.width,
                  static_cast<unsigned int>(current));
    break;
  case Ui2InstrumentValueFormat::Decimal:
    std::snprintf(destination, capacity, "%d", current);
    break;
  case Ui2InstrumentValueFormat::DecimalOneBased:
    std::snprintf(destination, capacity, "%02d", current + 1);
    break;
  case Ui2InstrumentValueFormat::Note: {
    static constexpr std::array<const char *, 12> names{
        "C",  "C#", "D",  "D#", "E",  "F",
        "F#", "G",  "G#", "A",  "A#", "B"};
    if (current < 0)
      detail::CopyText(destination, capacity, "--");
    else
      std::snprintf(destination, capacity, "%s%d", names[current % 12],
                    current / 12 - 2);
    break;
  }
  case Ui2InstrumentValueFormat::Boolean:
    detail::CopyText(destination, capacity, current != 0 ? "YES" : "NO");
    break;
  case Ui2InstrumentValueFormat::Choice:
    detail::CopyText(destination, capacity,
                     text == nullptr || text[0] == '\0' ? "--" : text, true);
    break;
  case Ui2InstrumentValueFormat::OffHex:
    if (current < descriptor.minimum)
      detail::CopyText(destination, capacity, "--");
    else
      std::snprintf(destination, capacity, "%0*X", descriptor.width,
                    static_cast<unsigned int>(current));
    break;
  case Ui2InstrumentValueFormat::Bitmask: {
    const unsigned int digits =
        std::min<unsigned int>(descriptor.width, capacity - 1U);
    for (unsigned int index = 0U; index < digits; ++index) {
      const unsigned int bit = digits - index - 1U;
      destination[index] = (current & (1 << bit)) != 0 ? '1' : '0';
    }
    destination[digits] = '\0';
    break;
  }
  case Ui2InstrumentValueFormat::UserText:
    detail::CopyText(destination, capacity,
                     text == nullptr || text[0] == '\0' ? "--" : text);
    break;
  case Ui2InstrumentValueFormat::SliceCount:
    if (current <= 1)
      detail::CopyText(destination, capacity, "OFF / ADJUST");
    else
      std::snprintf(destination, capacity, "%d / ADJUST", current);
    break;
  case Ui2InstrumentValueFormat::SampleFilter:
    std::snprintf(destination, capacity, "LP / %02X %02X",
                  static_cast<unsigned int>(current & 0xFF),
                  static_cast<unsigned int>(secondary & 0xFF));
    break;
  case Ui2InstrumentValueFormat::SidOscillator:
    std::snprintf(destination, capacity, "PULSEWIDTH %03X",
                  static_cast<unsigned int>(current & 0xFFF));
    break;
  case Ui2InstrumentValueFormat::SidFilter:
    std::snprintf(destination, capacity, "CUTOFF %03X",
                  static_cast<unsigned int>(current & 0x7FF));
    break;
  case Ui2InstrumentValueFormat::SidWaveform: {
    static constexpr std::array<const char *, 9> names{
        "--", "A", "/", "A/", "PULSE", "A PULSE", "/ PULSE",
        "A/ PULSE", "NOISE"};
    detail::CopyText(destination, capacity,
                     current >= 0 && current < static_cast<int>(names.size())
                         ? names[current]
                         : "--");
    break;
  }
  case Ui2InstrumentValueFormat::OpalAlgorithm:
    detail::CopyText(destination, capacity,
                     current == 0 ? "1*2" : current == 1 ? "1+2" : "--");
    break;
  case Ui2InstrumentValueFormat::OpalWave: {
    static constexpr std::array<const char *, 8> names{
        "SINE", "HALF", "ABS", "PULS", "EVEN", "AB-E", "SQR", "DSQR"};
    detail::CopyText(destination, capacity,
                     current >= 0 && current < static_cast<int>(names.size())
                         ? names[current]
                         : "--");
    break;
  }
  case Ui2InstrumentValueFormat::OpalKeyscale: {
    static constexpr std::array<const char *, 4> names{"0", "1.5", "3", "6"};
    detail::CopyText(destination, capacity,
                     current >= 0 && current < static_cast<int>(names.size())
                         ? names[current]
                         : "--");
    break;
  }
  case Ui2InstrumentValueFormat::SampleLoop: {
    static constexpr std::array<const char *, 5> names{
        "ONE SHOT", "FORWARD", "PING PONG", "OSCILLATOR", "LOOP SYNC"};
    detail::CopyText(destination, capacity,
                     current >= 0 && current < static_cast<int>(names.size())
                         ? names[current]
                         : "--");
    break;
  }
  case Ui2InstrumentValueFormat::None:
    detail::CopyText(destination, capacity, "--");
    break;
  }
}

static_assert(std::is_trivially_copyable_v<Ui2InstrumentParameterDescriptor>);
static_assert(sizeof(Ui2InstrumentParameterDescriptor) <= 40U);

} // namespace ui2
