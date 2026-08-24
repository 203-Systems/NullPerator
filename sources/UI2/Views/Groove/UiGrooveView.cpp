/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Groove/UiGrooveView.h"

#include "UI2/Render/UiFrameRenderer.h"

#include <array>

namespace ui2 {
namespace {

std::array<char, 3> HexByte(std::uint8_t value) {
  constexpr char digits[] = "0123456789ABCDEF";
  return {digits[value >> 4U], digits[value & 0x0FU], 0};
}

RectI16 ResolvedCursorRect(const UiGrooveViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiGrooveView::CursorTargetRect(data.editRow);
}

RectI16 ExpandedCursorDamage(RectI16 rect) {
  if (rect.Empty())
    return {};
  return Intersect({static_cast<std::int16_t>(rect.x - 1),
                    static_cast<std::int16_t>(rect.y - 1),
                    static_cast<std::int16_t>(rect.width + 2),
                    static_cast<std::int16_t>(rect.height + 2)},
                   RectI16::Screen());
}

} // namespace

RectI16 UiGrooveView::CursorTargetRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return {27, static_cast<std::int16_t>(48 + row * 9), 15, 9};
}

RectI16 UiGrooveView::RowDamageRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return {5, static_cast<std::int16_t>(47 + row * 9), 40, 11};
}

void UiGrooveView::RenderDelta(const UiGrooveViewData &previous,
                               const UiGrooveViewData &current,
                               const UiFrameScene &currentScene,
                               UiIndexedSurface &surface,
                               const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.number != current.number)
    render({80, 0, 40, 34});
  if (previous.power != current.power)
    render({184, 0, 56, 34});

  const RectI16 oldCursor = ResolvedCursorRect(previous);
  const RectI16 newCursor = ResolvedCursorRect(current);
  if (oldCursor != newCursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
    render(RowDamageRect(previous.editRow));
    render(RowDamageRect(current.editRow));
  }
  for (std::uint8_t row = 0; row < 16U; ++row) {
    if (previous.steps[row] != current.steps[row])
      render(RowDamageRect(row));
  }
}

UiBuildStatus UiGrooveView::Build(const UiGrooveViewData &data, UiPalette &,
                                  UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomVisible = false;
  scene.topBackground = UiColorToken::SurfaceBarDeep;
  const UiTopBarModel top{
      .title = "GROOVE",
      .meta = data.number,
      .power = data.power,
  };
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text("STEP", 28, 39, UiColorToken::CursorPrimary);
  const RectI16 cursor = ResolvedCursorRect(data);
  for (std::uint8_t row = 0; row < 16U; ++row) {
    const std::int16_t y = static_cast<std::int16_t>(49 + row * 9);
    const auto rowText = HexByte(row);
    builder.Text(rowText.data(), 8, y,
                 row == data.editRow ? UiColorToken::CursorPrimary
                                     : UiColorToken::TextDim);
    const auto value = HexByte(data.steps[row]);
    const char *display = data.steps[row] == 0xFFU ? "--" : value.data();
    builder.Text(display, 29, y,
                 data.steps[row] == 0xFFU ? UiColorToken::TextDim
                                          : UiColorToken::TextPrimary);
  }
  builder.Selection(cursor);
  if (data.cursorInkVisible && data.editRow < 16U) {
    const auto value = HexByte(data.steps[data.editRow]);
    const char *display =
        data.steps[data.editRow] == 0xFFU ? "--" : value.data();
    builder.Text(display, 29, static_cast<std::int16_t>(49 + data.editRow * 9),
                 UiColorToken::CursorInk);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
