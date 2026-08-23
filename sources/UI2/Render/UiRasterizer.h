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
  static void Render(std::span<const UiCommand> commands,
                     UiIndexedSurface &surface);
};

} // namespace ui2
