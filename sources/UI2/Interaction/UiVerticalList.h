/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"

#include <algorithm>
#include <cstdint>

namespace ui2 {

// Shared fixed-bar list policy. Page commands stay in logical screen
// coordinates; only the retained content layer is translated. There is no
// scrollbar or heap-backed list state on the firmware path.
class UiVerticalList {
public:
  [[nodiscard]] static constexpr std::int16_t
  Clamp(std::int16_t offset, std::int16_t viewportBottom,
        std::int16_t contentBottom) {
    const std::int16_t maximum = static_cast<std::int16_t>(
        std::max<std::int32_t>(0, contentBottom - viewportBottom));
    return std::clamp(offset, static_cast<std::int16_t>(0), maximum);
  }

  [[nodiscard]] static constexpr std::int16_t
  Reveal(std::int16_t currentOffset, RectI16 item,
         std::int16_t viewportTop, std::int16_t viewportBottom,
         std::int16_t contentBottom) {
    const std::int16_t maximum = static_cast<std::int16_t>(
        std::max<std::int32_t>(0, contentBottom - viewportBottom));
    std::int16_t offset = Clamp(currentOffset, viewportBottom, contentBottom);
    if (item.y - offset < viewportTop) {
      offset = static_cast<std::int16_t>(item.y - viewportTop);
    } else if (item.Bottom() - offset > viewportBottom) {
      offset = static_cast<std::int16_t>(item.Bottom() - viewportBottom);
    }
    return std::clamp(offset, static_cast<std::int16_t>(0), maximum);
  }

  [[nodiscard]] static constexpr RectI16 VisualRect(RectI16 logical,
                                                     std::int16_t offset) {
    logical.y = static_cast<std::int16_t>(logical.y - offset);
    return logical;
  }
};

} // namespace ui2
