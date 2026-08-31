/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Record/UiRecordView.h"

#include "Application/UI2/Ui2FixedText.h"
#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

RectI16 ResolvedCursorRect(const UiRecordViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiRecordView::CursorTargetRect(data.focus);
}

RectI16 ExpandedCursorDamage(RectI16 rect) {
  if (rect.Empty()) return {};
  return Intersect({static_cast<std::int16_t>(rect.x - 1),
                    static_cast<std::int16_t>(rect.y - 1),
                    static_cast<std::int16_t>(rect.width + 2),
                    static_cast<std::int16_t>(rect.height + 2)},
                   RectI16::Screen());
}

void DrawSection(UiSceneBuilder<256, 1024> &builder, std::string_view label,
                 std::int16_t y) {
  const std::int16_t width = UiFont5x7::TextWidth(label.size());
  builder.Text(label, 9, y, UiColorToken::TextColored);
  builder.Fill({static_cast<std::int16_t>(9 + width + 7),
                static_cast<std::int16_t>(y + 3),
                static_cast<std::int16_t>(222 - width), 1},
               UiColorToken::CursorRow);
}

std::string_view Instruction(UiRecordState state) {
  switch (state) {
  case UiRecordState::Unavailable:
    return "RECORDING UNAVAILABLE";
  case UiRecordState::Armed:
    return "PRESS PLAY TO RECORD";
  case UiRecordState::Recording:
    return "PRESS PLAY TO STOP";
  case UiRecordState::Saving:
    return "SAVING";
  }
  return {};
}

UiColorToken StateColor(UiRecordState state) {
  switch (state) {
  case UiRecordState::Unavailable:
  case UiRecordState::Saving:
    return UiColorToken::SystemWarning;
  case UiRecordState::Armed:
    // Preserve the approved bright-green armed affordance. Warning/error
    // states use their dedicated semantic system slots below.
    return UiColorToken::PlaybackActive;
  case UiRecordState::Recording:
    return UiColorToken::SystemError;
  }
  return UiColorToken::TextNormal;
}

void DrawSelectedInk(UiSceneBuilder<256, 1024> &builder,
                     const UiRecordViewData &data) {
  switch (data.focus) {
  case UiRecordFocus::Source:
    builder.Text("SOURCE", 9, 43, UiColorToken::TextHighlighted);
    builder.Text(data.source, 92, 43, UiColorToken::TextHighlighted);
    break;
  case UiRecordFocus::LineGain:
    builder.Text("LINE GAIN", 9, 54, UiColorToken::TextHighlighted);
    builder.Text(data.lineGain, 92, 54, UiColorToken::TextHighlighted);
    break;
  case UiRecordFocus::MicGain:
    builder.Text("MIC GAIN", 9, 65, UiColorToken::TextHighlighted);
    builder.Text(data.micGain, 92, 65, UiColorToken::TextHighlighted);
    break;
  case UiRecordFocus::None:
    break;
  }
}

UiBottomBarModel BottomBarFor(UiRecordState state) {
  UiBottomBarModel bottom{.kind = UiBottomBarKind::Hidden};
  if (state == UiRecordState::Armed) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"MONITOR", "RECORD", {}, {}};
    bottom.actions.count = 2;
    bottom.actions.active = 1;
  } else if (state == UiRecordState::Recording) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"MONITOR", "STOP", {}, {}};
    bottom.actions.count = 2;
    bottom.actions.active = 1;
  }
  return bottom;
}

} // namespace

void UiRecordView::RenderDelta(const UiRecordViewData &previous,
                               const UiRecordViewData &current,
                               const UiFrameScene &currentScene,
                               UiIndexedSurface &surface,
                               const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.power != current.power || previous.source != current.source)
    render({0, 0, 240, 34});
  if (previous.source != current.source) render({5, 40, 230, 12});
  if (previous.lineGain != current.lineGain) render({5, 52, 230, 11});
  if (previous.micGain != current.micGain) render({5, 63, 230, 11});
  if (previous.meterAvailable != current.meterAvailable ||
      previous.safeWidth != current.safeWidth ||
      previous.warningWidth != current.warningWidth) {
    render({9, 100, 222, 14});
  }
  if (previous.elapsed != current.elapsed ||
      previous.savingPercent != current.savingPercent ||
      previous.state != current.state) {
    render({0, 128, 240, 56});
  }
  if (previous.state != current.state)
    render({0, 208, 240, 32});

  const RectI16 oldCursor = ResolvedCursorRect(previous);
  const RectI16 newCursor = ResolvedCursorRect(current);
  if (oldCursor != newCursor || previous.focus != current.focus ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
  }
}

UiBuildStatus UiRecordView::Build(const UiRecordViewData &data,
                                  UiPalette &, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{
      .title = "RECORD", .meta = data.source, .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built) return topStatus;
  const UiBottomBarModel bottom = BottomBarFor(data.state);
  scene.bottomVisible = bottom.kind != UiBottomBarKind::Hidden;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built) return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text("SOURCE", 9, 43, UiColorToken::TextDim);
  builder.Text(data.source, 92, 43, UiColorToken::TextNormal);
  builder.Text("LINE GAIN", 9, 54, UiColorToken::TextDim);
  builder.Text(data.lineGain, 92, 54, UiColorToken::TextNormal);
  builder.Text("MIC GAIN", 9, 65, UiColorToken::TextDim);
  builder.Text(data.micGain, 92, 65, UiColorToken::TextNormal);
  DrawSection(builder, "LEVEL", 84);
  builder.Fill({9, 100, 222, 14}, UiColorToken::DerivedVuTrack);
  if (data.meterAvailable) {
    const std::int16_t safe = static_cast<std::int16_t>(
        std::min<std::uint16_t>(data.safeWidth, 222));
    builder.Fill({9, 100, safe, 14}, UiColorToken::VuSafe);
    const std::int16_t warning = static_cast<std::int16_t>(
        std::min<std::uint16_t>(data.warningWidth, 222 - safe));
    builder.Fill({static_cast<std::int16_t>(9 + safe), 100, warning, 14},
                 UiColorToken::VuWarning);
  }

  const UiColorToken stateColor = StateColor(data.state);
  if (data.state == UiRecordState::Saving) {
    std::array<char, 6> progress{};
    FormatUiPercent100(data.savingPercent, progress);
    builder.CenteredText(progress.data(), 120, 132, stateColor, 2);
  } else {
    const UiColorToken elapsedColor =
        data.state == UiRecordState::Recording
            ? UiColorToken::SystemError
            : (data.state == UiRecordState::Unavailable
                   ? UiColorToken::TextDim
                   : UiColorToken::TextNormal);
    builder.CenteredText(data.elapsed, 120, 132, elapsedColor, 2);
  }
  builder.CenteredText(Instruction(data.state), 120, 164, stateColor);

  const RectI16 cursor = ResolvedCursorRect(data);
  if (!cursor.Empty())
    builder.Selection(cursor);
  if (data.cursorInkVisible)
    DrawSelectedInk(builder, data);
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
