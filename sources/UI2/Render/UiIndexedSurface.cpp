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
  FillRect(rect, color, RectI16::Screen());
}

void UiIndexedSurface::FillRect(RectI16 rect, PaletteIndex color,
                                RectI16 clip) {
  rect = Intersect(rect, Intersect(clip, RectI16::Screen()));
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
  FillRoundedRect(rect, fill, corner, radius, RectI16::Screen());
}

void UiIndexedSurface::FillRoundedRect(RectI16 rect, PaletteIndex fill,
                                       PaletteIndex corner,
                                       std::uint8_t radius, RectI16 clip) {
  const RectI16 visible = Intersect(rect, Intersect(clip, RectI16::Screen()));
  if (visible.Empty()) return;
  if (radius == 0 || rect.width < 3 || rect.height < 3) {
    FillRect(rect, fill, clip);
    return;
  }

  // The approved UI has fully crisp edges. Only the original four corner
  // pixels use a precomposited coverage color; clipping never invents corners.
  for (std::int16_t y = visible.y; y < visible.Bottom(); ++y) {
    for (std::int16_t x = visible.x; x < visible.Right(); ++x) {
      const bool edgeX = x == rect.x || x == rect.Right() - 1;
      const bool edgeY = y == rect.y || y == rect.Bottom() - 1;
      storage_.pixels[Offset(x, y)] = edgeX && edgeY ? corner : fill;
    }
  }
  storage_.dirty.Mark(visible);
}

void UiIndexedSurface::FillCoverageRoundedRect(
    RectI16 rect, PaletteIndex fill, const UiPalette &palette,
    UiCoverage coverage, std::uint8_t radius, RectI16 clip) {
  const RectI16 visible = Intersect(rect, Intersect(clip, RectI16::Screen()));
  if (visible.Empty()) return;
  if (radius == 0 || rect.width < 3 || rect.height < 3) {
    FillRect(rect, fill, clip);
    return;
  }
  for (std::int16_t y = visible.y; y < visible.Bottom(); ++y) {
    for (std::int16_t x = visible.x; x < visible.Right(); ++x) {
      const bool edgeX = x == rect.x || x == rect.Right() - 1;
      const bool edgeY = y == rect.y || y == rect.Bottom() - 1;
      const std::size_t offset = Offset(x, y);
      storage_.pixels[offset] =
          edgeX && edgeY
              ? palette.CoverageIndex(coverage, storage_.pixels[offset])
              : fill;
    }
  }
  storage_.dirty.Mark(visible);
}

void UiIndexedSurface::DrawGlyph5x7(
    PointI16 origin, const std::array<std::uint8_t, 7> &rows,
    PaletteIndex color, std::uint8_t scale, RectI16 clip) {
  if (scale == 0) return;
  const RectI16 bounds{origin.x, origin.y,
                       static_cast<std::int16_t>(5 * scale),
                       static_cast<std::int16_t>(7 * scale)};
  const RectI16 visible = Intersect(bounds, Intersect(clip, RectI16::Screen()));
  if (visible.Empty()) return;
  for (std::int16_t y = visible.y; y < visible.Bottom(); ++y) {
    const std::uint8_t glyphY = static_cast<std::uint8_t>(y - origin.y) / scale;
    for (std::int16_t x = visible.x; x < visible.Right(); ++x) {
      const std::uint8_t glyphX = static_cast<std::uint8_t>(x - origin.x) / scale;
      if ((rows[glyphY] & (1U << (4U - glyphX))) != 0U) {
        storage_.pixels[Offset(x, y)] = color;
      }
    }
  }
  storage_.dirty.Mark(visible);
}

void UiIndexedSurface::SetPixel(std::int16_t x, std::int16_t y,
                                PaletteIndex color) {
  if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) return;
  storage_.pixels[Offset(x, y)] = color;
  storage_.dirty.Mark({x, y, 1, 1});
}

void UiIndexedSurface::RemapRect(
    RectI16 rect,
    const std::array<PaletteIndex, UiPalette::kColorCount> &mapping) {
  rect = Intersect(rect, RectI16::Screen());
  if (rect.Empty()) return;
  bool changed = false;
  for (std::int16_t y = rect.y; y < rect.Bottom(); ++y) {
    for (std::int16_t x = rect.x; x < rect.Right(); ++x) {
      PaletteIndex &pixel = storage_.pixels[Offset(x, y)];
      const PaletteIndex replacement = mapping[pixel];
      changed = changed || replacement != pixel;
      pixel = replacement;
    }
  }
  if (changed) storage_.dirty.Mark(rect);
}

void UiIndexedSurface::ScrollRect(RectI16 rect, std::int16_t deltaX,
                                  std::int16_t deltaY, PaletteIndex fill) {
  rect = Intersect(rect, RectI16::Screen());
  if (rect.Empty() || (deltaX == 0 && deltaY == 0))
    return;
  const int yBegin = deltaY > 0 ? rect.Bottom() - 1 : rect.y;
  const int yEnd = deltaY > 0 ? rect.y - 1 : rect.Bottom();
  const int yStep = deltaY > 0 ? -1 : 1;
  const int xBegin = deltaX > 0 ? rect.Right() - 1 : rect.x;
  const int xEnd = deltaX > 0 ? rect.x - 1 : rect.Right();
  const int xStep = deltaX > 0 ? -1 : 1;
  for (int y = yBegin; y != yEnd; y += yStep) {
    for (int x = xBegin; x != xEnd; x += xStep) {
      const int sourceX = x - deltaX;
      const int sourceY = y - deltaY;
      storage_.pixels[Offset(static_cast<std::int16_t>(x),
                             static_cast<std::int16_t>(y))] =
          sourceX >= rect.x && sourceX < rect.Right() && sourceY >= rect.y &&
                  sourceY < rect.Bottom()
              ? storage_.pixels[Offset(static_cast<std::int16_t>(sourceX),
                                       static_cast<std::int16_t>(sourceY))]
              : fill;
    }
  }
  storage_.dirty.Mark(rect);
}

PaletteIndex UiIndexedSurface::Pixel(std::int16_t x, std::int16_t y) const {
  if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) return 0;
  return storage_.pixels[Offset(x, y)];
}

} // namespace ui2
