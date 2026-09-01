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
  case UiDialogAction::Replace:
    return "REPLACE";
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

template <std::size_t Width, std::size_t Height>
constexpr auto PackPixelMask(
    const std::array<std::string_view, Height> &rows) {
  std::array<std::uint8_t, (Width * Height + 7U) / 8U> packed{};
  for (std::size_t y = 0; y < Height; ++y) {
    for (std::size_t x = 0; x < Width; ++x) {
      if (x < rows[y].size() && rows[y][x] == '1') {
        const std::size_t bit = y * Width + x;
        packed[bit / 8U] = static_cast<std::uint8_t>(
            packed[bit / 8U] | (1U << (bit % 8U)));
      }
    }
  }
  return packed;
}

constexpr auto kShiftSolid = PackPixelMask<11>(std::array<std::string_view, 11>{
    "00000100000", "00001110000", "00011111000", "00111111100",
    "01111111110", "11111111111", "00011111000", "00011111000",
    "00011111000", "00011111000", "00011111000"});
constexpr auto kShiftOutline =
    PackPixelMask<11>(std::array<std::string_view, 11>{
        "00000100000", "00001010000", "00010001000", "00100000100",
        "01000000010", "11100000111", "00010001000", "00010001000",
        "00010001000", "00010001000", "00011111000"});
constexpr auto kSpaceIcon = PackPixelMask<21>(std::array<std::string_view, 5>{
    "100000000000000000001", "100000000000000000001",
    "100000000000000000001", "100000000000000000001",
    "111111111111111111111"});
constexpr auto kEraseOutline =
    PackPixelMask<17>(std::array<std::string_view, 9>{
        "00001111111111111", "00010000000000001", "00100000000000001",
        "01000000000000001", "10000000000000001", "01000000000000001",
        "00100000000000001", "00010000000000001", "00001111111111111"});
constexpr auto kEraseX = PackPixelMask<5>(std::array<std::string_view, 5>{
    "10001", "01010", "00100", "01010", "10001"});

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
                   UiSceneBuilder<80, 256> &builder, std::int16_t y,
                   bool retainSelectedAccent = false) {
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
    const bool accented = retainSelectedAccent &&
                          index == data.selectedAction;
    builder.CenteredText(
        label, center, y,
        selected   ? UiColorToken::TextHighlighted
        : accented ? UiColorToken::TextColored
                   : UiColorToken::TextDim);
  }
}

void RenderCenteredLabel(UiSceneBuilder<80, 256> &builder,
                         const UiDialogViewData &data, std::int16_t y,
                         UiColorToken color) {
  if (data.labelUserText)
    builder.CenteredUserText(data.label, 120, y, color);
  else
    builder.CenteredText(data.label, 120, y, color);
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
  builder.UserText(data.value, 14, 57,
                   inputInk ? UiColorToken::TextHighlighted
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
      // Keyboard case is interaction state, not a presentation preference.
      // Preserve the selected glyph even when the global font mode requests
      // all-upper or all-lower UI labels.
      builder.LiteralText(std::string_view(&key, 1), x, row.y,
                          selectedInk ? UiColorToken::TextHighlighted
                                      : UiColorToken::TextDim);
      ++keyIndex;
    }
  }

  constexpr std::array<std::string_view, 5> specialLabels{"", "-", "", ".",
                                                          ""};
  for (std::uint8_t index = 0; index < specialLabels.size(); ++index) {
    const bool selected = data.focus == UiDialogFocus::Keyboard &&
                          data.selectedKey == keyIndex;
    const bool selectedInk =
        selected && (!data.cursorVisualOverride || data.cursorInkVisible);
    if (selected && !data.cursorVisualOverride)
      builder.Selection(kRenameSpecialKeys[index]);
    const std::int16_t center = static_cast<std::int16_t>(
        kRenameSpecialKeys[index].x + kRenameSpecialKeys[index].width / 2);
    const UiColorToken iconColor =
        selectedInk ? UiColorToken::TextHighlighted : UiColorToken::TextDim;
    if (index == 0U) {
      const UiColorToken shiftColor =
          selectedInk    ? UiColorToken::TextHighlighted
          : data.uppercase ? UiColorToken::TextColored
                           : UiColorToken::TextDim;
      builder.PixelMask(
          {21, 169, 11, 11},
          data.uppercase ? std::span{kShiftSolid} : std::span{kShiftOutline},
          shiftColor);
    } else if (index == 2U) {
      builder.PixelMask({110, 173, 21, 5}, kSpaceIcon, iconColor);
    } else if (index == 4U) {
      builder.PixelMask({205, 169, 17, 9}, kEraseOutline, iconColor);
      builder.PixelMask({214, 171, 5, 5}, kEraseX, iconColor);
    } else {
      builder.CenteredText(specialLabels[index], center, 171,
                           selectedInk ? UiColorToken::TextHighlighted
                                       : UiColorToken::TextDim);
    }
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
    const bool save = data.actions[index] == UiDialogAction::Save;
    const bool enabled = !save || data.saveEnabled;
    const UiColorToken color =
        !enabled ? UiColorToken::TextDim
        : selected ? UiColorToken::TextColored
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
  if (kind == UiDialogKind::Feedback)
    return {12, 184, 216, 18};
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
  // Bottom bars express focus through colored text. The rounded content cursor
  // is reserved for the input and keyboard regions above the bar.
  return {};
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
      RenderCenteredLabel(builder, data, 122, UiColorToken::TextDim);
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

  if (data.kind == UiDialogKind::Feedback) {
    // Feedback is an overlay, not a modal. Keep both bars and the complete
    // base scene intact so page input and live updates continue normally.
    scene.overlay.Clear();
    UiSceneBuilder<80, 256> builder(scene.overlay);
    const UiColorToken accent = data.tone == UiDialogTone::Error
                                    ? UiColorToken::SystemError
                                    : UiColorToken::TextColored;
    builder.Fill({12, 184, 216, 18}, accent);
    builder.Fill({14, 186, 212, 14}, UiColorToken::SurfaceBackground);
    builder.CenteredText(data.title, 120, 189, accent);
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
      RenderCenteredLabel(builder, data, 105, UiColorToken::TextDim);
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
    builder.UserText(data.value, valueX, 107,
                     data.actionsFocused ? UiColorToken::TextNormal
                                         : UiColorToken::TextHighlighted);
    RenderActions(data, builder, 139, true);
    break;
  }
  case UiDialogKind::RenderProgress:
    // Render Progress has one status line plus elapsed/progress. Legacy
    // diagnostic snapshots may carry both a generic title and a useful label;
    // the label wins instead of silently expanding the approved two-line UI.
    builder.CenteredText(data.label.empty() ? data.title : data.label, 120, 91,
                         UiColorToken::SystemWarning);
    builder.CenteredText(data.elapsed, 120, 108,
                         UiColorToken::SystemWarning);
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
  case UiDialogKind::Feedback:
    break;
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
