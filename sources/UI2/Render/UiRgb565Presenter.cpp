/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Render/UiRgb565Presenter.h"

#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Theme/UiPalette.h"

#include <algorithm>

namespace ui2 {

PresentResult
UiRgb565Presenter::Present(const UiIndexedSurface &surface,
                           const UiPalette &palette,
                           std::span<const DirtyStrip> strips) {
  if (transfer_ == nullptr || transferPixelCount_ < kTransferPixels ||
      writeChunk_ == nullptr || strips.empty()) {
    return PresentResult::Failed;
  }

  const auto pixels = surface.Pixels();
  const auto &rgb565 = palette.Rgb565Colors();
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
        // Keep byte-order selection outside the pixel loop. The ESP32 -Os
        // build otherwise emits a non-inlined conversion call per pixel.
        if (byteOrder_ == UiRgb565ByteOrder::Native) {
          for (std::uint16_t x = 0; x < width; ++x) {
            transfer_[destination + x] = rgb565[pixels[source + x]];
          }
        } else {
          for (std::uint16_t x = 0; x < width; ++x) {
            const std::uint16_t color = rgb565[pixels[source + x]];
            transfer_[destination + x] = static_cast<std::uint16_t>(
                (color >> 8U) | (color << 8U));
          }
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
