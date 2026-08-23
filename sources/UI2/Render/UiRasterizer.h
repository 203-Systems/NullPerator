/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiCommandList.h"

#include <span>

namespace ui2 {

class UiRasterizer {
public:
  static void Render(UiCommandStream stream, UiIndexedSurface &surface,
                     const UiPalette *palette = nullptr,
                     PointI16 origin = {},
                     RectI16 clip = RectI16::Screen());
};

} // namespace ui2
