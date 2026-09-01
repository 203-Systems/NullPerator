/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Chrome/UiChromeRenderer.h"

#include "Application/UI2/Ui2FixedText.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>

namespace ui2 {
namespace {

using BarBuilder = UiSceneBuilder<64, 256>;

void DrawSegments(BarBuilder &builder,
                  const std::array<UiColoredText, 3> &segments,
                  std::uint8_t count, std::int16_t y) {
  std::int16_t x = 9;
  const std::size_t safeCount =
      std::min<std::size_t>(count, segments.size());
  for (std::size_t index = 0; index < safeCount; ++index) {
    const std::int16_t segmentX =
        segments[index].x >= 0 ? segments[index].x : x;
    if (segments[index].userData)
      builder.UserText(segments[index].text, segmentX, y,
                       segments[index].color);
    else
      builder.Text(segments[index].text, segmentX, y,
                   segments[index].color);
    x = static_cast<std::int16_t>(
        segmentX + UiFont5x7::TextWidth(segments[index].text.size()) + 6);
  }
}

void DrawVerticalArrow(BarBuilder &builder, std::int16_t x, std::int16_t y,
                       bool up) {
  for (std::int16_t row = 0; row < 3; ++row) {
    const std::int16_t inset =
        up ? static_cast<std::int16_t>(2 - row) : row;
    builder.Fill({static_cast<std::int16_t>(x + inset),
                  static_cast<std::int16_t>(y + row), 1, 1},
                 UiColorToken::DerivedTextFaint);
    if (inset != 2) {
      builder.Fill({static_cast<std::int16_t>(x + 4 - inset),
                    static_cast<std::int16_t>(y + row), 1, 1},
                   UiColorToken::DerivedTextFaint);
    }
  }
}

void DrawPlusMinus(BarBuilder &builder, std::int16_t x, std::int16_t y) {
  builder.Fill({static_cast<std::int16_t>(x + 2), y, 1, 5},
               UiColorToken::TextColored);
  builder.Fill({x, static_cast<std::int16_t>(y + 2), 5, 1},
               UiColorToken::TextColored);
  builder.Fill({x, static_cast<std::int16_t>(y + 6), 5, 1},
               UiColorToken::TextColored);
}

} // namespace

void UiChromeRenderer::DrawPower(const UiTopBarModel &model,
                                 BarBuilder &builder) {
  UiColorToken color = UiColorToken::BatteryNormal;
  if (model.power == UiPowerState::Charging) {
    color = UiColorToken::BatteryCharging;
  } else if (model.power == UiPowerState::BatteryLow) {
    color = UiColorToken::BatteryLow;
  }
  const std::int16_t x = 207;
  const std::int16_t y = 12;
  const std::int16_t fillWidth = BatteryFillWidth(model);
  builder.Fill({x, y, 20, 1}, color);
  builder.Fill({x, static_cast<std::int16_t>(y + 9), 20, 1}, color);
  builder.Fill({x, static_cast<std::int16_t>(y + 1), 1, 8}, color);
  builder.Fill({static_cast<std::int16_t>(x + 19),
                static_cast<std::int16_t>(y + 1), 1, 8},
               color);
  builder.Fill({static_cast<std::int16_t>(x + 20),
                static_cast<std::int16_t>(y + 3), 3, 4},
               color);
  builder.Fill({static_cast<std::int16_t>(x + 2),
                static_cast<std::int16_t>(y + 2), fillWidth, 6},
               color);
}

void UiChromeRenderer::DrawSaving(const UiTopBarModel &model,
                                  BarBuilder &builder) {
  constexpr std::int16_t kX = 207;
  constexpr std::int16_t kY = 12;
  constexpr std::uint8_t kBarCount = 4;
  const std::uint8_t phase = static_cast<std::uint8_t>(model.power) -
                             static_cast<std::uint8_t>(UiPowerState::Saving);
  for (std::uint8_t index = 0; index < kBarCount; ++index) {
    const std::uint8_t distance =
        static_cast<std::uint8_t>((index + kBarCount - phase) % kBarCount);
    const UiColorToken color =
        distance == 0U ? UiColorToken::TextColored
                       : (distance == 1U ? UiColorToken::TextNormal
                                         : UiColorToken::DerivedTextFaint);
    builder.Fill({static_cast<std::int16_t>(kX + index * 5), kY, 3, 10},
                 color);
  }
}

std::int16_t UiChromeRenderer::BatteryFillWidth(const UiTopBarModel &model) {
  if (model.showBatteryPercent) {
    const std::uint16_t percentage =
        std::min<std::uint16_t>(model.batteryPercent, 100U);
    if (percentage == 0U)
      return 0;
    // The icon has sixteen interior columns. Round upward so a non-empty
    // battery always retains at least one visible column.
    return static_cast<std::int16_t>(
        std::min<std::uint16_t>(16U, (percentage * 16U + 99U) / 100U));
  }
  // Fixtures and platforms without a percentage retain the established
  // coarse presentation. Charging changes color only; it must not imply full.
  if (model.power == UiPowerState::BatteryHigh)
    return 16;
  if (model.power == UiPowerState::BatteryLow)
    return 4;
  return 11;
}

UiBuildStatus UiChromeRenderer::BuildTop(const UiTopBarModel &model,
                                         UiBarScene &scene,
                                         std::optional<RectI16> navHighlight) {
  scene.Clear();
  BarBuilder builder(scene);
  const std::uint8_t titleScale = model.title.size() <= 7 ? 2 : 1;
  builder.Text(model.title, 9, 10, UiColorToken::TextNormal, titleScale);
  if (!model.meta.empty()) {
    const std::int16_t metaX =
        model.metaX >= 0
            ? model.metaX
            : static_cast<std::int16_t>(
                  9 + UiFont5x7::TextWidth(model.title.size(), titleScale) + 7);
    if (model.metaUserData)
      builder.UserText(model.meta, metaX, 10, UiColorToken::TextColored);
    else
      builder.Text(model.meta, metaX, 10, UiColorToken::TextColored);
    if (model.metaSelected) {
      const RectI16 selection =
          model.metaSelectionOverride && !model.metaSelectionRect.Empty()
              ? model.metaSelectionRect
              : MetaTargetRect(model);
      builder.Selection(selection);
      if (model.metaInkVisible) {
        if (model.metaUserData)
          builder.UserText(model.meta, metaX, 10,
                           UiColorToken::TextHighlighted);
        else
          builder.Text(model.meta, metaX, 10,
                       UiColorToken::TextHighlighted);
      }
    }
  }

  if (model.power == UiPowerState::Playing) {
    builder.Text(">", 193, 14, UiColorToken::PlaybackActive);
    constexpr std::int16_t kPlayingTextRight = 230;
    const std::int16_t elapsedX = static_cast<std::int16_t>(
        kPlayingTextRight - UiFont5x7::TextWidth(model.elapsed.size()));
    builder.Text(model.elapsed, elapsedX, 14, UiColorToken::TextNormal);
  } else if (IsSavingPowerState(model.power)) {
    DrawSaving(model, builder);
  } else if (model.power == UiPowerState::Navigation) {
    const UiNavMapModel map =
        model.navMapOverride ? model.navMap : NavigationMap(model.navTarget);
    const std::optional<RectI16> animatedHighlight =
        model.navCursor.selectionOverride
            ? std::optional<RectI16>{model.navCursor.selectionRect}
            : std::nullopt;
    const RectI16 selection = navHighlight.value_or(
        animatedHighlight.value_or(NavTargetRect(model.navTarget)));
    if (map.visible != 0U && !selection.Empty()) builder.Selection(selection);
    const auto navColor = [&](UiNavTarget target) {
      return model.navTarget == target && model.navCursor.inkVisible
                 ? UiColorToken::TextHighlighted
                 : UiColorToken::TextNormal;
    };
    if (map.Contains(UiNavTarget::Project)) {
      builder.CenteredText("P", 204, 3, navColor(UiNavTarget::Project));
    }
    if (map.Contains(UiNavTarget::Mixer)) {
      builder.CenteredText("M", 204, 24, navColor(UiNavTarget::Mixer));
    }
    if (map.Contains(UiNavTarget::Groove)) {
      builder.CenteredText("G", 220, 3, navColor(UiNavTarget::Groove));
    }
    if (map.Contains(UiNavTarget::PhraseTable)) {
      builder.CenteredText("T", 220, 24, navColor(UiNavTarget::PhraseTable));
    }
    if (map.Contains(UiNavTarget::InstrumentTable)) {
      builder.CenteredText("T", 228, 24,
                           navColor(UiNavTarget::InstrumentTable));
    }
    if (map.Contains(UiNavTarget::Song))
      builder.Text("S", 202, 13, navColor(UiNavTarget::Song));
    if (map.Contains(UiNavTarget::Chain))
      builder.Text("C", 210, 13, navColor(UiNavTarget::Chain));
    if (map.Contains(UiNavTarget::Phrase))
      builder.Text("P", 218, 13, navColor(UiNavTarget::Phrase));
    if (map.Contains(UiNavTarget::Instrument))
      builder.Text("I", 226, 13, navColor(UiNavTarget::Instrument));
  } else {
    if (model.showBatteryPercent) {
      std::array<char, 5> percent{};
      FormatUiPercent100(model.batteryPercent, percent);
      builder.Text(percent.data(), 184, 14, UiColorToken::TextNormal);
    }
    DrawPower(model, builder);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

UiNavMapModel UiChromeRenderer::NavigationMap(UiNavTarget target) {
  const auto map = [](std::initializer_list<UiNavTarget> targets) {
    UiNavMapModel result{};
    for (const UiNavTarget item : targets)
      result.visible |= UiNavTargetBit(item);
    return result;
  };

  if (target == UiNavTarget::None)
    return {};

  // S/C/P/I is the permanent horizontal spine. The active cursor's column is
  // then completed vertically, so Mixer for example reads P / SCPI / M
  // without hiding the rest of the horizontal navigation context.
  UiNavMapModel result = map({UiNavTarget::Song, UiNavTarget::Chain,
                              UiNavTarget::Phrase,
                              UiNavTarget::Instrument});
  const auto add = [&](UiNavTarget item) {
    result.visible |= UiNavTargetBit(item);
  };
  switch (target) {
  case UiNavTarget::Project:
  case UiNavTarget::Song:
  case UiNavTarget::Mixer:
    add(UiNavTarget::Project);
    add(UiNavTarget::Mixer);
    break;
  case UiNavTarget::Chain:
    break;
  case UiNavTarget::Groove:
  case UiNavTarget::Phrase:
  case UiNavTarget::PhraseTable:
    add(UiNavTarget::Groove);
    add(UiNavTarget::PhraseTable);
    break;
  case UiNavTarget::Instrument:
  case UiNavTarget::InstrumentTable:
    add(UiNavTarget::InstrumentTable);
    break;
  case UiNavTarget::None:
    break;
  }
  return result;
}

RectI16 UiChromeRenderer::NavTargetRect(UiNavTarget target) {
  switch (target) {
  case UiNavTarget::Project:
    return {201, 2, 7, 9};
  case UiNavTarget::Song:
    return {201, 12, 7, 9};
  case UiNavTarget::Chain:
    return {209, 12, 7, 9};
  case UiNavTarget::Phrase:
    return {217, 12, 7, 9};
  case UiNavTarget::Instrument:
    return {225, 12, 7, 9};
  case UiNavTarget::Mixer:
    return {201, 23, 7, 9};
  case UiNavTarget::Groove:
    return {217, 2, 7, 9};
  case UiNavTarget::PhraseTable:
    return {217, 23, 7, 9};
  case UiNavTarget::InstrumentTable:
    return {225, 23, 7, 9};
  case UiNavTarget::None:
    return {};
  }
  return {};
}

RectI16 UiChromeRenderer::MetaTargetRect(const UiTopBarModel &model) {
  if (model.meta.empty())
    return {};
  const std::int16_t metaX =
      model.metaX >= 0
          ? model.metaX
          : static_cast<std::int16_t>(
                9 +
                UiFont5x7::TextWidth(model.title.size(),
                                     model.title.size() <= 7 ? 2 : 1) +
                7);
  return {
      static_cast<std::int16_t>(metaX - 2), 9,
      static_cast<std::int16_t>(UiFont5x7::TextWidth(model.meta.size()) + 4),
      9};
}

RectI16 UiChromeRenderer::BottomTrackTargetRect(std::int8_t track) {
  if (track < 0 || track >= 8)
    return {};
  const std::int16_t center = static_cast<std::int16_t>(15 + track * 30);
  return {static_cast<std::int16_t>(center - 7), 212, 15, 8};
}

RectI16 UiChromeRenderer::BottomRgbTargetRect(std::uint8_t component,
                                              std::uint8_t value) {
  if (component >= 3U)
    return {};
  constexpr std::array<std::int16_t, 3> centers{42, 120, 198};
  std::array<char, 4> text{};
  const int length = std::snprintf(text.data(), text.size(), "%u",
                                   static_cast<unsigned>(value));
  const std::int16_t width = UiFont5x7::TextWidth(
      static_cast<std::size_t>(std::clamp(length, 0, 3)));
  const std::int16_t valueX = static_cast<std::int16_t>(
      centers[component] - width / 2 + 7);
  return {static_cast<std::int16_t>(valueX - 2), 218,
          static_cast<std::int16_t>(width + 4), 11};
}

UiBuildStatus UiChromeRenderer::BuildBottom(const UiBottomBarModel &model,
                                            UiBarScene &scene) {
  scene.Clear();
  BarBuilder builder(scene);
  switch (model.kind) {
  case UiBottomBarKind::Hidden:
    break;
  case UiBottomBarKind::TrackNotes: {
    for (std::size_t index = 0; index < model.trackNotes.notes.size();
         ++index) {
      const std::int16_t center = static_cast<std::int16_t>(
          15 + static_cast<std::int16_t>(index) * 30);
      std::array<char, 3> track{
          'T', static_cast<char>('1' + static_cast<int>(index)), 0};
      builder.CenteredText(track.data(), center, 213, UiColorToken::TextDim);
      const std::string_view note = model.trackNotes.notes[index];
      const UiColorToken noteColor =
          note == "--" ? UiColorToken::DerivedTextFaint : UiColorToken::TextNormal;
      if (static_cast<std::int8_t>(index) == model.trackNotes.selectedNote) {
        const std::int16_t width = UiFont5x7::TextWidth(note.size());
        builder.Fill({static_cast<std::int16_t>(center - width / 2 - 2), 226,
                      static_cast<std::int16_t>(width + 4), 9},
                     UiColorToken::TextColored);
        builder.CenteredText(note, center, 227, UiColorToken::TextHighlighted);
      } else {
        builder.CenteredText(note, center, 227, noteColor);
      }
    }
    if (model.trackNotes.selectedTrack >= 0 &&
        model.trackNotes.selectedTrack < 8) {
      const std::int8_t track = model.trackNotes.selectedTrack;
      const std::int16_t center = static_cast<std::int16_t>(15 + track * 30);
      std::array<char, 3> label{'T', static_cast<char>('1' + track), 0};
      const RectI16 selection =
          model.trackNotes.trackSelectionOverride &&
                  !model.trackNotes.trackSelectionRect.Empty()
              ? model.trackNotes.trackSelectionRect
              : BottomTrackTargetRect(track);
      builder.Selection(selection);
      if (model.trackNotes.trackInkVisible) {
        builder.CenteredText(label.data(), center, 213,
                             UiColorToken::TextHighlighted);
      }
    }
    break;
  }
  case UiBottomBarKind::Context:
    if (model.context.secondLineCount == 0) {
      DrawSegments(builder, model.context.firstLine,
                   model.context.firstLineCount, 220);
    } else {
      DrawSegments(builder, model.context.firstLine,
                   model.context.firstLineCount, 213);
      DrawSegments(builder, model.context.secondLine,
                   model.context.secondLineCount, 227);
    }
    break;
  case UiBottomBarKind::Actions: {
    const std::size_t count = std::min<std::size_t>(
        model.actions.count, model.actions.actions.size());
    if (count == 0U)
      break;
    const std::int16_t width =
        static_cast<std::int16_t>(240 / static_cast<std::int16_t>(count));
    for (std::size_t index = 0; index < count; ++index) {
      builder.CenteredText(
          model.actions.actions[index],
          static_cast<std::int16_t>(width * static_cast<std::int16_t>(index) +
                                    width / 2),
          220,
          index == model.actions.active ? UiColorToken::TextColored
                                        : UiColorToken::TextDim);
    }
    break;
  }
  case UiBottomBarKind::Selector: {
    if (model.selector.options.empty() ||
        model.selector.current >= model.selector.options.size()) {
      break;
    }
    const auto centeredOption = [&](std::string_view option,
                                    std::int16_t center,
                                    UiColorToken color) {
      if (model.selector.preserveCase)
        builder.CenteredLiteralText(option, center, 220, color);
      else
        builder.CenteredText(option, center, 220, color);
    };
    if (model.selector.options.size() == 1) {
      centeredOption(model.selector.options[0], 120,
                     model.selector.highlightCurrent
                         ? UiColorToken::TextColored
                         : UiColorToken::TextDim);
      builder.Text("<", 7, 220, UiColorToken::DerivedTextFaint);
      builder.Text(">", 228, 220, UiColorToken::DerivedTextFaint);
      break;
    }
    if (model.selector.options.size() == 2) {
      for (std::uint8_t index = 0; index < 2; ++index) {
        centeredOption(model.selector.options[index],
                       index == 0 ? 60 : 180,
                       model.selector.highlightCurrent &&
                               index == model.selector.current
                           ? UiColorToken::TextColored
                           : UiColorToken::TextDim);
      }
      break;
    }
    const auto optionAt = [&](int index) -> std::string_view {
      if (model.selector.wrap) {
        const int count = static_cast<int>(model.selector.options.size());
        const int wrapped = (index % count + count) % count;
        return model.selector.options[static_cast<std::size_t>(wrapped)];
      }
      if (index < 0 ||
          index >= static_cast<int>(model.selector.options.size())) {
        return {};
      }
      return model.selector.options[static_cast<std::size_t>(index)];
    };
    const int current = model.selector.current;
    centeredOption(optionAt(current - 1), 60, UiColorToken::TextDim);
    centeredOption(optionAt(current), 120,
                   model.selector.highlightCurrent
                       ? UiColorToken::TextColored
                       : UiColorToken::TextDim);
    centeredOption(optionAt(current + 1), 180, UiColorToken::TextDim);
    builder.Text("<", 7, 220, UiColorToken::DerivedTextFaint);
    builder.Text(">", 228, 220, UiColorToken::DerivedTextFaint);
    break;
  }
  case UiBottomBarKind::AdjustmentLegend: {
    std::array<char, 8> fine{};
    std::array<char, 8> coarse{};
    const bool semanticFine = !model.adjustment.fineLabel.empty();
    const bool semanticCoarse = !model.adjustment.coarseLabel.empty();
    if (!semanticFine)
      std::snprintf(fine.data(), fine.size(), "%u",
                    static_cast<unsigned>(model.adjustment.fineStep));
    if (!semanticCoarse) {
      if (model.adjustment.coarseOctave)
        std::snprintf(coarse.data(), coarse.size(), "OCT");
      else
        std::snprintf(coarse.data(), coarse.size(), "%u",
                      static_cast<unsigned>(model.adjustment.coarseStep));
    }
    const std::string_view fineText =
        semanticFine ? model.adjustment.fineLabel : std::string_view(fine.data());
    const std::string_view coarseText = semanticCoarse
                                            ? model.adjustment.coarseLabel
                                            : std::string_view(coarse.data());
    builder.Text("<", 24, 220, UiColorToken::DerivedTextFaint);
    if (!semanticFine)
      DrawPlusMinus(builder, 54, 220);
    builder.CenteredText(fineText, 63, 220, UiColorToken::TextColored);
    builder.Text(">", 91, 220, UiColorToken::DerivedTextFaint);
    DrawVerticalArrow(builder, 142, 222, false);
    if (!semanticCoarse)
      DrawPlusMinus(builder, 170, 220);
    builder.CenteredText(coarseText, 183, 220, UiColorToken::TextColored);
    DrawVerticalArrow(builder, 215, 222, true);
    break;
  }
  case UiBottomBarKind::Rgb: {
    constexpr std::array<std::string_view, 3> labels{"R", "G", "B"};
    constexpr std::array<std::int16_t, 3> centers{42, 120, 198};
    for (std::uint8_t index = 0; index < 3U; ++index) {
      std::array<char, 4> value{};
      std::snprintf(value.data(), value.size(), "%u",
                    static_cast<unsigned>(model.rgb.values[index]));
      const RectI16 target =
          BottomRgbTargetRect(index, model.rgb.values[index]);
      builder.Text(labels[index],
                   static_cast<std::int16_t>(centers[index] - 22), 220,
                   UiColorToken::TextColored);
      if (index == model.rgb.active) {
        builder.Selection(target);
        builder.Text(value.data(), static_cast<std::int16_t>(target.x + 2),
                     220, UiColorToken::TextHighlighted);
      } else {
        builder.Text(value.data(), static_cast<std::int16_t>(target.x + 2),
                     220, UiColorToken::TextNormal);
      }
    }
    break;
  }
  case UiBottomBarKind::Clipboard: {
    std::array<char, 16> dimensions{};
    const unsigned width = model.clipboard.width;
    const unsigned height = model.clipboard.height;
    std::snprintf(dimensions.data(), dimensions.size(), "%uX%u %s", width,
                  height, width == 1U && height == 1U ? "CELL" : "CELLS");
    constexpr std::string_view copied = "COPIED";
    constexpr std::int16_t gap = 9;
    const std::int16_t copiedWidth = UiFont5x7::TextWidth(copied.size());
    const std::int16_t dimensionsWidth =
        UiFont5x7::TextWidth(std::char_traits<char>::length(dimensions.data()));
    const std::int16_t start = static_cast<std::int16_t>(
        (240 - copiedWidth - gap - dimensionsWidth) / 2);
    builder.Text(copied, start, 216, UiColorToken::TextColored);
    builder.Text(dimensions.data(),
                 static_cast<std::int16_t>(start + copiedWidth + gap), 216,
                 UiColorToken::TextColored);
    builder.CenteredText("SHIFT+ENTER: PASTE", 120, 228,
                         UiColorToken::TextNormal);
    break;
  }
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
