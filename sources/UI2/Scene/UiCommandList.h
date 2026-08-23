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

enum class UiCommandKind : std::uint8_t {
  FillRect,
  FillRoundedRect,
};

struct UiCommand {
  RectI16 bounds;
  UiCommandKind kind = UiCommandKind::FillRect;
  PaletteIndex color = 0;
  PaletteIndex auxiliary_color = 0;
  std::uint8_t radius = 0;
};

static_assert(sizeof(UiCommand) <= 12);

template <std::size_t Capacity> class UiCommandList {
public:
  [[nodiscard]] bool FillRect(RectI16 bounds, PaletteIndex color) {
    return Push({bounds, UiCommandKind::FillRect, color, color, 0});
  }

  [[nodiscard]] bool FillRoundedRect(RectI16 bounds, PaletteIndex color,
                                     PaletteIndex corner,
                                     std::uint8_t radius = 1) {
    return Push(
        {bounds, UiCommandKind::FillRoundedRect, color, corner, radius});
  }

  void Clear() {
    size_ = 0;
    overflowed_ = false;
  }

  [[nodiscard]] std::span<const UiCommand> Commands() const {
    return {commands_.data(), size_};
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
  std::size_t size_ = 0;
  bool overflowed_ = false;
};

} // namespace ui2
