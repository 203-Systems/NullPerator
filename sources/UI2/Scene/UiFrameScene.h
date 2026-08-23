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
  UiColorToken topBackground = UiColorToken::SurfaceBarDeep;
  UiColorToken bottomBackground = UiColorToken::SurfaceBarDeep;
  std::int16_t topHeight = kTopBarHeight;
  std::int16_t bottomTop = kBottomBarTop;
  bool bottomVisible = true;

  void Clear() {
    top.Clear();
    content.Clear();
    bottom.Clear();
  }
};

static_assert(sizeof(UiFrameScene) < 7'100);

} // namespace ui2
