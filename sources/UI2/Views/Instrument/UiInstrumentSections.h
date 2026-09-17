/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace ui2 {

enum class UiInstrumentKind : std::uint8_t {
  None,
  Sample,
  Midi,
  Sid,
  Opal,
  Drum,
  Stack,
  Chiptune,
  GBWave,
  GBPulse,
  GBNoise
};

inline constexpr std::int16_t kUiInstrumentOperatorHeaderY = 120;
[[nodiscard]] constexpr std::int16_t
UiInstrumentOperatorRowY(std::uint8_t row) {
  return kUiInstrumentOperatorHeaderY + 12 + row * 9;
}

struct UiInstrumentSection {
  std::string_view title;
  std::uint8_t firstField;
  std::int16_t leadingSpace = 16;
};

namespace detail {
inline constexpr std::array<UiInstrumentSection, 6> kSampleSections{
    {{"SOURCE", 0},
     {"LEVEL & PITCH", 2},
     {"CHARACTER", 6},
     {"FILTER", 9},
     {"PLAYBACK", 12},
     {"MODULATION", 17}}};
inline constexpr std::array<UiInstrumentSection, 2> kMidiSections{
    {{"OUTPUT", 0}, {"MODULATION", 4}}};
inline constexpr std::array<UiInstrumentSection, 3> kSidSections{
    {// Match Drum's 16 px gap above the table and 19 px gap below its last row.
     {"OSCILLATOR", 0},
     {"ENVELOPE", 5, 18},
     {"FILTER & OUTPUT", 6, 21}}};
inline constexpr std::array<UiInstrumentSection, 2> kDrumSections{
    {{"VOICES", 0, 4}, {"KIT", 12}}};
inline constexpr std::array<UiInstrumentSection, 4> kStackSections{
    {{"OSCILLATOR", 0}, {"TONE", 4}, {"ENVELOPE", 7}, {"MODULATION", 11}}};
inline constexpr std::array<UiInstrumentSection, 5> kChiptuneSections{
    {{"OSCILLATOR", 0},
     {"ENVELOPE", 5},
     {"VIBRATO", 8},
     {"SWEEP", 10},
     {"MODULATION", 12}}};
inline constexpr std::array<UiInstrumentSection, 3> kGBPulseSections{
    {{"OSCILLATOR", 0}, {"ENVELOPE & SWEEP", 3}, {"MODULATION", 6}}};
inline constexpr std::array<UiInstrumentSection, 3> kGBNoiseSections{
    {{"NOISE", 0}, {"ENVELOPE", 2}, {"MODULATION", 4}}};
inline constexpr std::array<UiInstrumentSection, 3> kGBWaveSections{
    {{"OSCILLATOR", 0}, {"WAVE RAM (4 SAMPLES / ROW)", 4}, {"MODULATION", 12}}};
} // namespace detail

[[nodiscard]] constexpr std::span<const UiInstrumentSection>
UiInstrumentSections(UiInstrumentKind kind) {
  switch (kind) {
  case UiInstrumentKind::Sample:
    return detail::kSampleSections;
  case UiInstrumentKind::Midi:
    return detail::kMidiSections;
  case UiInstrumentKind::Sid:
    return detail::kSidSections;
  case UiInstrumentKind::Drum:
    return detail::kDrumSections;
  case UiInstrumentKind::Stack:
    return detail::kStackSections;
  case UiInstrumentKind::Chiptune:
    return detail::kChiptuneSections;
  case UiInstrumentKind::GBWave:
    return detail::kGBWaveSections;
  case UiInstrumentKind::GBPulse:
    return detail::kGBPulseSections;
  case UiInstrumentKind::GBNoise:
    return detail::kGBNoiseSections;
  // OPAL retains its general/operator layout; NONE has no parameters.
  case UiInstrumentKind::Opal:
  case UiInstrumentKind::None:
    return {};
  }
  return {};
}

// Parameter order and identity stay unchanged. This shared spacing is used by
// parameter capture, cursor placement, scrolling, and snapshot fixtures.
[[nodiscard]] constexpr std::int16_t
UiInstrumentSectionFieldY(UiInstrumentKind kind, std::uint8_t field,
                          std::int16_t ungroupedY) {
  for (const auto &section : UiInstrumentSections(kind)) {
    if (section.firstField <= field)
      ungroupedY += section.leadingSpace;
  }
  return ungroupedY;
}

} // namespace ui2
