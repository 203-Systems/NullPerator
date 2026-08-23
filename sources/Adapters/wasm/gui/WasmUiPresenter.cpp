/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Adapters/wasm/gui/WasmUiPresenter.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Theme/UiPalette.h"

#include <algorithm>

ui2::PresentResult
WasmUiPresenter::Present(const ui2::UiIndexedSurface &surface,
                         const ui2::UiPalette &palette,
                         std::span<const ui2::DirtyStrip> strips) {
  if (rgbaFrame_ == nullptr || frameBytes_ < kRequiredBytes ||
      commit_ == nullptr) {
    return ui2::PresentResult::Failed;
  }
  for (const ui2::DirtyStrip strip : strips) {
    const std::uint16_t left =
        std::min<std::uint16_t>(strip.x, ui2::kScreenWidth);
    const std::uint16_t top =
        std::min<std::uint16_t>(strip.y, ui2::kScreenHeight);
    const std::uint16_t right = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(static_cast<std::uint32_t>(strip.x) +
                                    static_cast<std::uint32_t>(strip.width),
                                ui2::kScreenWidth));
    const std::uint16_t bottom = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(static_cast<std::uint32_t>(strip.y) +
                                    static_cast<std::uint32_t>(strip.height),
                                ui2::kScreenHeight));
    for (std::uint16_t y = top; y < bottom; ++y) {
      for (std::uint16_t x = left; x < right; ++x) {
        const std::size_t pixel =
            static_cast<std::size_t>(y) * ui2::kScreenWidth + x;
        const ui2::Rgb888 color = palette.Get(surface.Pixels()[pixel]);
        std::uint8_t *destination = rgbaFrame_ + pixel * 4U;
        destination[0] = color.red;
        destination[1] = color.green;
        destination[2] = color.blue;
        destination[3] = 0xFF;
      }
    }
  }
  return commit_(context_) ? ui2::PresentResult::Presented
                           : ui2::PresentResult::Deferred;
}
