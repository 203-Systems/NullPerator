/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Scene/UiSceneBuilder.h"

#include <cstdint>

namespace ui2 {

struct UiFrameScene {
  UiBarScene top;
  UiContentScene content;
  UiBarScene bottom;
  UiColorToken topBackground = UiColorToken::SurfaceTopBar;
  UiColorToken bottomBackground = UiColorToken::SurfaceBottomBar;
  std::int16_t topHeight = kTopBarHeight;
  std::int16_t bottomTop = kBottomBarTop;
  std::int16_t contentOffsetY = 0;
  bool bottomVisible = true;

  void Clear() {
    top.Clear();
    content.Clear();
    bottom.Clear();
    contentOffsetY = 0;
  }
};

static_assert(sizeof(UiFrameScene) < 7'100);

} // namespace ui2
