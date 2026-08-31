/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

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

} // namespace ui2
