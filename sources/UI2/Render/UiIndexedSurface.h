/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"
#include "UI2/Render/UiDirtyTiles.h"
#include "UI2/Theme/UiPalette.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ui2 {

struct UiSurfaceStorage {
  alignas(4) std::array<PaletteIndex,
                        static_cast<std::size_t>(kScreenWidth) * kScreenHeight>
      pixels{};
  UiDirtyTiles dirty;
};

class UiIndexedSurface {
public:
  explicit UiIndexedSurface(UiSurfaceStorage &storage) : storage_(storage) {}

  void Clear(PaletteIndex color);
  void FillRect(RectI16 rect, PaletteIndex color);
  void FillRect(RectI16 rect, PaletteIndex color, RectI16 clip);
  void FillRoundedRect(RectI16 rect, PaletteIndex fill,
                       PaletteIndex corner, std::uint8_t radius = 1);
  void FillRoundedRect(RectI16 rect, PaletteIndex fill, PaletteIndex corner,
                       std::uint8_t radius, RectI16 clip);
  void FillCoverageRoundedRect(RectI16 rect, PaletteIndex fill,
                               const UiPalette &palette, UiCoverage coverage,
                               std::uint8_t radius, RectI16 clip);
  void DrawGlyph5x7(PointI16 origin, const std::array<std::uint8_t, 7> &rows,
                    PaletteIndex color, std::uint8_t scale, RectI16 clip);
  void SetPixel(std::int16_t x, std::int16_t y, PaletteIndex color);
  void MarkDirty(RectI16 rect) { storage_.dirty.Mark(rect); }

  [[nodiscard]] PaletteIndex Pixel(std::int16_t x, std::int16_t y) const;
  [[nodiscard]] std::span<const PaletteIndex> Pixels() const {
    return storage_.pixels;
  }
  [[nodiscard]] const UiDirtyTiles &DirtyTiles() const {
    return storage_.dirty;
  }
  void ClearDirty() { storage_.dirty.Clear(); }

private:
  [[nodiscard]] static std::size_t Offset(std::int16_t x, std::int16_t y) {
    return static_cast<std::size_t>(y) * kScreenWidth +
           static_cast<std::size_t>(x);
  }

  UiSurfaceStorage &storage_;
};

static_assert(sizeof(UiSurfaceStorage) < 58'000);

} // namespace ui2
