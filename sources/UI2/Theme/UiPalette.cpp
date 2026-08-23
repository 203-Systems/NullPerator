/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Theme/UiPalette.h"

namespace ui2 {

UiPalette::UiPalette() {
  for (std::size_t index = 0; index < kColorCount; ++index) {
    Set(static_cast<PaletteIndex>(index), {});
  }
}

void UiPalette::Set(PaletteIndex index, Rgb888 color) {
  colors_[index] = color;
  rgb565_[index] = PackRgb565(color);
}

Rgb888 UiPalette::Get(PaletteIndex index) const { return colors_[index]; }

std::uint16_t UiPalette::Rgb565(PaletteIndex index) const {
  return rgb565_[index];
}

std::uint32_t UiPalette::Rgba8888(PaletteIndex index) const {
  const Rgb888 color = colors_[index];
  return (static_cast<std::uint32_t>(color.red) << 24U) |
         (static_cast<std::uint32_t>(color.green) << 16U) |
         (static_cast<std::uint32_t>(color.blue) << 8U) | 0xFFU;
}

} // namespace ui2
