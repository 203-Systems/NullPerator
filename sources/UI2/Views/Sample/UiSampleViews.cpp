/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Sample/UiSampleViews.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

template <typename Data>
RectI16 ResolvedCursorRect(const Data &data, RectI16 fallback) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return fallback;
}

RectI16 ExpandedCursorDamage(RectI16 rect) {
  if (rect.Empty()) return {};
  return Intersect({static_cast<std::int16_t>(rect.x - 1),
                    static_cast<std::int16_t>(rect.y - 1),
                    static_cast<std::int16_t>(rect.width + 2),
                    static_cast<std::int16_t>(rect.height + 2)},
                   RectI16::Screen());
}

template <typename Data>
bool WaveformChanged(const Data &left, const Data &right) {
  return left.waveformRevision != right.waveformRevision ||
         left.waveformMask.size() != right.waveformMask.size();
}

bool MarkersChanged(std::span<const UiSampleWaveformMarker> left,
                    std::span<const UiSampleWaveformMarker> right) {
  return left.size() != right.size() ||
         !std::equal(left.begin(), left.end(), right.begin());
}

void DrawMarkers(UiSceneBuilder<256, 1024> &builder,
                 std::span<const UiSampleWaveformMarker> markers,
                 std::int16_t y, std::int16_t height) {
  for (const UiSampleWaveformMarker marker : markers) {
    const std::int16_t x = static_cast<std::int16_t>(
        9 + std::min<std::uint8_t>(marker.x, 221U));
    UiColorToken color = marker.selected ? UiColorToken::TextColored
                                         : UiColorToken::TextDim;
    if (marker.kind == UiSampleWaveformMarkerKind::Playhead)
      color = UiColorToken::TextNormal;
    builder.Fill({x, y, 1, height}, color);
  }
}

void DrawField(UiSceneBuilder<256, 1024> &builder, std::string_view label,
               std::string_view value, std::int16_t y) {
  builder.Text(label, 9, y, UiColorToken::TextDim);
  builder.Text(value, 92, y, UiColorToken::TextNormal);
}

void DrawUserSection(UiSceneBuilder<256, 1024> &builder,
                     std::string_view label, std::int16_t y) {
  const std::int16_t width = UiFont5x7::TextWidth(label.size());
  builder.UserText(label, 9, y, UiColorToken::TextColored);
  builder.Fill({static_cast<std::int16_t>(9 + width + 7),
                static_cast<std::int16_t>(y + 3),
                static_cast<std::int16_t>(222 - width), 1},
               UiColorToken::CursorRow);
}

} // namespace

RectI16 UiSampleEditorView::CursorTargetRect(UiSampleEditorCursor cursor) {
  switch (cursor) {
  case UiSampleEditorCursor::Name:
    return {7, 41, 226, 9};
  case UiSampleEditorCursor::Waveform:
    return {7, 58, 226, 76};
  case UiSampleEditorCursor::Start:
    return {7, 144, 226, 9};
  case UiSampleEditorCursor::End:
    return {7, 155, 226, 9};
  case UiSampleEditorCursor::Field3:
    return {7, 166, 226, 9};
  case UiSampleEditorCursor::Field4:
    return {7, 177, 226, 9};
  case UiSampleEditorCursor::Save:
  case UiSampleEditorCursor::SaveAndLoad:
  case UiSampleEditorCursor::Discard:
  case UiSampleEditorCursor::None:
    return {};
  }
  return {};
}

void UiSampleEditorView::RenderDelta(
    const UiSampleEditorViewData &previous,
    const UiSampleEditorViewData &current, const UiFrameScene &currentScene,
    UiIndexedSurface &surface, const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.power != current.power) render({184, 0, 56, 34});
  if (previous.name != current.name) render({5, 40, 230, 12});
  if (WaveformChanged(previous, current) ||
      MarkersChanged(previous.markers, current.markers))
    render({7, 58, 226, 76});
  if (previous.start != current.start) render({5, 143, 230, 12});
  if (previous.end != current.end) render({5, 154, 230, 12});
  if (previous.field3Label != current.field3Label ||
      previous.field3Value != current.field3Value)
    render({5, 165, 230, 12});
  if (previous.field4Label != current.field4Label ||
      previous.field4Value != current.field4Value)
    render({5, 176, 230, 12});
  if (previous.help != current.help) render({5, 191, 230, 16});
  const RectI16 oldCursor =
      ResolvedCursorRect(previous, CursorTargetRect(previous));
  const RectI16 newCursor =
      ResolvedCursorRect(current, CursorTargetRect(current));
  if (oldCursor != newCursor ||
      previous.cursor != current.cursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
  }
  if (previous.bottomActions != current.bottomActions ||
      previous.bottomActionCount != current.bottomActionCount ||
      previous.bottomActive != current.bottomActive)
    render({0, 208, 240, 32});
}

UiBuildStatus UiSampleEditorView::Build(const UiSampleEditorViewData &data,
                                        UiPalette &, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{.title = "SAMPLE", .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built) return topStatus;
  UiBottomBarModel bottom{.kind = UiBottomBarKind::Actions};
  bottom.actions.actions = data.bottomActions;
  bottom.actions.count = std::min<std::uint8_t>(
      data.bottomActionCount,
      static_cast<std::uint8_t>(data.bottomActions.size()));
  bottom.actions.active = data.bottomActive;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built) return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  const RectI16 cursor = ResolvedCursorRect(data, CursorTargetRect(data));
  const bool waveformFocused =
      data.cursor == UiSampleEditorCursor::Waveform && data.cursorInkVisible;
  if (waveformFocused)
    builder.Selection(cursor);
  DrawUserSection(builder, data.name, 42);
  if (!waveformFocused)
    builder.Fill({9, 60, 222, 72}, UiColorToken::DerivedVuTrack);
  builder.SparseCoverageMask({9, 60, 222, 72}, data.waveformMask,
                             UiCoverage::Cursor, UiColorToken::DerivedVuTrack);
  DrawMarkers(builder, data.markers, 60, 72);
  DrawField(builder, "START", data.start, 145);
  DrawField(builder, "END", data.end, 156);
  DrawField(builder, data.field3Label, data.field3Value, 167);
  DrawField(builder, data.field4Label, data.field4Value, 178);
  if (!data.help.empty())
    builder.Text(data.help, 9, 198, UiColorToken::DerivedTextFaint);
  if (!waveformFocused && !cursor.Empty())
    builder.Selection(cursor);
  if (data.cursorInkVisible) {
    switch (data.cursor) {
    case UiSampleEditorCursor::Name:
      builder.UserText(data.name, 9, 42, UiColorToken::TextHighlighted);
      break;
    case UiSampleEditorCursor::Start:
      if (data.enterDigitFocus && !data.start.empty()) {
        const std::uint8_t digit = std::min<std::uint8_t>(
            data.focusDigit,
            static_cast<std::uint8_t>(data.start.size() - 1U));
        builder.Text(data.start.substr(digit, 1),
                     static_cast<std::int16_t>(92 + digit * 6), 145,
                     UiColorToken::TextHighlighted);
      } else {
        builder.Text("START", 9, 145, UiColorToken::TextHighlighted);
        builder.Text(data.start, 92, 145, UiColorToken::TextHighlighted);
      }
      break;
    case UiSampleEditorCursor::End:
      if (data.enterDigitFocus && !data.end.empty()) {
        const std::uint8_t digit = std::min<std::uint8_t>(
            data.focusDigit,
            static_cast<std::uint8_t>(data.end.size() - 1U));
        builder.Text(data.end.substr(digit, 1),
                     static_cast<std::int16_t>(92 + digit * 6), 156,
                     UiColorToken::TextHighlighted);
      } else {
        builder.Text("END", 9, 156, UiColorToken::TextHighlighted);
        builder.Text(data.end, 92, 156, UiColorToken::TextHighlighted);
      }
      break;
    case UiSampleEditorCursor::Field3:
      builder.Text(data.field3Label, 9, 167,
                   UiColorToken::TextHighlighted);
      builder.Text(data.field3Value, 92, 167,
                   UiColorToken::TextHighlighted);
      break;
    case UiSampleEditorCursor::Field4:
      builder.Text(data.field4Label, 9, 178,
                   UiColorToken::TextHighlighted);
      builder.Text(data.field4Value, 92, 178,
                   UiColorToken::TextHighlighted);
      break;
    case UiSampleEditorCursor::Waveform:
    case UiSampleEditorCursor::Save:
    case UiSampleEditorCursor::SaveAndLoad:
    case UiSampleEditorCursor::Discard:
    case UiSampleEditorCursor::None:
      break;
    }
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

RectI16 UiSampleSlicesView::CursorTargetRect(UiSampleSlicesCursor cursor) {
  switch (cursor) {
  case UiSampleSlicesCursor::Status:
    return {7, 138, 226, 9};
  case UiSampleSlicesCursor::Waveform:
    return {7, 43, 226, 86};
  case UiSampleSlicesCursor::AutoSliceCount:
    return {7, 173, 226, 9};
  case UiSampleSlicesCursor::AutoSlice:
    return {7, 184, 226, 9};
  case UiSampleSlicesCursor::None:
    return {};
  }
  return {};
}

void UiSampleSlicesView::RenderDelta(
    const UiSampleSlicesViewData &previous,
    const UiSampleSlicesViewData &current, const UiFrameScene &currentScene,
    UiIndexedSurface &surface, const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.power != current.power)
    render({184, 0, 56, 34});
  if (WaveformChanged(previous, current) ||
      previous.selectedMarker != current.selectedMarker ||
      MarkersChanged(previous.markers, current.markers)) {
    render({9, 44, 222, 84});
  }
  if (previous.slice != current.slice) render({5, 137, 230, 12});
  if (previous.start != current.start) render({5, 148, 230, 12});
  if (previous.zoom != current.zoom) render({5, 159, 230, 12});
  if (previous.autoSliceCount != current.autoSliceCount ||
      previous.autoSliceApplyAvailable != current.autoSliceApplyAvailable)
    render({5, 172, 230, 23});
  if (previous.help != current.help) render({5, 184, 230, 23});
  const RectI16 oldCursor =
      ResolvedCursorRect(previous, CursorTargetRect(previous));
  const RectI16 newCursor =
      ResolvedCursorRect(current, CursorTargetRect(current));
  if (oldCursor != newCursor ||
      previous.cursor != current.cursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
  }
}

UiBuildStatus UiSampleSlicesView::Build(const UiSampleSlicesViewData &data,
                                        UiPalette &, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{.title = "SLICES", .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built) return topStatus;
  UiBottomBarModel bottom{.kind = UiBottomBarKind::Actions};
  bottom.actions.actions = {"ADD", "MOVE", "DELETE", {}};
  bottom.actions.count = 3;
  bottom.actions.active = 1;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built) return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  const RectI16 cursor = ResolvedCursorRect(data, CursorTargetRect(data));
  const bool waveformFocused =
      data.cursor == UiSampleSlicesCursor::Waveform && data.cursorInkVisible;
  if (waveformFocused)
    builder.Selection(cursor);
  if (!waveformFocused)
    builder.Fill({9, 46, 222, 78}, UiColorToken::DerivedVuTrack);
  builder.SparseCoverageMask({9, 46, 222, 78}, data.waveformMask,
                             UiCoverage::Playback, UiColorToken::DerivedVuTrack);
  if (data.markers.empty()) {
    constexpr std::array<std::int16_t, 5> kMarkerX{9, 64, 119, 174, 230};
    for (std::size_t index = 0; index < kMarkerX.size(); ++index) {
      builder.Fill({kMarkerX[index], 44, 1, 84},
                   index == data.selectedMarker ? UiColorToken::TextColored
                                                : UiColorToken::TextDim);
    }
  } else {
    DrawMarkers(builder, data.markers, 44, 84);
  }
  DrawField(builder, "SLICE", data.slice, 139);
  DrawField(builder, "START", data.start, 150);
  DrawField(builder, "ZOOM", data.zoom, 161);
  if (data.autoSliceCount.empty()) {
    builder.Text(data.help, 9, 188, UiColorToken::DerivedTextFaint);
  } else {
    DrawField(builder, "AUTO", data.autoSliceCount, 174);
    DrawField(builder, "SLICE",
              data.autoSliceApplyAvailable ? "APPLY" : "LOCKED", 185);
    builder.Text(data.help, 9, 198, UiColorToken::DerivedTextFaint);
  }
  if (!waveformFocused && !cursor.Empty())
    builder.Selection(cursor);
  if (data.cursorInkVisible) {
    switch (data.cursor) {
    case UiSampleSlicesCursor::Status:
      builder.Text("SLICE", 9, 139, UiColorToken::TextHighlighted);
      builder.Text(data.slice, 92, 139, UiColorToken::TextHighlighted);
      break;
    case UiSampleSlicesCursor::AutoSliceCount:
      builder.Text("AUTO", 9, 174, UiColorToken::TextHighlighted);
      builder.Text(data.autoSliceCount, 92, 174,
                   UiColorToken::TextHighlighted);
      break;
    case UiSampleSlicesCursor::AutoSlice:
      builder.Text("SLICE", 9, 185, UiColorToken::TextHighlighted);
      builder.Text(data.autoSliceApplyAvailable ? "APPLY" : "LOCKED", 92,
                   185, UiColorToken::TextHighlighted);
      break;
    case UiSampleSlicesCursor::Waveform:
    case UiSampleSlicesCursor::None:
      break;
    }
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
