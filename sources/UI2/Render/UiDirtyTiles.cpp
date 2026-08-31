/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Render/UiDirtyTiles.h"

#include <algorithm>

namespace ui2 {

bool DirtyStripList::Push(DirtyStrip strip) {
  if (size_ >= strips_.size()) return false;
  strips_[size_++] = strip;
  return true;
}

void UiDirtyTiles::Clear() { words_.fill(0); }

void UiDirtyTiles::Set(std::uint16_t x, std::uint16_t y) {
  const std::size_t tile = static_cast<std::size_t>(y) * kColumns + x;
  words_[tile / 32U] |= 1U << (tile % 32U);
}

bool UiDirtyTiles::Test(std::uint16_t x, std::uint16_t y) const {
  const std::size_t tile = static_cast<std::size_t>(y) * kColumns + x;
  return (words_[tile / 32U] & (1U << (tile % 32U))) != 0;
}

void UiDirtyTiles::Mark(RectI16 rect) {
  rect = Intersect(rect, RectI16::Screen());
  if (rect.Empty()) return;
  const std::uint16_t firstX = static_cast<std::uint16_t>(rect.x) / kTileSize;
  const std::uint16_t firstY = static_cast<std::uint16_t>(rect.y) / kTileSize;
  const std::uint16_t lastX =
      static_cast<std::uint16_t>(rect.Right() - 1) / kTileSize;
  const std::uint16_t lastY =
      static_cast<std::uint16_t>(rect.Bottom() - 1) / kTileSize;
  for (std::uint16_t y = firstY; y <= lastY; ++y) {
    for (std::uint16_t x = firstX; x <= lastX; ++x) Set(x, y);
  }
}

void UiDirtyTiles::MarkAll() { Mark(RectI16::Screen()); }

bool UiDirtyTiles::Any() const {
  return std::any_of(words_.begin(), words_.end(),
                     [](std::uint32_t word) { return word != 0; });
}

bool UiDirtyTiles::Collect(DirtyStripList &output) const {
  struct Run {
    std::uint16_t x = 0;
    std::uint16_t width = 0;
    std::size_t strip = 0;
  };
  std::array<Run, kColumns> firstRow{};
  std::array<Run, kColumns> secondRow{};
  auto *previous = &firstRow;
  auto *current = &secondRow;
  std::size_t previousCount = 0;
  output.Clear();

  for (std::uint16_t tileY = 0; tileY < kRows; ++tileY) {
    std::size_t currentCount = 0;
    std::uint16_t tileX = 0;
    while (tileX < kColumns) {
      if (!Test(tileX, tileY)) {
        ++tileX;
        continue;
      }
      const std::uint16_t start = tileX;
      while (tileX < kColumns && Test(tileX, tileY)) ++tileX;
      const std::uint16_t runWidth = tileX - start;
      Run run{static_cast<std::uint16_t>(start * kTileSize),
              static_cast<std::uint16_t>(runWidth * kTileSize), 0};
      bool merged = false;
      for (std::size_t index = 0; index < previousCount; ++index) {
        if ((*previous)[index].x == run.x &&
            (*previous)[index].width == run.width) {
          run.strip = (*previous)[index].strip;
          output.At(run.strip).height += kTileSize;
          merged = true;
          break;
        }
      }
      if (!merged) {
        run.strip = output.Size();
        if (!output.Push({run.x, static_cast<std::uint16_t>(tileY * kTileSize),
                          run.width, kTileSize})) {
          return false;
        }
      }
      (*current)[currentCount++] = run;
    }
    std::swap(previous, current);
    previousCount = currentCount;
  }
  return true;
}

} // namespace ui2
