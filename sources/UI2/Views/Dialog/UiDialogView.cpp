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
  case UiDialogAction::Save:
    return "SAVE";
  case UiDialogAction::Random:
    return "RANDOM";
  }
  return {};
}

struct KeyboardRow {
  std::string_view keys;
  std::int16_t y;
  std::int16_t start;
  std::int16_t step;
};

constexpr std::array<KeyboardRow, 4> kRenameKeyboard{{
    {"1234567890", 83, 12, 23},
    {"QWERTYUIOP", 104, 12, 23},
    {"ASDFGHJKL", 125, 23, 24},
    {"ZXCVBNM", 146, 43, 26},
}};

constexpr std::array<std::int16_t, 3> kRenameActionCenters{40, 120, 197};
constexpr std::array<RectI16, 5> kRenameSpecialKeys{{
    {9, 168, 35, 13},
    {51, 168, 25, 13},
    {83, 168, 74, 13},
    {164, 168, 25, 13},
    {196, 168, 35, 13},
}};

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
                   UiSceneBuilder<80, 256> &builder, std::int16_t y) {
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

void RenderRename(const UiDialogViewData &data,
                  UiSceneBuilder<80, 256> &builder) {
  builder.Fill(RectI16::Screen(), UiColorToken::SurfaceBackground);
  builder.Fill({0, 0, 240, 36}, UiColorToken::SurfaceTopBar);
  builder.CenteredText("RENAME", 120, 9, UiColorToken::TextNormal, 2);
  builder.Text(data.label.empty() ? std::string_view{"NAME"} : data.label, 9,
               43, UiColorToken::TextColored);
  builder.RowHighlight({9, 53, 222, 15});

  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty())
    builder.Selection(data.cursorVisualRect);

  const bool inputSelected = data.focus == UiDialogFocus::Input;
  const bool inputInk = inputSelected &&
                        (!data.cursorVisualOverride || data.cursorInkVisible);
  if (inputSelected && !data.cursorVisualOverride) {
    builder.Selection({9, 53, 222, 15});
  }
  builder.Text(data.value, 14, 57,
               inputInk
                   ? UiColorToken::TextHighlighted
                   : UiColorToken::TextNormal);
  builder.Fill({9, 74, 222, 1}, UiColorToken::CursorRow);

  std::uint8_t keyIndex = 0;
  for (const KeyboardRow &row : kRenameKeyboard) {
    for (std::size_t column = 0; column < row.keys.size(); ++column) {
      char key = row.keys[column];
      if (!data.uppercase && key >= 'A' && key <= 'Z')
        key = static_cast<char>(key + ('a' - 'A'));
      const std::int16_t x = static_cast<std::int16_t>(
          row.start + static_cast<std::int16_t>(column) * row.step);
      const bool selected = data.focus == UiDialogFocus::Keyboard &&
                            keyIndex == data.selectedKey;
      const bool selectedInk =
          selected && (!data.cursorVisualOverride || data.cursorInkVisible);
      if (selected && !data.cursorVisualOverride) {
        builder.Selection({static_cast<std::int16_t>(x - 4),
                           static_cast<std::int16_t>(row.y - 2),
                           13, 11});
      }
      builder.Text(std::string_view(&key, 1), x, row.y,
                   selectedInk ? UiColorToken::TextHighlighted
                               : UiColorToken::TextDim);
      ++keyIndex;
    }
  }

  const std::array<std::string_view, 5> specialLabels{
      data.uppercase ? std::string_view{"ABC"} : std::string_view{"abc"},
      "-", "SPACE", ".", "DEL"};
  for (std::uint8_t index = 0; index < specialLabels.size(); ++index) {
    const bool selected = data.focus == UiDialogFocus::Keyboard &&
                          data.selectedKey == keyIndex;
    const bool selectedInk =
        selected && (!data.cursorVisualOverride || data.cursorInkVisible);
    if (selected && !data.cursorVisualOverride)
      builder.Selection(kRenameSpecialKeys[index]);
    builder.CenteredText(specialLabels[index],
                         static_cast<std::int16_t>(
                             kRenameSpecialKeys[index].x +
                             kRenameSpecialKeys[index].width / 2),
                         171, selectedInk ? UiColorToken::TextHighlighted
                                          : UiColorToken::TextDim);
    ++keyIndex;
  }

  builder.Fill({0, 200, 240, 40}, UiColorToken::SurfaceBottomBar);
  builder.Fill({0, 200, 240, 1}, UiColorToken::CursorRow);
  const std::uint8_t actionCount = static_cast<std::uint8_t>(
      std::min<std::size_t>(3U, std::min<std::size_t>(data.actionCount,
                                                      data.actions.size())));
  for (std::uint8_t index = 0; index < actionCount; ++index) {
    const std::string_view label = ActionLabel(data.actions[index]);
    const std::int16_t center = kRenameActionCenters[index];
    const bool selected = data.focus == UiDialogFocus::Actions &&
                          index == data.selectedAction;
    const bool selectedInk =
        selected && (!data.cursorVisualOverride || data.cursorInkVisible);
    if (selected && !data.cursorVisualOverride) {
      const std::int16_t width = UiFont5x7::TextWidth(label.size());
      builder.Selection({static_cast<std::int16_t>(
                             center - (width + 1) / 2 - 4),
                         216,
                         static_cast<std::int16_t>(width + 8), 11});
    }
    const bool save = data.actions[index] == UiDialogAction::Save;
    const bool enabled = !save || data.saveEnabled;
    const UiColorToken color =
        !enabled      ? UiColorToken::TextDim
        : selectedInk ? UiColorToken::TextHighlighted
        : save        ? UiColorToken::TextColored
                      : UiColorToken::TextDim;
    builder.CenteredText(label, center, 218, color);
  }
}

} // namespace

RectI16 UiDialogView::DamageRect(UiDialogKind kind) {
  if (kind == UiDialogKind::FullScreen)
    return RectI16::Screen();
  if (kind == UiDialogKind::Rename)
    return RectI16::Screen();
  return {27, 71, 186, 94};
}

RectI16 UiDialogView::CursorTargetRect(const UiDialogViewData &data) {
  if (data.kind != UiDialogKind::Rename)
    return {};
  if (data.focus == UiDialogFocus::Input)
    return {9, 53, 222, 15};
  if (data.focus == UiDialogFocus::Keyboard) {
    std::uint8_t keyIndex = 0U;
    for (const KeyboardRow &row : kRenameKeyboard) {
      for (std::size_t column = 0; column < row.keys.size(); ++column) {
        if (keyIndex == data.selectedKey) {
          const std::int16_t x = static_cast<std::int16_t>(
              row.start + static_cast<std::int16_t>(column) * row.step);
          return {static_cast<std::int16_t>(x - 4),
                  static_cast<std::int16_t>(row.y - 2), 13, 11};
        }
        ++keyIndex;
      }
    }
    const std::uint8_t specialIndex = static_cast<std::uint8_t>(
        data.selectedKey - keyIndex);
    return specialIndex < kRenameSpecialKeys.size()
               ? kRenameSpecialKeys[specialIndex]
               : RectI16{};
  }
  if (data.selectedAction >= data.actionCount ||
      data.selectedAction >= kRenameActionCenters.size())
    return {};
  const std::string_view label = ActionLabel(data.actions[data.selectedAction]);
  const std::int16_t width = UiFont5x7::TextWidth(label.size());
  const std::int16_t center = kRenameActionCenters[data.selectedAction];
  return {static_cast<std::int16_t>(center - (width + 1) / 2 - 4), 216,
          static_cast<std::int16_t>(width + 8), 11};
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
    UiSceneBuilder<80, 256> builder(scene.overlay);
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

  if (data.kind == UiDialogKind::Rename) {
    scene.Clear();
    scene.topHeight = 0;
    scene.bottomVisible = false;
    UiSceneBuilder<80, 256> builder(scene.overlay);
    RenderRename(data, builder);
    return builder.Ok() ? UiBuildStatus::Built
                        : UiBuildStatus::CommandOverflow;
  }

  scene.bottomVisible = false;
  scene.overlay.Clear();
  UiSceneBuilder<80, 256> builder(scene.overlay);

  builder.Fill({28, 72, 184, 92}, UiColorToken::TextNormal);
  builder.Fill({31, 75, 178, 86}, UiColorToken::SurfaceBackground);
  switch (data.kind) {
  case UiDialogKind::Message:
    if (data.label.empty()) {
      builder.CenteredText(data.title, 120, 94,
                           UiColorToken::TextNormal);
      RenderActions(data, builder, 132);
    } else {
      builder.CenteredText(data.title, 120, 88,
                           UiColorToken::TextNormal);
      builder.CenteredText(data.label, 120, 105,
                           UiColorToken::TextDim);
      RenderActions(data, builder, 139);
    }
    break;
  case UiDialogKind::TextInput: {
    builder.CenteredText(data.title, 120, 88,
                         UiColorToken::TextNormal);
    builder.Text(data.label, 42, 108,
                 UiColorToken::TextDim);
    const std::int16_t valueX = static_cast<std::int16_t>(
        42 + UiFont5x7::TextWidth(data.label.size()) + 15);
    const std::int16_t selectionX = static_cast<std::int16_t>(valueX - 4);
    if (!data.actionsFocused) {
      builder.Selection(
          {selectionX, 105,
           static_cast<std::int16_t>(192 - selectionX), 11});
    }
    builder.Text(data.value, valueX, 107,
                 data.actionsFocused ? UiColorToken::TextNormal
                                     : UiColorToken::TextHighlighted);
    RenderActions(data, builder, 139);
    break;
  }
  case UiDialogKind::RenderProgress:
    if (data.label.empty()) {
      builder.CenteredText(data.title, 120, 91,
                           UiColorToken::SystemWarning);
      builder.CenteredText(data.elapsed, 120, 108,
                           UiColorToken::SystemWarning);
    } else {
      builder.CenteredText(data.title, 120, 83,
                           UiColorToken::SystemWarning);
      builder.CenteredText(data.label, 120, 98,
                           UiColorToken::TextNormal);
      builder.CenteredText(data.elapsed, 120, 113,
                           UiColorToken::SystemWarning);
    }
    builder.Fill({48, 126, 144, 7},
                 UiColorToken::DerivedVuTrack);
    builder.Fill(
        {48, 126,
         static_cast<std::int16_t>(
             std::min<std::uint8_t>(data.progressWidth, 144)),
         7},
        UiColorToken::CursorPrimary);
    RenderActions(data, builder, 145);
    break;
  case UiDialogKind::FullScreen:
  case UiDialogKind::Rename:
    break;
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
