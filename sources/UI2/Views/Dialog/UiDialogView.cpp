/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Dialog/UiDialogView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

#include <algorithm>

namespace ui2 {

namespace {

constexpr std::string_view ActionLabel(UiDialogAction action) {
  switch (action) {
  case UiDialogAction::Ok:
    return "OK";
  case UiDialogAction::Yes:
    return "YES";
  case UiDialogAction::Cancel:
    return "CANCEL";
  case UiDialogAction::No:
    return "NO";
  }
  return {};
}

std::int16_t ActionCenter(std::uint8_t index, std::uint8_t count) {
  if (count <= 1U)
    return 120;
  const int span = std::min(142, 71 * (static_cast<int>(count) - 1));
  const int start = 120 - span / 2;
  return static_cast<std::int16_t>(
      start + (span * static_cast<int>(index)) /
                  (static_cast<int>(count) - 1));
}

void RenderActions(const UiDialogViewData &data,
                   UiSceneBuilder<256, 1024> &builder, std::int16_t y) {
  const std::uint8_t count = static_cast<std::uint8_t>(
      std::min<std::size_t>(data.actionCount, data.actions.size()));
  for (std::uint8_t index = 0; index < count; ++index) {
    const std::string_view label = ActionLabel(data.actions[index]);
    const std::int16_t center = ActionCenter(index, count);
    const bool selected = data.actionsFocused && index == data.selectedAction;
    if (selected) {
      const std::int16_t width = static_cast<std::int16_t>(
          UiFont5x7::TextWidth(label.size()) + 6);
      builder.Selection(
          {static_cast<std::int16_t>(center - width / 2),
           static_cast<std::int16_t>(y - 2), width, 11});
    }
    builder.CenteredText(label, center, y,
                         selected ? UiColorToken::TextHighlighted
                                  : UiColorToken::TextDim);
  }
}

} // namespace

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
    const std::uint8_t titleScale =
        UiFont5x7::TextWidth(data.title.size(), 2) <= 224 ? 2U : 1U;
    if (data.label.empty()) {
      builder.CenteredText(data.title, 120,
                           titleScale == 2U ? 112 : 116,
                           UiColorToken::SystemError, titleScale);
    } else {
      builder.CenteredText(data.title, 120,
                           titleScale == 2U ? 102 : 106,
                           UiColorToken::SystemError, titleScale);
      builder.CenteredText(data.label, 120, 122, UiColorToken::TextDim);
    }
    return builder.Ok() ? UiBuildStatus::Built
                        : UiBuildStatus::CommandOverflow;
  }

  scene.bottomVisible = false;
  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Fill({28, 72, 184, 92}, UiColorToken::TextNormal);
  builder.Fill({31, 75, 178, 86}, UiColorToken::SurfaceBackground);
  switch (data.kind) {
  case UiDialogKind::Message:
    if (data.label.empty()) {
      builder.CenteredText(data.title, 120, 94, UiColorToken::TextNormal);
      RenderActions(data, builder, 132);
    } else {
      builder.CenteredText(data.title, 120, 88, UiColorToken::TextNormal);
      builder.CenteredText(data.label, 120, 105, UiColorToken::TextDim);
      RenderActions(data, builder, 139);
    }
    break;
  case UiDialogKind::TextInput: {
    builder.CenteredText(data.title, 120, 88, UiColorToken::TextNormal);
    builder.Text(data.label, 42, 108, UiColorToken::TextDim);
    const std::int16_t valueX = static_cast<std::int16_t>(
        42 + UiFont5x7::TextWidth(data.label.size()) + 15);
    const std::int16_t selectionX = static_cast<std::int16_t>(valueX - 4);
    if (!data.actionsFocused) {
      builder.Selection(
          {selectionX, 105, static_cast<std::int16_t>(192 - selectionX), 11});
    }
    builder.Text(data.value, valueX, 107,
                 data.actionsFocused ? UiColorToken::TextNormal
                                     : UiColorToken::TextHighlighted);
    RenderActions(data, builder, 139);
    break;
  }
  case UiDialogKind::RenderProgress:
    if (data.label.empty()) {
      builder.CenteredText(data.title, 120, 91, UiColorToken::SystemWarning);
      builder.CenteredText(data.elapsed, 120, 108,
                           UiColorToken::SystemWarning);
    } else {
      builder.CenteredText(data.title, 120, 83, UiColorToken::SystemWarning);
      builder.CenteredText(data.label, 120, 98, UiColorToken::TextNormal);
      builder.CenteredText(data.elapsed, 120, 113,
                           UiColorToken::SystemWarning);
    }
    builder.Fill({48, 126, 144, 7}, UiColorToken::DerivedVuTrack);
    builder.Fill(
        {48, 126,
         static_cast<std::int16_t>(
             std::min<std::uint8_t>(data.progressWidth, 144)),
        7},
        UiColorToken::CursorPrimary);
    RenderActions(data, builder, 145);
    break;
  case UiDialogKind::FullScreen:
    break;
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
