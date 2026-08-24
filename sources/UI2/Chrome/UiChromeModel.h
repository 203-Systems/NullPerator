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
  UiNavTarget navTarget = UiNavTarget::Song;
  UiNavCursorModel navCursor{};
  bool metaSelected = false;
  RectI16 metaSelectionRect{};
  bool metaSelectionOverride = false;
  bool metaInkVisible = true;
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
};

struct UiAdjustmentLegendModel {
  std::uint8_t fineStep = 1;
  std::uint8_t coarseStep = 10;
  bool coarseOctave = false;
};

enum class UiBottomBarKind : std::uint8_t {
  Hidden,
  TrackNotes,
  Context,
  Actions,
  Selector,
  AdjustmentLegend,
};

struct UiBottomBarModel {
  UiBottomBarKind kind = UiBottomBarKind::Hidden;
  UiTrackNotesModel trackNotes{};
  UiContextBarModel context{};
  UiActionBarModel actions{};
  UiSelectorBarModel selector{};
  UiAdjustmentLegendModel adjustment{};
};

struct UiResolvedChrome {
  UiTopBarModel top;
  UiBottomBarModel bottom;
};

} // namespace ui2
