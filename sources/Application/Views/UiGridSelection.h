/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

// Compact, read-only selection geometry shared with UI2. Clipboard payloads
// remain private to their owning editor; the renderer only needs normalized
// bounds plus the anchor and active endpoint for visual treatment.
struct UiGridSelection {
  bool active = false;
  std::int16_t left = 0;
  std::int16_t top = 0;
  std::int16_t right = 0;
  std::int16_t bottom = 0;
  std::int16_t anchorColumn = 0;
  std::int16_t anchorRow = 0;
  std::int16_t activeColumn = 0;
  std::int16_t activeRow = 0;

  bool operator==(const UiGridSelection &) const = default;
};

static_assert(sizeof(UiGridSelection) <= 20);
