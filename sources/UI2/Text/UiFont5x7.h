/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <array>
#include <cstdint>

namespace ui2 {

class UiFont5x7 {
public:
  static constexpr std::int16_t kGlyphWidth = 5;
  static constexpr std::int16_t kGlyphHeight = 7;
  static constexpr std::int16_t kAdvance = 6;

  using Rows = std::array<std::uint8_t, kGlyphHeight>;

  [[nodiscard]] static Rows Glyph(char character);
  [[nodiscard]] static constexpr std::int16_t TextWidth(std::size_t length,
                                                        std::uint8_t scale = 1) {
    return length == 0
               ? 0
               : static_cast<std::int16_t>(length * kAdvance * scale - scale);
  }
};

} // namespace ui2
