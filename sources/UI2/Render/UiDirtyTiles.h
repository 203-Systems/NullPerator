/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ui2 {

class DirtyStripList {
public:
  static constexpr std::size_t kMaxStrips = 450;

  void Clear() { size_ = 0; }
  bool Push(DirtyStrip strip);
  [[nodiscard]] std::span<const DirtyStrip> Strips() const {
    return {strips_.data(), size_};
  }
  [[nodiscard]] std::size_t Size() const { return size_; }

  DirtyStrip &At(std::size_t index) { return strips_[index]; }

private:
  std::array<DirtyStrip, kMaxStrips> strips_{};
  std::size_t size_ = 0;
};

class UiDirtyTiles {
public:
  static constexpr std::uint16_t kTileSize = 8;
  static constexpr std::uint16_t kColumns = kScreenWidth / kTileSize;
  static constexpr std::uint16_t kRows = kScreenHeight / kTileSize;
  static constexpr std::size_t kTileCount = kColumns * kRows;
  static constexpr std::size_t kWordCount = (kTileCount + 31U) / 32U;

  void Clear();
  void Mark(RectI16 rect);
  void MarkAll();
  [[nodiscard]] bool Any() const;
  bool Collect(DirtyStripList &output) const;

private:
  [[nodiscard]] bool Test(std::uint16_t x, std::uint16_t y) const;
  void Set(std::uint16_t x, std::uint16_t y);

  std::array<std::uint32_t, kWordCount> words_{};
};

static_assert(UiDirtyTiles::kColumns == 30);
static_assert(UiDirtyTiles::kRows == 30);
static_assert(sizeof(UiDirtyTiles) <= 128);

} // namespace ui2
