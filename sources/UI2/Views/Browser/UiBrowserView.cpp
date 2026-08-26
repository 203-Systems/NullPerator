/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Browser/UiBrowserView.h"

#include "UI2/Render/UiFrameRenderer.h"

#include <algorithm>
#include <limits>

namespace ui2 {
namespace {

constexpr std::int16_t kFirstRowTextY = 45;
constexpr std::int16_t kRowStep = 11;
constexpr std::int16_t kListTop = 43;
constexpr std::int16_t kListHeight =
    static_cast<std::int16_t>(kUiBrowserVisibleRowCapacity * kRowStep);
constexpr RectI16 kListDamageRect{5, 40, 233, 148};
constexpr RectI16 kScrollTrackRect{236, kListTop, 1, kListHeight};
constexpr std::size_t kMaximumItemCharacters = 35;

std::int16_t ItemTextX(std::string_view item) {
  // Keep the parent affordance in the directory-name column without putting
  // brackets back into its actual label.
  return item == ".." ? 27 : 21;
}

std::uint8_t VisibleItemCount(const UiBrowserViewData &data) {
  return std::min<std::uint8_t>(
      data.visibleItemCount,
      static_cast<std::uint8_t>(kUiBrowserVisibleRowCapacity));
}

std::uint16_t TotalItemCount(const UiBrowserViewData &data) {
  const std::uint16_t suppliedWindowEnd = static_cast<std::uint16_t>(
      std::min<std::uint32_t>(
          std::numeric_limits<std::uint16_t>::max(),
          static_cast<std::uint32_t>(data.topIndex) + VisibleItemCount(data)));
  return std::max(data.totalItemCount, suppliedWindowEnd);
}

bool HasSelectedRow(const UiBrowserViewData &data) {
  return data.selectedRow < VisibleItemCount(data);
}

RectI16 ResolvedCursorRect(const UiBrowserViewData &data) {
  if (!HasSelectedRow(data))
    return {};
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiBrowserView::CursorTargetRect(data.selectedRow);
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

RectI16 UiBrowserView::ScrollThumbRect(const UiBrowserViewData &data) {
  const std::uint16_t total = TotalItemCount(data);
  constexpr std::uint16_t visible =
      static_cast<std::uint16_t>(kUiBrowserVisibleRowCapacity);
  if (total <= visible)
    return {};

  const std::int16_t thumbHeight = static_cast<std::int16_t>(std::max<int>(
      5, static_cast<int>(kListHeight) * visible / total));
  const std::uint16_t maximumTop = static_cast<std::uint16_t>(total - visible);
  const std::uint16_t top = std::min(data.topIndex, maximumTop);
  const std::int16_t travel =
      static_cast<std::int16_t>(kListHeight - thumbHeight);
  const std::int16_t thumbY = static_cast<std::int16_t>(
      kListTop + static_cast<std::int32_t>(travel) * top / maximumTop);
  return {235, thumbY, 2, thumbHeight};
}

void UiBrowserView::RenderDelta(const UiBrowserViewData &previous,
                                const UiBrowserViewData &current,
                                const UiFrameScene &currentScene,
                                UiIndexedSurface &surface,
                                const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.title != current.title || previous.meta != current.meta) {
    render({0, 0, 184, 34});
  }
  if (previous.power != current.power)
    render({184, 0, 56, 34});
  const bool listChanged =
      previous.items != current.items ||
      previous.visibleItemCount != current.visibleItemCount ||
      previous.topIndex != current.topIndex ||
      previous.totalItemCount != current.totalItemCount;
  if (listChanged) {
    render(kListDamageRect);
  } else if (previous.selectedRow != current.selectedRow ||
             ResolvedCursorRect(previous) != ResolvedCursorRect(current) ||
             previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(ResolvedCursorRect(previous)));
    render(ExpandedCursorDamage(ResolvedCursorRect(current)));
  }
  if (previous.footer != current.footer)
    render({5, 190, 230, 12});
  if (previous.actions != current.actions ||
      previous.actionCount != current.actionCount ||
      previous.activeAction != current.activeAction) {
    render({0, 208, 240, 32});
  }
}

UiBuildStatus UiBrowserView::Build(const UiBrowserViewData &data, UiPalette &,
                                   UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{
      .title = data.title, .meta = data.meta, .power = data.power,
      .metaUserData = true};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;

  UiBottomBarModel bottom{.kind = UiBottomBarKind::Actions};
  bottom.actions.actions = {
      data.actions[0], data.actions[1], data.actions[2], {}};
  bottom.actions.count = std::min<std::uint8_t>(
      data.actionCount, static_cast<std::uint8_t>(data.actions.size()));
  bottom.actions.active =
      bottom.actions.count == 0U
          ? 0U
          : std::min<std::uint8_t>(
                data.activeAction,
                static_cast<std::uint8_t>(bottom.actions.count - 1U));
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  const std::uint8_t visibleCount = VisibleItemCount(data);
  for (std::uint8_t row = 0; row < visibleCount; ++row) {
    builder.UserText(data.items[row].substr(0, kMaximumItemCharacters),
                     ItemTextX(data.items[row]),
                     static_cast<std::int16_t>(kFirstRowTextY + row * kRowStep),
                     UiColorToken::TextNormal);
  }

  if (TotalItemCount(data) > kUiBrowserVisibleRowCapacity) {
    builder.Fill(kScrollTrackRect, UiColorToken::DerivedTextFaint);
    builder.Fill(ScrollThumbRect(data), UiColorToken::TextColored);
  }

  const RectI16 cursor = ResolvedCursorRect(data);
  if (!cursor.Empty()) {
    builder.Selection(cursor);
  }
  if (HasSelectedRow(data) && data.cursorInkVisible) {
    const std::int16_t selectedY = static_cast<std::int16_t>(
        kFirstRowTextY + data.selectedRow * kRowStep);
    builder.Text(">", 10, selectedY, UiColorToken::TextHighlighted);
    builder.UserText(
        data.items[data.selectedRow].substr(0, kMaximumItemCharacters),
        ItemTextX(data.items[data.selectedRow]), selectedY,
        UiColorToken::TextHighlighted);
  }
  builder.Text(data.footer, 9, 192, UiColorToken::DerivedTextFaint);
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
