/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Render/IUiPresenter.h"

#include <cstddef>
#include <cstdint>

namespace ui2 {

enum class UiRgb565ByteOrder : std::uint8_t {
  Native,
  MostSignificantByteFirst,
};

// Converts the indexed UI2 surface into small RGB565 DMA chunks. It deliberately
// owns no second framebuffer: the largest allocation is one 240 x 24 transfer
// block (11,520 bytes), independent of the number or size of dirty regions.
class UiRgb565Presenter final : public IUiPresenter {
public:
  static constexpr std::uint16_t kChunkRows = 24;
  static constexpr std::size_t kTransferPixels =
      static_cast<std::size_t>(kScreenWidth) * kChunkRows;

  using WriteChunkFunction = bool (*)(void *context, std::uint16_t x,
                                      std::uint16_t y, std::uint16_t width,
                                      std::uint16_t height,
                                      const std::uint16_t *pixels);

  UiRgb565Presenter(std::uint16_t *transferPixels,
                    std::size_t transferPixelCount,
                    WriteChunkFunction writeChunk, void *context,
                    UiRgb565ByteOrder byteOrder)
      : transfer_(transferPixels), transferPixelCount_(transferPixelCount),
        writeChunk_(writeChunk), context_(context), byteOrder_(byteOrder) {}

  PresentResult Present(const UiIndexedSurface &surface,
                        const UiPalette &palette,
                        std::span<const DirtyStrip> strips) override;

private:
  std::uint16_t *transfer_ = nullptr;
  std::size_t transferPixelCount_ = 0;
  WriteChunkFunction writeChunk_ = nullptr;
  void *context_ = nullptr;
  UiRgb565ByteOrder byteOrder_ = UiRgb565ByteOrder::Native;
};

static_assert(sizeof(UiRgb565Presenter) <= 64);

} // namespace ui2
