/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Chain/UiChainView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Render/UiVuGradient.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

std::array<char, 3> HexByte(std::uint8_t value) {
  constexpr char digits[] = "0123456789ABCDEF";
  return {digits[value >> 4U], digits[value & 0x0FU], 0};
}

RectI16 ResolvedCursorRect(const UiChainViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiChainView::CursorTargetRect(data.editRow);
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

RectI16 UiChainView::CursorTargetRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return {31, static_cast<std::int16_t>(48 + row * 9), 15, 9};
}

RectI16 UiChainView::RowDamageRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return {5, static_cast<std::int16_t>(47 + row * 9), 84, 11};
}

RectI16 UiChainView::VuDamageRect(std::uint8_t side) {
  if (side >= 2U)
    return {};
  return {static_cast<std::int16_t>(219 + side * 9), kMeterTop, 7,
          kMeterHeight};
}

void UiChainView::RenderDelta(const UiChainViewData &previous,
                              const UiChainViewData &current,
                              const UiFrameScene &currentScene,
                              UiIndexedSurface &surface,
                              const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.number != current.number)
    render({56, 0, 40, 34});
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
    if (previous.phrases[row] != current.phrases[row] ||
        previous.transposes[row] != current.transposes[row]) {
      render(RowDamageRect(row));
    }
  }
  for (std::uint8_t track = 0; track < 8U; ++track) {
    if (previous.trackNotes[track] != current.trackNotes[track]) {
      render({static_cast<std::int16_t>(track * 30), 208, 30, 32});
    }
  }
  for (std::uint8_t side = 0; side < 2U; ++side) {
    if (previous.vuLevelTop[side] != current.vuLevelTop[side]) {
      render(VuDamageRect(side));
    }
  }
}

UiBuildStatus UiChainView::Build(const UiChainViewData &data,
                                 UiPalette &palette, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;

  const UiTopBarModel top{
      .title = "CHAIN",
      .meta = data.number,
      .power = data.power,
  };
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;
  UiBottomBarModel bottom{.kind = UiBottomBarKind::TrackNotes};
  bottom.trackNotes.notes = data.trackNotes;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  if (!UiVuGradient::Configure(palette, kMeterHeight)) {
    return UiBuildStatus::CommandOverflow;
  }
  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text("PH", 34, 39, UiColorToken::TextColored);
  builder.Text("TR", 69, 39, UiColorToken::TextDim);
  const RectI16 cursor = ResolvedCursorRect(data);
  for (std::uint8_t row = 0; row < 16U; ++row) {
    const std::int16_t y = static_cast<std::int16_t>(49 + row * 9);
    const auto rowText = HexByte(row);
    builder.Text(rowText.data(), 8, y,
                 row == data.editRow ? UiColorToken::TextColored
                                     : UiColorToken::DerivedTextFaint);
    const auto phrase = HexByte(data.phrases[row]);
    const char *phraseText = data.phrases[row] == 0xFFU ? "--" : phrase.data();
    builder.Text(phraseText, 33, y,
                 data.phrases[row] == 0xFFU ? UiColorToken::DerivedTextFaint
                                            : UiColorToken::TextNormal);
    const auto transpose = HexByte(data.transposes[row]);
    const char *transposeText =
        data.transposes[row] == 0xFFU ? "--" : transpose.data();
    builder.Text(transposeText, 68, y,
                 data.transposes[row] == 0xFFU ? UiColorToken::DerivedTextFaint
                                               : UiColorToken::TextDim);
  }
  builder.Selection(cursor);
  if (data.cursorInkVisible && data.editRow < 16U) {
    const auto phrase = HexByte(data.phrases[data.editRow]);
    const char *display =
        data.phrases[data.editRow] == 0xFFU ? "--" : phrase.data();
    builder.Text(display, 33, static_cast<std::int16_t>(49 + data.editRow * 9),
                 UiColorToken::TextHighlighted);
  }
  for (std::uint8_t side = 0; side < 2U; ++side) {
    const RectI16 meter = VuDamageRect(side);
    builder.Fill(meter, UiColorToken::DerivedVuTrack);
    const std::uint8_t level =
        std::min<std::uint8_t>(data.vuLevelTop[side], kMeterHeight);
    builder.VerticalPaletteRamp(
        {meter.x, static_cast<std::int16_t>(meter.y + level), meter.width,
         static_cast<std::int16_t>(meter.height - level)},
        UiVuGradient::IndexAt(level));
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
