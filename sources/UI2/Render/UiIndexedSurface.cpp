/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Render/UiIndexedSurface.h"

#include <algorithm>

namespace ui2 {

void UiIndexedSurface::Clear(PaletteIndex color) {
  storage_.pixels.fill(color);
  storage_.dirty.MarkAll();
}

void UiIndexedSurface::FillRect(RectI16 rect, PaletteIndex color) {
  rect = Intersect(rect, RectI16::Screen());
  if (rect.Empty()) return;
  for (std::int16_t y = rect.y; y < rect.Bottom(); ++y) {
    auto begin = storage_.pixels.begin() + Offset(rect.x, y);
    std::fill_n(begin, rect.width, color);
  }
  storage_.dirty.Mark(rect);
}

void UiIndexedSurface::FillRoundedRect(RectI16 rect, PaletteIndex fill,
                                       PaletteIndex corner,
                                       std::uint8_t radius) {
  rect = Intersect(rect, RectI16::Screen());
  if (rect.Empty()) return;
  if (radius == 0 || rect.width < 3 || rect.height < 3) {
    FillRect(rect, fill);
    return;
  }

  // The approved UI uses crisp straight edges and one coverage-colored pixel
  // at each corner. It is intentionally not a general blur/vector rasterizer.
  FillRect({rect.x, static_cast<std::int16_t>(rect.y + 1), rect.width,
            static_cast<std::int16_t>(rect.height - 2)},
           fill);
  FillRect({static_cast<std::int16_t>(rect.x + 1), rect.y,
            static_cast<std::int16_t>(rect.width - 2), rect.height},
           fill);
  SetPixel(rect.x, rect.y, corner);
  SetPixel(static_cast<std::int16_t>(rect.Right() - 1), rect.y, corner);
  SetPixel(rect.x, static_cast<std::int16_t>(rect.Bottom() - 1), corner);
  SetPixel(static_cast<std::int16_t>(rect.Right() - 1),
           static_cast<std::int16_t>(rect.Bottom() - 1), corner);
}

void UiIndexedSurface::SetPixel(std::int16_t x, std::int16_t y,
                                PaletteIndex color) {
  if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) return;
  storage_.pixels[Offset(x, y)] = color;
  storage_.dirty.Mark({x, y, 1, 1});
}

PaletteIndex UiIndexedSurface::Pixel(std::int16_t x, std::int16_t y) const {
  if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) return 0;
  return storage_.pixels[Offset(x, y)];
}

} // namespace ui2
