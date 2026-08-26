/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Render/UiRgb565Presenter.h"

#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Theme/UiPalette.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

template <std::size_t Size>
constexpr std::array<std::uint8_t, Size> MakeSt7789ContrastCurve() {
  std::array<std::uint8_t, Size> curve{};
  constexpr std::uint32_t maximum = Size - 1U;
  for (std::uint32_t value = 0; value <= maximum; ++value) {
    // The Node panel lifts dark and mid-tone RGB565 values compared with the
    // desktop preview. Blend linear light with a quadratic response so black
    // and white remain exact while intermediate colors become less washed
    // out. A lookup keeps the per-pixel ESP32 path multiplication-free.
    curve[value] = static_cast<std::uint8_t>(
        (value * maximum + value * value + maximum) / (2U * maximum));
  }
  return curve;
}

constexpr auto kSt7789Curve5 = MakeSt7789ContrastCurve<32>();
constexpr auto kSt7789Curve6 = MakeSt7789ContrastCurve<64>();

[[nodiscard]] std::uint16_t ApplySt7789Contrast(std::uint16_t color) {
  const std::uint16_t red = kSt7789Curve5[(color >> 11U) & 0x1FU];
  const std::uint16_t green = kSt7789Curve6[(color >> 5U) & 0x3FU];
  const std::uint16_t blue = kSt7789Curve5[color & 0x1FU];
  return static_cast<std::uint16_t>((red << 11U) | (green << 5U) | blue);
}

} // namespace

std::uint16_t
UiRgb565Presenter::TransportColor(std::uint16_t rgb565) const {
  if (toneCurve_ == UiRgb565ToneCurve::St7789Contrast)
    rgb565 = ApplySt7789Contrast(rgb565);
  if (byteOrder_ == UiRgb565ByteOrder::Native) return rgb565;
  return static_cast<std::uint16_t>((rgb565 >> 8U) | (rgb565 << 8U));
}

PresentResult
UiRgb565Presenter::Present(const UiIndexedSurface &surface,
                           const UiPalette &palette,
                           std::span<const DirtyStrip> strips) {
  if (transfer_ == nullptr || transferPixelCount_ < kTransferPixels ||
      writeChunk_ == nullptr || strips.empty()) {
    return PresentResult::Failed;
  }

  const auto pixels = surface.Pixels();
  for (const DirtyStrip strip : strips) {
    const std::uint16_t left =
        std::min<std::uint16_t>(strip.x, kScreenWidth);
    const std::uint16_t top =
        std::min<std::uint16_t>(strip.y, kScreenHeight);
    const std::uint16_t right = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(static_cast<std::uint32_t>(strip.x) +
                                    static_cast<std::uint32_t>(strip.width),
                                kScreenWidth));
    const std::uint16_t bottom = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(static_cast<std::uint32_t>(strip.y) +
                                    static_cast<std::uint32_t>(strip.height),
                                kScreenHeight));
    if (left >= right || top >= bottom) continue;

    const std::uint16_t width = right - left;
    for (std::uint16_t y = top; y < bottom;) {
      const std::uint16_t height = static_cast<std::uint16_t>(
          std::min<std::uint16_t>(kChunkRows, bottom - y));
      for (std::uint16_t row = 0; row < height; ++row) {
        const std::size_t source =
            static_cast<std::size_t>(y + row) * kScreenWidth + left;
        const std::size_t destination =
            static_cast<std::size_t>(row) * width;
        for (std::uint16_t x = 0; x < width; ++x) {
          transfer_[destination + x] =
              TransportColor(palette.Rgb565(pixels[source + x]));
        }
      }
      if (!writeChunk_(context_, left, y, width, height, transfer_)) {
        return PresentResult::Deferred;
      }
      y = static_cast<std::uint16_t>(y + height);
    }
  }
  return PresentResult::Presented;
}

} // namespace ui2
