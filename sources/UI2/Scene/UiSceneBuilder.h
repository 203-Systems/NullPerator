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

  void Selection(RectI16 bounds, bool playback = false) {
    const UiColorToken fill = playback ? UiColorToken::PlaybackActive
                                       : UiColorToken::CursorPrimary;
    const UiCoverage coverage =
        playback ? UiCoverage::Playback : UiCoverage::Cursor;
    Accept(commands_.FillSelection(bounds, Index(fill), coverage));
  }

  void VerticalPaletteRamp(RectI16 bounds, PaletteIndex firstColor) {
    Accept(commands_.FillVerticalPaletteRamp(bounds, firstColor));
  }

  void Text(std::string_view text, std::int16_t x, std::int16_t y,
            UiColorToken color, std::uint8_t scale = 1) {
    Accept(commands_.Text({x, y}, text, Index(color), scale));
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

static_assert(sizeof(UiContentScene) < 4'700);
static_assert(sizeof(UiBarScene) < 1'200);

} // namespace ui2
