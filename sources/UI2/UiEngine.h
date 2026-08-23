/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Render/IUiPresenter.h"
#include "UI2/Render/UiDirtyTiles.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Render/UiRasterizer.h"
#include "UI2/Scene/UiCommandList.h"
#include "UI2/Theme/UiPalette.h"

#include <span>

namespace ui2 {

struct UiEngineStorage {
  UiSurfaceStorage surface;
  DirtyStripList strips;
};

class UiEngine {
public:
  UiEngine(UiEngineStorage &storage, IUiPresenter &presenter)
      : storage_(storage), surface_(storage.surface), presenter_(presenter) {}

  UiPalette &Palette() { return palette_; }
  UiIndexedSurface &Surface() { return surface_; }

  template <std::size_t Capacity, std::size_t TextCapacity>
  PresentResult
  RenderAndPresent(const UiCommandList<Capacity, TextCapacity> &commands) {
    UiRasterizer::Render(commands.Stream(), surface_, &palette_);
    return PresentDirty();
  }

  PresentResult PresentDirty();

private:
  UiEngineStorage &storage_;
  UiIndexedSurface surface_;
  UiPalette palette_;
  IUiPresenter &presenter_;
};

} // namespace ui2
