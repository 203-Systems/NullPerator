/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace ui2 {

// Shared value semantics for the chain transpose byte. Both the model port
// and the renderer use this contract; neither needs to reinterpret the other.
struct Ui2ChainTranspose {
  static constexpr int kMinimum = -99;
  static constexpr int kMaximum = 99;

  [[nodiscard]] static constexpr int Decode(std::uint8_t encoded) {
    return static_cast<std::int8_t>(encoded);
  }

  [[nodiscard]] static constexpr std::uint8_t Encode(int value) {
    return static_cast<std::uint8_t>(static_cast<std::int8_t>(
        std::clamp(value, kMinimum, kMaximum)));
  }

  [[nodiscard]] static constexpr std::uint8_t Adjust(std::uint8_t encoded,
                                                     int delta) {
    return Encode(Decode(encoded) + delta);
  }

  [[nodiscard]] static std::array<char, 4> Format(std::uint8_t encoded) {
    const int value = std::clamp(Decode(encoded), kMinimum, kMaximum);
    const int magnitude = value < 0 ? -value : value;
    return {value < 0 ? '-' : '+',
            static_cast<char>('0' + magnitude / 10),
            static_cast<char>('0' + magnitude % 10), 0};
  }
};

} // namespace ui2
