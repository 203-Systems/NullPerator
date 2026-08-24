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
  RenderRegion(scene, surface, palette, RectI16::Screen());
}

void UiFrameRenderer::RenderRegion(const UiFrameScene &scene,
                                   UiIndexedSurface &surface,
                                   const UiPalette &palette,
                                   RectI16 region) {
  region = Intersect(region, RectI16::Screen());
  if (region.Empty()) return;

  surface.FillRect(region, palette.Index(UiColorToken::SurfaceBackground));
  surface.FillRect({0, 0, kScreenWidth, scene.topHeight},
                   palette.Index(scene.topBackground), region);
  const std::int16_t contentBottom =
      scene.bottomVisible ? scene.bottomTop : kScreenHeight;
  UiRasterizer::Render(scene.content.Stream(), surface, &palette,
                       {0, static_cast<std::int16_t>(-scene.contentOffsetY)},
                       Intersect(region,
                                 {0, scene.topHeight, kScreenWidth,
                                  static_cast<std::int16_t>(
                                      contentBottom - scene.topHeight)}));
  UiRasterizer::Render(scene.top.Stream(), surface, &palette, {},
                       Intersect(region,
                                 {0, 0, kScreenWidth, scene.topHeight}));
  if (scene.bottomVisible) {
    surface.FillRect(
        {0, scene.bottomTop, kScreenWidth,
         static_cast<std::int16_t>(kScreenHeight - scene.bottomTop)},
        palette.Index(scene.bottomBackground), region);
    UiRasterizer::Render(
        scene.bottom.Stream(), surface, &palette, {},
        Intersect(region,
                  {0, scene.bottomTop, kScreenWidth,
                   static_cast<std::int16_t>(kScreenHeight -
                                             scene.bottomTop)}));
  }
  // Overlay commands use absolute screen coordinates and are intentionally
  // last. A modal therefore stays anchored while a list beneath it scrolls,
  // and its fixed 256-byte payload never competes with waveform data.
  UiRasterizer::Render(scene.overlay.Stream(), surface, &palette, {}, region);
}

} // namespace ui2
