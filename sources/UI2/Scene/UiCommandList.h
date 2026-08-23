/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"
#include "UI2/Theme/UiPalette.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ui2 {

enum class UiCommandKind : std::uint8_t {
  FillRect,
  FillRoundedRect,
  FillCoverageRoundedRect,
  Text,
};

struct UiCommand {
  RectI16 bounds;
  std::uint16_t payload = 0;
  UiCommandKind kind = UiCommandKind::FillRect;
  PaletteIndex color = 0;
  PaletteIndex auxiliaryColor = 0;
  std::uint8_t parameter = 0;
};

static_assert(sizeof(UiCommand) <= 14);

struct UiCommandStream {
  std::span<const UiCommand> commands;
  std::span<const char> text;
};

template <std::size_t Capacity, std::size_t TextCapacity = 0>
class UiCommandList {
public:
  [[nodiscard]] bool FillRect(RectI16 bounds, PaletteIndex color) {
    return Push({bounds, 0, UiCommandKind::FillRect, color, color, 0});
  }

  [[nodiscard]] bool FillRoundedRect(RectI16 bounds, PaletteIndex color,
                                     PaletteIndex corner,
                                     std::uint8_t radius = 1) {
    return Push({bounds, 0, UiCommandKind::FillRoundedRect, color, corner,
                 radius});
  }

  [[nodiscard]] bool FillSelection(RectI16 bounds, PaletteIndex color,
                                   UiCoverage coverage,
                                   std::uint8_t radius = 1) {
    return Push({bounds, 0, UiCommandKind::FillCoverageRoundedRect, color,
                 static_cast<PaletteIndex>(coverage), radius});
  }

  [[nodiscard]] bool Text(PointI16 origin, std::string_view text,
                          PaletteIndex color, std::uint8_t scale = 1) {
    if (text.size() > TextCapacity - textSize_ || text.size() > 255U) {
      overflowed_ = true;
      return false;
    }
    const std::uint16_t offset = static_cast<std::uint16_t>(textSize_);
    std::copy(text.begin(), text.end(), text_.begin() + textSize_);
    textSize_ += text.size();
    const RectI16 bounds{
        origin.x, origin.y,
        static_cast<std::int16_t>(text.empty()
                                      ? 0
                                      : text.size() * 6U * scale - scale),
        static_cast<std::int16_t>(7U * scale)};
    if (!Push({bounds, offset, UiCommandKind::Text, color,
               static_cast<PaletteIndex>(text.size()), scale})) {
      textSize_ = offset;
      return false;
    }
    return true;
  }

  void Clear() {
    size_ = 0;
    textSize_ = 0;
    overflowed_ = false;
  }

  [[nodiscard]] std::span<const UiCommand> Commands() const {
    return {commands_.data(), size_};
  }
  [[nodiscard]] UiCommandStream Stream() const {
    return {Commands(), {text_.data(), textSize_}};
  }
  [[nodiscard]] std::size_t Size() const { return size_; }
  [[nodiscard]] bool Overflowed() const { return overflowed_; }

private:
  [[nodiscard]] bool Push(UiCommand command) {
    if (size_ >= commands_.size()) {
      overflowed_ = true;
      return false;
    }
    commands_[size_++] = command;
    return true;
  }

  std::array<UiCommand, Capacity> commands_{};
  std::array<char, TextCapacity> text_{};
  std::size_t size_ = 0;
  std::size_t textSize_ = 0;
  bool overflowed_ = false;
};

using UiSceneBuffer = UiCommandList<256, 1024>;
static_assert(sizeof(UiSceneBuffer) < 4'700);

} // namespace ui2
