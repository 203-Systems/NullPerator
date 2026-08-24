/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

namespace ui2 {

struct UiVuElement {
  // The user edits only safe/warning/peak. These two low-level presentation
  // values are element data and generate private indexed-cache colors.
  static constexpr std::uint8_t kTrackAlpha = 8;
  static constexpr std::uint8_t kSafeLowAlpha = 197;
};

} // namespace ui2
