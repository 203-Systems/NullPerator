/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2ThemeController.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ui2 {

struct Ui2ThemeColorEditResult final {
  std::uint32_t packedColor = 0;
  std::uint8_t color = 0;
  bool accepted = false;
  bool changed = false;
};

// Allocation-free policy shared by the application mutation boundary and the
// retained Theme state. Config owns persistence; this workflow only validates
// a typed controller command and produces one bounded RGB888 replacement.
class Ui2ThemeWorkflow final {
public:
  using Colors = std::array<std::uint32_t, Ui2ThemeController::ColorCount>;

  [[nodiscard]] static constexpr std::array<std::uint8_t, 3>
  Components(std::uint32_t packedColor) {
    return {static_cast<std::uint8_t>((packedColor >> 16U) & 0xFFU),
            static_cast<std::uint8_t>((packedColor >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(packedColor & 0xFFU)};
  }

  [[nodiscard]] static constexpr Ui2ThemeColorEditResult
  Execute(Ui2ThemeCommand command, const Colors &colors) {
    if (command.type != Ui2ThemeCommandType::AdjustColor || command.color < 0 ||
        static_cast<std::size_t>(command.color) >= colors.size() ||
        command.component >= 3U) {
      return {};
    }

    const std::uint8_t color = static_cast<std::uint8_t>(command.color);
    const std::uint8_t shift =
        static_cast<std::uint8_t>((2U - command.component) * 8U);
    const std::uint32_t current = colors[color] & 0x00FFFFFFU;
    const int previous = static_cast<int>((current >> shift) & 0xFFU);
    const std::uint32_t adjusted = static_cast<std::uint32_t>(
        std::clamp(previous + static_cast<int>(command.delta), 0, 255));
    const std::uint32_t packed =
        (current & ~(std::uint32_t{0xFFU} << shift)) | (adjusted << shift);
    return {.packedColor = packed,
            .color = color,
            .accepted = true,
            .changed = packed != current};
  }
};

static_assert(Ui2ThemeController::ColorCount == 19U);
static_assert(std::is_trivially_copyable_v<Ui2ThemeColorEditResult>);
static_assert(sizeof(Ui2ThemeColorEditResult) <= 8U);

} // namespace ui2
