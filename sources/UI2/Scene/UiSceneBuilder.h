/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Scene/UiCommandList.h"
#include "UI2/Text/UiFont5x7.h"

#include <cstdint>
#include <string_view>

namespace ui2 {

enum class UiSelectionStyle : std::uint8_t {
  Cursor,
  Playback,
  MutedPlayback,
};

template <std::size_t CommandCapacity, std::size_t TextCapacity>
class UiSceneBuilder {
public:
  explicit UiSceneBuilder(
      UiCommandList<CommandCapacity, TextCapacity> &commands)
      : commands_(commands) {}

  void Fill(RectI16 bounds, UiColorToken color) {
    Accept(commands_.FillRect(bounds, Index(color)));
  }

  void RoundedFill(RectI16 bounds, UiColorToken color,
                   UiColorToken corner) {
    Accept(commands_.FillRoundedRect(bounds, Index(color), Index(corner)));
  }

  void Selection(RectI16 bounds,
                 UiSelectionStyle style = UiSelectionStyle::Cursor) {
    UiColorToken fill = UiColorToken::CursorPrimary;
    UiCoverage coverage = UiCoverage::Cursor;
    if (style == UiSelectionStyle::Playback) {
      fill = UiColorToken::PlaybackActive;
      coverage = UiCoverage::Playback;
    } else if (style == UiSelectionStyle::MutedPlayback) {
      fill = UiColorToken::DerivedPlaybackMuted;
      coverage = UiCoverage::Playback;
    }
    Accept(commands_.FillSelection(bounds, Index(fill), coverage));
  }

  void RowHighlight(RectI16 bounds) {
    RoundedFill(bounds, UiColorToken::CursorRow,
                UiColorToken::DerivedCursorRowCorner);
  }

  void SelectionHighlight(RectI16 bounds) {
    RoundedFill(bounds, UiColorToken::SelectionActive,
                UiColorToken::DerivedSelectionCorner);
  }

  void VerticalPaletteRamp(RectI16 bounds, PaletteIndex firstColor) {
    Accept(commands_.FillVerticalPaletteRamp(bounds, firstColor));
  }

  void SparseCoverageMask(RectI16 bounds,
                          std::span<const std::uint8_t> encoded,
                          UiCoverage coverage, UiColorToken background) {
    Accept(commands_.SparseCoverageMask(bounds, encoded, Index(background),
                                        coverage));
  }

  void Text(std::string_view text, std::int16_t x, std::int16_t y,
            UiColorToken color, std::uint8_t scale = 1) {
    Accept(commands_.Text({x, y}, text, Index(color), scale));
  }

  // User-authored names, paths and filenames must never inherit the global
  // UI label casing preference.
  void UserText(std::string_view text, std::int16_t x, std::int16_t y,
                UiColorToken color, std::uint8_t scale = 1) {
    Accept(commands_.Text({x, y}, text, Index(color), scale, true));
  }

  // Some UI values use letter case as data (for example the three Font
  // choices "Case", "CASE", and "case"). Render those examples literally so
  // the preference being edited cannot transform all choices into one label.
  void LiteralText(std::string_view text, std::int16_t x, std::int16_t y,
                   UiColorToken color, std::uint8_t scale = 1) {
    Accept(commands_.Text({x, y}, text, Index(color), scale, true));
  }

  void CenteredText(std::string_view text, std::int16_t center,
                    std::int16_t y, UiColorToken color,
                    std::uint8_t scale = 1) {
    const std::int16_t width = UiFont5x7::TextWidth(text.size(), scale);
    // This is Math.round(center - width / 2) for integral center values.
    const std::int16_t x =
        static_cast<std::int16_t>(center - width / 2 - (width % 2 < 0));
    Text(text, x, y, color, scale);
  }

  void CenteredUserText(std::string_view text, std::int16_t center,
                        std::int16_t y, UiColorToken color,
                        std::uint8_t scale = 1) {
    const std::int16_t width = UiFont5x7::TextWidth(text.size(), scale);
    const std::int16_t x =
        static_cast<std::int16_t>(center - width / 2 - (width % 2 < 0));
    UserText(text, x, y, color, scale);
  }

  void CenteredLiteralText(std::string_view text, std::int16_t center,
                           std::int16_t y, UiColorToken color,
                           std::uint8_t scale = 1) {
    const std::int16_t width = UiFont5x7::TextWidth(text.size(), scale);
    const std::int16_t x =
        static_cast<std::int16_t>(center - width / 2 - (width % 2 < 0));
    LiteralText(text, x, y, color, scale);
  }

  [[nodiscard]] bool Ok() const { return ok_ && !commands_.Overflowed(); }

  [[nodiscard]] static constexpr PaletteIndex Index(UiColorToken color) {
    return static_cast<PaletteIndex>(color);
  }

private:
  void Accept(bool accepted) { ok_ = ok_ && accepted; }

  UiCommandList<CommandCapacity, TextCapacity> &commands_;
  bool ok_ = true;
};

using UiContentScene = UiCommandList<256, 1024>;
using UiBarScene = UiCommandList<64, 256>;
// Dialogs render after the page, bars and content. Keeping their short text
// payload separate prevents a waveform command from consuming the bytes a
// confirmation or editor needs, without giving every page a larger buffer.
using UiOverlayScene = UiCommandList<80, 256>;

static_assert(sizeof(UiContentScene) < 4'700);
static_assert(sizeof(UiBarScene) < 1'200);
static_assert(sizeof(UiOverlayScene) < 1'500);

} // namespace ui2
