/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Theme/UiPalette.h"

namespace ui2 {

class UiFrameRenderer {
public:
  static void RenderStatic(const UiFrameScene &scene,
                           UiIndexedSurface &surface,
                           const UiPalette &palette);
};

} // namespace ui2
