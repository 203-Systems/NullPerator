/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ui2 {

// Assign a C string to the fixed, zero-padded buffers used by UI frame state.
// The explicit bounded loop avoids pulling printf formatting into the 30 Hz
// capture path and always leaves a terminator when the destination is nonempty.
template <std::size_t Size>
void CopyUiText(std::array<char, Size> &destination, const char *source) {
  destination.fill('\0');
  if constexpr (Size > 0U) {
    if (source == nullptr) return;
    std::size_t length = 0U;
    while (length + 1U < Size && source[length] != '\0') ++length;
    std::copy_n(source, length, destination.begin());
  }
}

// Format the fixed MM:SS transport clock without pulling printf formatting
// through every UI frame. The display intentionally wraps minutes at 100,
// matching the previous "%02d:%02d" formatting contract.
inline void FormatUiElapsed(int seconds, std::array<char, 6> &destination) {
  const std::uint32_t elapsed =
      seconds < 0 ? 0U : static_cast<std::uint32_t>(seconds);
  const std::uint32_t minutes = (elapsed / 60U) % 100U;
  const std::uint32_t remainder = elapsed % 60U;
  destination = {
      static_cast<char>('0' + minutes / 10U),
      static_cast<char>('0' + minutes % 10U),
      ':',
      static_cast<char>('0' + remainder / 10U),
      static_cast<char>('0' + remainder % 10U),
      '\0',
  };
}

// Mixer capture formats all eight channel volumes plus the master volume on
// every 30 Hz frame. Keep that bounded decimal conversion independent of the
// general printf machinery while preserving the existing 0..999 clamp.
inline void FormatUiVolume(int value, std::array<char, 4> &destination) {
  const std::uint32_t volume =
      static_cast<std::uint32_t>(std::clamp(value, 0, 999));
  if (volume >= 100U) {
    destination = {
        static_cast<char>('0' + volume / 100U),
        static_cast<char>('0' + (volume / 10U) % 10U),
        static_cast<char>('0' + volume % 10U),
        '\0',
    };
  } else if (volume >= 10U) {
    destination = {
        static_cast<char>('0' + volume / 10U),
        static_cast<char>('0' + volume % 10U),
        '\0',
        '\0',
    };
  } else {
    destination = {
        static_cast<char>('0' + volume),
        '\0',
        '\0',
        '\0',
    };
  }
}

} // namespace ui2
