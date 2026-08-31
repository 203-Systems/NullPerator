/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Model/Song.h"

#include <cstdint>

namespace player_direct_note {

inline constexpr std::uint8_t MaximumMidiNote = 0x7FU;

[[nodiscard]] constexpr bool IsPlayableTarget(std::uint16_t instrument,
                                               std::uint8_t note) noexcept {
  return instrument < MAX_INSTRUMENT_COUNT && note <= MaximumMidiNote;
}

} // namespace player_direct_note
