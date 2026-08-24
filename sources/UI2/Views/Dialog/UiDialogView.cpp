/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Dialog/UiDialogView.h"

#include "UI2/Render/UiFrameRenderer.h"

#include <algorithm>

namespace ui2 {

RectI16 UiDialogView::DamageRect(UiDialogKind kind) {
  return kind == UiDialogKind::FullScreen ? RectI16::Screen()
                                          : RectI16{27, 71, 186, 94};
}

void UiDialogView::RenderDelta(const UiDialogViewData &previous,
                               const UiDialogViewData &current,
                               const UiFrameScene &currentScene,
                               UiIndexedSurface &surface,
                               const UiPalette &palette) {
  if (previous == current) return;
  if (previous.kind != current.kind) {
    UiFrameRenderer::RenderStatic(currentScene, surface, palette);
    return;
  }
  UiFrameRenderer::RenderRegion(currentScene, surface, palette,
                                DamageRect(current.kind));
}

UiBuildStatus UiDialogView::Apply(const UiDialogViewData &data,
                                  UiFrameScene &scene) {
  if (data.kind == UiDialogKind::FullScreen) {
    scene.Clear();
    scene.topHeight = 0;
    scene.bottomVisible = false;
    UiSceneBuilder<256, 1024> builder(scene.content);
    builder.Fill({0, 0, 240, 10}, UiColorToken::TextNormal);
    builder.Fill({0, 230, 240, 10}, UiColorToken::TextNormal);
    builder.CenteredText("DIAGNOSTIC", 120, 102, UiColorToken::SystemError, 2);
    builder.CenteredText("FULL SCREEN", 120, 122, UiColorToken::TextDim);
    return builder.Ok() ? UiBuildStatus::Built
                        : UiBuildStatus::CommandOverflow;
  }

  scene.bottomVisible = false;
  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Fill({28, 72, 184, 92}, UiColorToken::TextNormal);
  builder.Fill({31, 75, 178, 86}, UiColorToken::SurfaceBackground);
  switch (data.kind) {
  case UiDialogKind::Message:
    builder.CenteredText(data.title, 120, 94, UiColorToken::TextNormal);
    builder.CenteredText("OK", 85, 132, UiColorToken::TextColored);
    builder.CenteredText("CANCEL", 156, 132, UiColorToken::TextDim);
    break;
  case UiDialogKind::TextInput:
    builder.CenteredText(data.title, 120, 88, UiColorToken::TextNormal);
    builder.Text(data.label, 42, 108, UiColorToken::TextDim);
    builder.Selection({76, 105, 116, 11});
    builder.Text(data.value, 80, 107, UiColorToken::TextHighlighted);
    builder.CenteredText("OK", 85, 139, UiColorToken::TextColored);
    builder.CenteredText("CANCEL", 156, 139, UiColorToken::TextDim);
    break;
  case UiDialogKind::RenderProgress:
    builder.CenteredText("RENDERING", 120, 91, UiColorToken::SystemWarning);
    builder.CenteredText(data.elapsed, 120, 108, UiColorToken::SystemWarning);
    builder.Fill({48, 126, 144, 7}, UiColorToken::DerivedVuTrack);
    builder.Fill(
        {48, 126,
         static_cast<std::int16_t>(
             std::min<std::uint8_t>(data.progressWidth, 144)),
         7},
        UiColorToken::CursorPrimary);
    builder.CenteredText("CANCEL", 120, 145, UiColorToken::TextNormal);
    break;
  case UiDialogKind::FullScreen:
    break;
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
