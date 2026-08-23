/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Render/UiFrameRenderer.h"

#include "UI2/Render/UiRasterizer.h"

namespace ui2 {

void UiFrameRenderer::RenderStatic(const UiFrameScene &scene,
                                   UiIndexedSurface &surface,
                                   const UiPalette &palette) {
  surface.Clear(palette.Index(UiColorToken::SurfaceBlack));
  surface.FillRect({5, 5, 230, 230},
                   palette.Index(UiColorToken::SurfaceField));
  surface.FillRect({0, 0, kScreenWidth, scene.topHeight},
                   palette.Index(scene.topBackground));
  const std::int16_t contentBottom =
      scene.bottomVisible ? scene.bottomTop : kScreenHeight;
  UiRasterizer::Render(scene.content.Stream(), surface, &palette, {},
                       {0, scene.topHeight, kScreenWidth,
                        static_cast<std::int16_t>(contentBottom -
                                                  scene.topHeight)});
  UiRasterizer::Render(scene.top.Stream(), surface, &palette, {},
                       {0, 0, kScreenWidth, scene.topHeight});
  if (scene.bottomVisible) {
    surface.FillRect(
        {0, scene.bottomTop, kScreenWidth,
         static_cast<std::int16_t>(kScreenHeight - scene.bottomTop)},
        palette.Index(scene.bottomBackground));
    UiRasterizer::Render(
        scene.bottom.Stream(), surface, &palette, {},
        {0, scene.bottomTop, kScreenWidth,
         static_cast<std::int16_t>(kScreenHeight - scene.bottomTop)});
  }
}

} // namespace ui2
