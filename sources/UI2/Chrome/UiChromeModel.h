/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Theme/UiPalette.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace ui2 {

enum class UiPowerState : std::uint8_t {
  Playing,
  BatteryNormal,
  BatteryHigh,
  BatteryLow,
  Charging,
  Navigation,
  Saving,
};

enum class UiNavTarget : std::uint8_t {
  Project,
  Song,
  Chain,
  Phrase,
  Instrument,
  Mixer,
  Groove,
  PhraseTable,
  InstrumentTable,
  // Pages without an approved tracker-map route keep NAV intentionally blank.
  // This prevents a default Song map from leaking into Device/Theme/Browser.
  None,
};

using UiNavTargetMask = std::uint16_t;

[[nodiscard]] constexpr UiNavTargetMask UiNavTargetBit(UiNavTarget target) {
  return static_cast<UiNavTargetMask>(
      UiNavTargetMask{1U} << static_cast<std::uint8_t>(target));
}

// A NAV map is an explicit visibility set. The renderer keeps the horizontal
// S/C/P/I spine visible and adds the vertical branch containing the current
// cursor. A compact mask keeps that topology independent from highlight state.
struct UiNavMapModel {
  UiNavTargetMask visible = 0U;

  [[nodiscard]] constexpr bool Contains(UiNavTarget target) const {
    return (visible & UiNavTargetBit(target)) != 0U;
  }

  bool operator==(const UiNavMapModel &) const = default;
};

struct UiNavCursorModel {
  RectI16 selectionRect{};
  bool selectionOverride = false;
  bool inkVisible = true;

  bool operator==(const UiNavCursorModel &) const = default;
};

struct UiTopBarModel {
  std::string_view title;
  std::string_view meta;
  std::string_view elapsed = "00:08";
  std::int16_t metaX = -1;
  UiPowerState power = UiPowerState::BatteryNormal;
  UiNavTarget navTarget = UiNavTarget::None;
  UiNavMapModel navMap{};
  bool navMapOverride = false;
  UiNavCursorModel navCursor{};
  bool metaSelected = false;
  RectI16 metaSelectionRect{};
  bool metaSelectionOverride = false;
  bool metaInkVisible = true;
  bool metaUserData = false;
  bool showBatteryPercent = false;
  std::uint8_t batteryPercent = 60;
};

struct UiTrackNotesModel {
  std::array<std::string_view, 8> notes{};
  std::int8_t selectedTrack = -1;
  std::int8_t selectedNote = -1;
  RectI16 trackSelectionRect{};
  bool trackSelectionOverride = false;
  bool trackInkVisible = true;
};

struct UiColoredText {
  std::string_view text;
  UiColorToken color = UiColorToken::TextNormal;
  std::int16_t x = -1;
  // User-authored names and filenames bypass the global UI label casing
  // transform. This is a rendering semantic, not a separate palette slot.
  bool userData = false;
};

struct UiContextBarModel {
  std::array<UiColoredText, 3> firstLine{};
  std::array<UiColoredText, 3> secondLine{};
  std::uint8_t firstLineCount = 0;
  std::uint8_t secondLineCount = 0;
};

struct UiActionBarModel {
  std::array<std::string_view, 4> actions{};
  std::uint8_t count = 0;
  std::uint8_t active = 0;
};

struct UiSelectorBarModel {
  std::span<const std::string_view> options;
  std::uint8_t current = 0;
  bool wrap = false;
  // Keep an inactive selector visible without implying that its current
  // option can be activated from the page's focused row.
  bool highlightCurrent = true;
  // Preserve option spelling when letter case is the value being selected.
  bool preserveCase = false;
};

struct UiAdjustmentLegendModel {
  std::uint8_t fineStep = 1;
  std::uint8_t coarseStep = 10;
  bool coarseOctave = false;
  // Optional semantic labels for non-numeric domains such as notes. Empty
  // labels retain the compact numeric +/- presentation.
  std::string_view fineLabel{};
  std::string_view coarseLabel{};
};

struct UiRgbBarModel {
  std::array<std::uint8_t, 3> values{};
  std::uint8_t active = 0;
};

enum class UiBottomBarKind : std::uint8_t {
  Hidden,
  TrackNotes,
  Context,
  Actions,
  Selector,
  AdjustmentLegend,
  Rgb,
};

struct UiBottomBarModel {
  UiBottomBarKind kind = UiBottomBarKind::Hidden;
  UiTrackNotesModel trackNotes{};
  UiContextBarModel context{};
  UiActionBarModel actions{};
  UiSelectorBarModel selector{};
  UiAdjustmentLegendModel adjustment{};
  UiRgbBarModel rgb{};
};

struct UiResolvedChrome {
  UiTopBarModel top;
  UiBottomBarModel bottom;
};

} // namespace ui2
