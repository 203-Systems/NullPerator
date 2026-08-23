/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ui2 {

class UiPalette {
public:
  static constexpr std::size_t kColorCount = 256;

  UiPalette();

  void Set(PaletteIndex index, Rgb888 color);
  [[nodiscard]] Rgb888 Get(PaletteIndex index) const;
  [[nodiscard]] std::uint16_t Rgb565(PaletteIndex index) const;
  [[nodiscard]] std::uint32_t Rgba8888(PaletteIndex index) const;

  [[nodiscard]] static constexpr std::uint16_t PackRgb565(Rgb888 color) {
    return static_cast<std::uint16_t>(
        ((static_cast<std::uint16_t>(color.red) & 0xF8U) << 8U) |
        ((static_cast<std::uint16_t>(color.green) & 0xFCU) << 3U) |
        (static_cast<std::uint16_t>(color.blue) >> 3U));
  }

private:
  std::array<Rgb888, kColorCount> colors_{};
  std::array<std::uint16_t, kColorCount> rgb565_{};
};

} // namespace ui2
