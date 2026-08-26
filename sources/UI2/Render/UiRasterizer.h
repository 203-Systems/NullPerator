/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiCommandList.h"

#include <array>
#include <span>

namespace ui2 {

// Optional indexed-color remap used by short-lived renderer effects.  It is
// deliberately a caller-owned fixed array: rasterization stays allocation
// free and ordinary static frames pay only one null check per command.
struct UiRasterColorMap {
  std::array<PaletteIndex, UiPalette::kColorCount> indices{};

  void Identity() {
    for (std::size_t index = 0; index < indices.size(); ++index)
      indices[index] = static_cast<PaletteIndex>(index);
  }

  [[nodiscard]] PaletteIndex Apply(PaletteIndex color) const {
    return indices[color];
  }
};

class UiRasterizer {
public:
  static void Render(UiCommandStream stream, UiIndexedSurface &surface,
                     const UiPalette *palette = nullptr,
                     PointI16 origin = {},
                     RectI16 clip = RectI16::Screen(),
                     UiTextCaseMode textCase = UiTextCaseMode::Upper,
                     const UiRasterColorMap *colorMap = nullptr);
};

} // namespace ui2
