/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Theme/UiPalette.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ui2 {

struct UiSongViewData {
  std::string_view name;
  std::string_view elapsed = "00:08";
  std::array<std::array<std::uint8_t, 8>, 16> rows{};
  std::array<std::string_view, 8> notes{};
  std::array<std::uint8_t, 8> playbackRows{};
  std::array<std::uint8_t, 2> vuLevelTop{14, 34};
  std::uint8_t editRow = 8;
  std::uint8_t editTrack = 0;
  bool playing = true;
};

class UiSongView {
public:
  [[nodiscard]] static UiBuildStatus Build(const UiSongViewData &data,
                                           UiPalette &palette,
                                           UiFrameScene &scene);
};

} // namespace ui2
