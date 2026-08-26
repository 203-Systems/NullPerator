/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/TrackerInput.h"

#include <cstdint>

namespace ui2 {

// Device/Theme (and the split-out Font page) inherit the legacy global PLAY
// contract only for an unmodified tap. Browser and editor pages keep their own
// PLAY ownership, such as Sample Browser press/release preview.
[[nodiscard]] constexpr bool Ui2IsPlainPlay(TrackerAction action, bool pressed,
                                            std::uint16_t heldMask) {
  return pressed && action == TrackerAction::Play &&
         heldMask == TrackerActionBit(TrackerAction::Play);
}

} // namespace ui2
