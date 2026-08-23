/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ui2 {

inline constexpr std::int16_t kScreenWidth = 240;
inline constexpr std::int16_t kScreenHeight = 240;
inline constexpr std::int16_t kTopBarHeight = 34;
inline constexpr std::int16_t kBottomBarTop = 208;
inline constexpr std::int16_t kBottomBarHeight = 32;

using PaletteIndex = std::uint8_t;

struct PointI16 {
  std::int16_t x = 0;
  std::int16_t y = 0;

  friend constexpr bool operator==(PointI16, PointI16) = default;
};

struct RectI16 {
  std::int16_t x = 0;
  std::int16_t y = 0;
  std::int16_t width = 0;
  std::int16_t height = 0;

  [[nodiscard]] constexpr bool Empty() const {
    return width <= 0 || height <= 0;
  }

  [[nodiscard]] constexpr std::int32_t Right() const {
    return static_cast<std::int32_t>(x) + width;
  }

  [[nodiscard]] constexpr std::int32_t Bottom() const {
    return static_cast<std::int32_t>(y) + height;
  }

  [[nodiscard]] static constexpr RectI16 Screen() {
    return {0, 0, kScreenWidth, kScreenHeight};
  }

  friend constexpr bool operator==(RectI16, RectI16) = default;
};

[[nodiscard]] constexpr RectI16 Intersect(RectI16 left, RectI16 right) {
  const std::int32_t x0 = std::max<std::int32_t>(left.x, right.x);
  const std::int32_t y0 = std::max<std::int32_t>(left.y, right.y);
  const std::int32_t x1 = std::min(left.Right(), right.Right());
  const std::int32_t y1 = std::min(left.Bottom(), right.Bottom());
  if (x1 <= x0 || y1 <= y0) return {};
  return {static_cast<std::int16_t>(x0), static_cast<std::int16_t>(y0),
          static_cast<std::int16_t>(x1 - x0),
          static_cast<std::int16_t>(y1 - y0)};
}

[[nodiscard]] constexpr RectI16 Union(RectI16 left, RectI16 right) {
  if (left.Empty()) return right;
  if (right.Empty()) return left;
  const std::int32_t x0 = std::min<std::int32_t>(left.x, right.x);
  const std::int32_t y0 = std::min<std::int32_t>(left.y, right.y);
  const std::int32_t x1 = std::max(left.Right(), right.Right());
  const std::int32_t y1 = std::max(left.Bottom(), right.Bottom());
  return {static_cast<std::int16_t>(x0), static_cast<std::int16_t>(y0),
          static_cast<std::int16_t>(x1 - x0),
          static_cast<std::int16_t>(y1 - y0)};
}

struct DirtyStrip {
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
};

struct Rgb888 {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;

  friend constexpr bool operator==(Rgb888, Rgb888) = default;
};

struct Rgba8888 {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  std::uint8_t alpha = 255;

  friend constexpr bool operator==(Rgba8888, Rgba8888) = default;
};

} // namespace ui2
