/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace ui2 {

// Instrument Import keeps ordinary directories visually bracketed, but the
// parent entry is the literal `..` navigation affordance approved for UI2.
// Keep this formatter allocation-free because snapshots are built every frame.
inline void FormatInstrumentImportBrowserItem(char *destination,
                                              std::size_t capacity,
                                              std::string_view name,
                                              bool directory) {
  if (destination == nullptr || capacity == 0U)
    return;
  destination[0] = '\0';
  if (capacity == 1U)
    return;

  const bool bracket = directory && name != ".." && capacity >= 3U;
  std::size_t offset = 0U;
  if (bracket)
    destination[offset++] = '[';

  const std::size_t suffix = bracket ? 1U : 0U;
  const std::size_t available = capacity - offset - suffix - 1U;
  const std::size_t count = std::min(name.size(), available);
  if (count != 0U)
    std::memcpy(destination + offset, name.data(), count);
  offset += count;

  if (bracket)
    destination[offset++] = ']';
  destination[offset] = '\0';
}

} // namespace ui2
