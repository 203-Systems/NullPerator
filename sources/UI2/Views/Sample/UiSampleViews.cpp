/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Sample/UiSampleViews.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

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
         left.waveformMask.data() != right.waveformMask.data() ||
         left.waveformMask.size() != right.waveformMask.size();
}

void DrawField(UiSceneBuilder<256, 1024> &builder, std::string_view label,
               std::string_view value, std::int16_t y) {
  builder.Text(label, 9, y, UiColorToken::TextMuted);
  builder.Text(value, 92, y, UiColorToken::TextPrimary);
}

void DrawSection(UiSceneBuilder<256, 1024> &builder, std::string_view label,
                 std::int16_t y) {
  const std::int16_t width = UiFont5x7::TextWidth(label.size());
  builder.Text(label, 9, y, UiColorToken::CursorPrimary);
  builder.Fill({static_cast<std::int16_t>(9 + width + 7),
                static_cast<std::int16_t>(y + 3),
                static_cast<std::int16_t>(222 - width), 1},
               UiColorToken::CursorRow);
}

} // namespace

void UiSampleEditorView::RenderDelta(
    const UiSampleEditorViewData &previous,
    const UiSampleEditorViewData &current, const UiFrameScene &currentScene,
    UiIndexedSurface &surface, const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.power != current.power) render({184, 0, 56, 34});
  if (previous.name != current.name) render({5, 40, 230, 12});
  if (WaveformChanged(previous, current)) render({9, 60, 222, 72});
  if (previous.start != current.start) render({5, 143, 230, 12});
  if (previous.end != current.end) render({5, 154, 230, 12});
  if (previous.loop != current.loop) render({5, 165, 230, 12});
  if (previous.gain != current.gain) render({5, 176, 230, 12});
  const RectI16 oldCursor =
      ResolvedCursorRect(previous, CursorTargetRect());
  const RectI16 newCursor = ResolvedCursorRect(current, CursorTargetRect());
  if (oldCursor != newCursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
  }
}

UiBuildStatus UiSampleEditorView::Build(const UiSampleEditorViewData &data,
                                        UiPalette &, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceBarDeep;
  scene.bottomBackground = UiColorToken::SurfaceBarDeep;
  const UiTopBarModel top{
      .title = "SAMPLE", .meta = "EDIT", .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built) return topStatus;
  UiBottomBarModel bottom{.kind = UiBottomBarKind::Actions};
  bottom.actions.actions = {"SAVE", "TRIM", "DISCARD", {}};
  bottom.actions.count = 3;
  bottom.actions.active = 0;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built) return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  DrawSection(builder, data.name, 42);
  builder.Fill({9, 60, 222, 72}, UiColorToken::VuTrack);
  builder.SparseCoverageMask({9, 60, 222, 72}, data.waveformMask,
                             UiCoverage::Cursor, UiColorToken::VuTrack);
  DrawField(builder, "START", data.start, 145);
  DrawField(builder, "END", data.end, 156);
  DrawField(builder, "LOOP", data.loop, 167);
  DrawField(builder, "GAIN", data.gain, 178);
  builder.Selection(ResolvedCursorRect(data, CursorTargetRect()));
  if (data.cursorInkVisible) {
    builder.Text("START", 9, 145, UiColorToken::CursorInk);
    builder.Text(data.start, 92, 145, UiColorToken::CursorInk);
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

void UiSampleSlicesView::RenderDelta(
    const UiSampleSlicesViewData &previous,
    const UiSampleSlicesViewData &current, const UiFrameScene &currentScene,
    UiIndexedSurface &surface, const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.sliceCount != current.sliceCount)
    render({0, 0, 240, 34});
  else if (previous.power != current.power)
    render({184, 0, 56, 34});
  if (WaveformChanged(previous, current) ||
      previous.selectedMarker != current.selectedMarker) {
    render({9, 44, 222, 84});
  }
  if (previous.slice != current.slice) render({5, 137, 230, 12});
  if (previous.start != current.start) render({5, 148, 230, 12});
  if (previous.zoom != current.zoom) render({5, 159, 230, 12});
  const RectI16 oldCursor =
      ResolvedCursorRect(previous, CursorTargetRect());
  const RectI16 newCursor = ResolvedCursorRect(current, CursorTargetRect());
  if (oldCursor != newCursor ||
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
  scene.topBackground = UiColorToken::SurfaceBarDeep;
  scene.bottomBackground = UiColorToken::SurfaceBarDeep;
  const UiTopBarModel top{
      .title = "SLICES", .meta = data.sliceCount, .power = data.power};
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
  builder.Fill({9, 46, 222, 78}, UiColorToken::VuTrack);
  builder.SparseCoverageMask({9, 46, 222, 78}, data.waveformMask,
                             UiCoverage::Playback, UiColorToken::VuTrack);
  constexpr std::array<std::int16_t, 5> kMarkerX{9, 64, 119, 174, 230};
  for (std::size_t index = 0; index < kMarkerX.size(); ++index) {
    builder.Fill({kMarkerX[index], 44, 1, 84},
                 index == data.selectedMarker ? UiColorToken::CursorPrimary
                                              : UiColorToken::TextMuted);
  }
  DrawField(builder, "SLICE", data.slice, 139);
  DrawField(builder, "START", data.start, 150);
  DrawField(builder, "ZOOM", data.zoom, 161);
  builder.Text("NAV SELECT  EDIT FINE", 9, 188, UiColorToken::TextDim);
  builder.Selection(ResolvedCursorRect(data, CursorTargetRect()));
  if (data.cursorInkVisible) {
    builder.Text("SLICE", 9, 139, UiColorToken::CursorInk);
    builder.Text(data.slice, 92, 139, UiColorToken::CursorInk);
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
