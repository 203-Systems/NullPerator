/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/TrackerInput.h"

#include <algorithm>
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

// Global transport always delegates the toggle to Player::OnStartButton. The
// false startFromPrevious flag is the important part of the contract: PM_SONG
// starts every track at the visible Song cursor instead of lastSongPos_.
template <typename Transport, typename PlayMode>
void Ui2ToggleSongTransportAtCursor(Transport &transport, PlayMode songMode,
                                    int cursorTrack,
                                    std::uint8_t channelCount) {
  if (channelCount == 0U)
    return;
  const auto track = static_cast<unsigned char>(
      std::clamp(cursorTrack, 0, static_cast<int>(channelCount) - 1));
  transport.OnStartButton(songMode, track, false, track);
}

} // namespace ui2
