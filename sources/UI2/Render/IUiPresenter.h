/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"

#include <span>

namespace ui2 {

class UiIndexedSurface;
class UiPalette;

enum class PresentResult : std::uint8_t { Presented, Deferred, Failed };

class IUiPresenter {
public:
  virtual ~IUiPresenter() = default;
  virtual PresentResult Present(const UiIndexedSurface &surface,
                                const UiPalette &palette,
                                std::span<const DirtyStrip> strips) = 0;
};

} // namespace ui2
