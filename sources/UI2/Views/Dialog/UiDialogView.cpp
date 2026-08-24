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
    builder.Fill({0, 0, 240, 10}, UiColorToken::TextPrimary);
    builder.Fill({0, 230, 240, 10}, UiColorToken::TextPrimary);
    builder.CenteredText("DIAGNOSTIC", 120, 102, UiColorToken::VuPeak, 2);
    builder.CenteredText("FULL SCREEN", 120, 122, UiColorToken::TextMuted);
    return builder.Ok() ? UiBuildStatus::Built
                        : UiBuildStatus::CommandOverflow;
  }

  scene.bottomVisible = false;
  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Fill({28, 72, 184, 92}, UiColorToken::TextPrimary);
  builder.Fill({31, 75, 178, 86}, UiColorToken::SurfaceField);
  switch (data.kind) {
  case UiDialogKind::Message:
    builder.CenteredText(data.title, 120, 94, UiColorToken::TextPrimary);
    builder.CenteredText("OK", 85, 132, UiColorToken::CursorPrimary);
    builder.CenteredText("CANCEL", 156, 132, UiColorToken::TextMuted);
    break;
  case UiDialogKind::TextInput:
    builder.CenteredText(data.title, 120, 88, UiColorToken::TextPrimary);
    builder.Text(data.label, 42, 108, UiColorToken::TextMuted);
    builder.Selection({76, 105, 116, 11});
    builder.Text(data.value, 80, 107, UiColorToken::CursorInk);
    builder.CenteredText("OK", 85, 139, UiColorToken::CursorPrimary);
    builder.CenteredText("CANCEL", 156, 139, UiColorToken::TextMuted);
    break;
  case UiDialogKind::RenderProgress:
    builder.CenteredText("RENDERING", 120, 91, UiColorToken::VuWarning);
    builder.CenteredText(data.elapsed, 120, 108, UiColorToken::VuWarning);
    builder.Fill({48, 126, 144, 7}, UiColorToken::VuTrack);
    builder.Fill(
        {48, 126,
         static_cast<std::int16_t>(
             std::min<std::uint8_t>(data.progressWidth, 144)),
         7},
        UiColorToken::CursorPrimary);
    builder.CenteredText("CANCEL", 120, 145, UiColorToken::TextPrimary);
    break;
  case UiDialogKind::FullScreen:
    break;
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
