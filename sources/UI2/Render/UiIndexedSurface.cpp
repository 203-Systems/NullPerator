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
  MarkDamage(rect);
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
    std::fill_n(storage_.pixels.begin() + Offset(visible.x, y), visible.width,
                fill);
  }
  const auto setCorner = [&](std::int16_t x, std::int16_t y) {
    if (x >= visible.x && y >= visible.y && x < visible.Right() &&
        y < visible.Bottom())
      storage_.pixels[Offset(x, y)] = corner;
  };
  setCorner(rect.x, rect.y);
  setCorner(static_cast<std::int16_t>(rect.Right() - 1), rect.y);
  setCorner(rect.x, static_cast<std::int16_t>(rect.Bottom() - 1));
  setCorner(static_cast<std::int16_t>(rect.Right() - 1),
            static_cast<std::int16_t>(rect.Bottom() - 1));
  MarkDamage(visible);
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
  std::array<PaletteIndex, 4> cornerColors{};
  std::uint8_t visibleCorners = 0U;
  const auto captureCorner = [&](std::uint8_t index, std::int16_t x,
                                 std::int16_t y) {
    if (x < visible.x || y < visible.y || x >= visible.Right() ||
        y >= visible.Bottom())
      return;
    const std::size_t offset = Offset(x, y);
    visibleCorners |= static_cast<std::uint8_t>(1U << index);
    cornerColors[index] =
        palette.CoverageIndex(coverage, storage_.pixels[offset]);
  };
  captureCorner(0U, rect.x, rect.y);
  captureCorner(1U, static_cast<std::int16_t>(rect.Right() - 1), rect.y);
  captureCorner(2U, rect.x,
                static_cast<std::int16_t>(rect.Bottom() - 1));
  captureCorner(3U, static_cast<std::int16_t>(rect.Right() - 1),
                static_cast<std::int16_t>(rect.Bottom() - 1));
  for (std::int16_t y = visible.y; y < visible.Bottom(); ++y) {
    std::fill_n(storage_.pixels.begin() + Offset(visible.x, y), visible.width,
                fill);
  }
  const auto restoreCorner = [&](std::uint8_t index, std::int16_t x,
                                 std::int16_t y) {
    if ((visibleCorners & static_cast<std::uint8_t>(1U << index)) != 0U)
      storage_.pixels[Offset(x, y)] = cornerColors[index];
  };
  restoreCorner(0U, rect.x, rect.y);
  restoreCorner(1U, static_cast<std::int16_t>(rect.Right() - 1), rect.y);
  restoreCorner(2U, rect.x,
                static_cast<std::int16_t>(rect.Bottom() - 1));
  restoreCorner(3U, static_cast<std::int16_t>(rect.Right() - 1),
                static_cast<std::int16_t>(rect.Bottom() - 1));
  MarkDamage(visible);
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
  MarkDamage(visible);
}

void UiIndexedSurface::SetPixel(std::int16_t x, std::int16_t y,
                                PaletteIndex color) {
  if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) return;
  storage_.pixels[Offset(x, y)] = color;
  if (damageBatchDepth_ == 0U) {
    storage_.dirty.MarkPixel(static_cast<std::uint16_t>(x),
                             static_cast<std::uint16_t>(y));
  }
}

PaletteIndex UiIndexedSurface::Pixel(std::int16_t x, std::int16_t y) const {
  if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) return 0;
  return storage_.pixels[Offset(x, y)];
}

} // namespace ui2
