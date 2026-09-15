/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Foundation/Types/FxCommands.h"
#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Scene/UiSceneBuilder.h"
#include <algorithm>

namespace ui2 {
// Positions are in the scrolling content's coordinate space. Keep the cursor
// in that space too, so it travels with the labels and divider during a pan.
inline int FxSelectorCellX(const fx::List &list, std::size_t index) {
  return 9 + list.Column(index) * 52 + 12 * static_cast<int>(list[index].group);
}
inline fx::List FxSelectorList(std::string_view selected, fx::Context context,
                               std::string_view original) {
  return fx::BuildList(context, original.empty() ? selected : original);
}
inline RectI16 FxSelectorCursorRect(std::string_view selected,
                                    fx::Context context,
                                    std::string_view original = {}) {
  const auto list = FxSelectorList(selected, context, original);
  for (std::size_t i = 0; i < list.count; ++i)
    if (list[i].name == selected)
      return {static_cast<std::int16_t>(FxSelectorCellX(list, i) + 4),
              static_cast<std::int16_t>(59 + list.Row(i) * 23), 45, 17};
  return {};
}
inline int FxSelectorMaxScroll(const fx::List &list) {
  const int end = 9 + list.Columns() * 52 + 12 * (fx::groups.size() - 1);
  return std::max(0, end - 231);
}
inline std::int16_t FxSelectorScrollTarget(std::string_view selected,
                                           fx::Context context,
                                           std::string_view original,
                                           int previous) {
  const auto list = FxSelectorList(selected, context, original);
  const auto cursor = FxSelectorCursorRect(selected, context, original);
  int scroll = std::clamp(previous, 0, FxSelectorMaxScroll(list));
  if (cursor.x - scroll < 13)
    scroll = cursor.x - 13;
  else if (cursor.x + 48 - scroll > 231)
    scroll = cursor.x + 48 - 231;
  return static_cast<std::int16_t>(
      std::clamp(scroll, 0, FxSelectorMaxScroll(list)));
}
inline UiBuildStatus BuildFxSelector(
    std::string_view selected, fx::Context context, std::string_view original,
    const UiBottomBarModel &help, UiPowerState power, std::string_view elapsed,
    UiFrameScene &scene, RectI16 cursor = {}, bool cursorOverride = false,
    bool inkVisible = true, std::int16_t scroll = 0) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  auto status = UiChromeRenderer::BuildTop(
      {.title = "FX SELECT",
       .meta = fx::InstrumentName(context.instrument),
       .elapsed = elapsed,
       .power = power},
      scene.top);
  if (status != UiBuildStatus::Built)
    return status;
  const auto *entry = fx::Find(selected);
  const bool unsupported = entry != nullptr && !fx::Available(*entry, context);
  auto bottom = help;
  if (unsupported) {
    bottom = {};
    bottom.kind = UiBottomBarKind::Context;
    bottom.context.firstLineCount = bottom.context.secondLineCount = 1;
    bottom.context.firstLine[0] = {"Not Supported", UiColorToken::TextColored,
                                   9};
    bottom.context.secondLine[0] = {"On This Instrument",
                                    UiColorToken::TextNormal, 9};
  }
  status = UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (status != UiBuildStatus::Built)
    return status;

  const auto list = FxSelectorList(selected, context, original);
  UiSceneBuilder<256, 1024> builder(scene.content);
  const auto label = [&](std::string_view text, int start, int end) {
    const int x = std::max(13, start + 4);
    if (x + UiFont5x7::TextWidth(text.size()) <= std::min(232, end))
      builder.Text(text, static_cast<std::int16_t>(x), 42,
                   UiColorToken::TextColored);
  };
  for (const auto group : fx::groups) {
    const int index = static_cast<int>(group);
    const int start = 9 + list.FirstColumn(group) * 52 + index * 12 - scroll;
    const int end = start + list.GroupColumns(group) * 52;
    label(fx::GroupName(group), start, end);
    if (group != fx::Group::Standard)
      builder.Fill({static_cast<std::int16_t>(start - 7), 39, 1, 154},
                   UiColorToken::DerivedTextFaint);
  }
  auto selection = cursorOverride
                       ? cursor
                       : FxSelectorCursorRect(selected, context, original);
  selection.x -= scroll;
  if (!selection.Empty()) {
    if (unsupported) {
      const auto x = selection.x, y = selection.y;
      const auto right = static_cast<std::int16_t>(x + selection.width - 1);
      const auto bottom = static_cast<std::int16_t>(y + selection.height - 1);
      builder.Fill({x, y, selection.width, 1}, UiColorToken::CursorPrimary);
      builder.Fill({x, bottom, selection.width, 1},
                   UiColorToken::CursorPrimary);
      builder.Fill({x, y, 1, selection.height}, UiColorToken::CursorPrimary);
      builder.Fill({right, y, 1, selection.height},
                   UiColorToken::CursorPrimary);
    } else {
      builder.Selection(selection);
    }
  }
  for (std::size_t i = 0; i < list.count; ++i) {
    const auto x = static_cast<std::int16_t>(FxSelectorCellX(list, i) - scroll);
    const auto y = static_cast<std::int16_t>(64 + list.Row(i) * 23);
    const bool active = list[i].name == selected;
    builder.CenteredText(list[i].name, x + 26, y,
                         !fx::Available(list[i], context)
                             ? UiColorToken::DerivedTextFaint
                         : active && inkVisible ? UiColorToken::TextHighlighted
                                                : UiColorToken::TextNormal);
  }
  // The content rasterizer clips to the screen. These fixed gutters narrow
  // the horizontal viewport without truncating glyphs in partially seen cells.
  builder.Fill({0, 34, 8, 174}, UiColorToken::SurfaceBackground);
  builder.Fill({232, 34, 8, 174}, UiColorToken::SurfaceBackground);
  const int maximum = FxSelectorMaxScroll(list);
  if (maximum > 0) {
    const int thumb = 222 * 222 / (222 + maximum);
    builder.Fill({9, 199, 222, 2}, UiColorToken::DerivedTextFaint);
    builder.Fill(
        {static_cast<std::int16_t>(9 + scroll * (222 - thumb) / maximum), 199,
         static_cast<std::int16_t>(thumb), 2},
        UiColorToken::TextColored);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}
} // namespace ui2
