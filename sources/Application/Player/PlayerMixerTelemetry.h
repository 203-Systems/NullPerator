/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 *
 * This file is part of the picoTracker firmware
 */

#pragma once

#include <cstdint>

struct PlayerMixerChannelTelemetry final {
  std::uint8_t note = 0xFFU;
  std::int8_t slice = -1;
  bool playing = false;
};

namespace PlayerMixerTelemetry {

inline constexpr std::uint32_t NoteMask = 0xFFU;
inline constexpr std::uint32_t SliceShift = 8U;
inline constexpr std::uint32_t SliceMask = 0xFFU << SliceShift;
inline constexpr std::uint32_t PlayingBit = 1U << 16U;
inline constexpr std::uint8_t NoSlice = 0xFFU;

[[nodiscard]] constexpr std::uint32_t Pack(std::uint8_t note,
                                           std::int8_t slice, bool playing) {
  const std::uint8_t encodedSlice =
      slice < 0 ? NoSlice : static_cast<std::uint8_t>(slice);
  return static_cast<std::uint32_t>(note) |
         (static_cast<std::uint32_t>(encodedSlice) << SliceShift) |
         (playing ? PlayingBit : 0U);
}

[[nodiscard]] constexpr PlayerMixerChannelTelemetry
Decode(std::uint32_t packed) {
  const std::uint8_t encodedSlice =
      static_cast<std::uint8_t>((packed & SliceMask) >> SliceShift);
  return {
      .note = static_cast<std::uint8_t>(packed & NoteMask),
      .slice = encodedSlice == NoSlice ? static_cast<std::int8_t>(-1)
                                       : static_cast<std::int8_t>(encodedSlice),
      .playing = (packed & PlayingBit) != 0U,
  };
}

} // namespace PlayerMixerTelemetry
