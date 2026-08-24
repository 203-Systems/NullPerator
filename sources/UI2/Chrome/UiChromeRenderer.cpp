/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Chrome/UiChromeRenderer.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace ui2 {
namespace {

using BarBuilder = UiSceneBuilder<64, 256>;

void DrawSegments(BarBuilder &builder,
                  const std::array<UiColoredText, 3> &segments,
                  std::uint8_t count, std::int16_t y) {
  std::int16_t x = 9;
  for (std::uint8_t index = 0; index < count; ++index) {
    const std::int16_t segmentX =
        segments[index].x >= 0 ? segments[index].x : x;
    builder.Text(segments[index].text, segmentX, y, segments[index].color);
    x = static_cast<std::int16_t>(
        segmentX + UiFont5x7::TextWidth(segments[index].text.size()) + 6);
  }
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
  const std::int16_t fillWidth =
      model.power == UiPowerState::Charging ||
              model.power == UiPowerState::BatteryHigh
          ? 16
          : (model.power == UiPowerState::BatteryLow ? 4 : 11);
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

UiBuildStatus UiChromeRenderer::BuildTop(const UiTopBarModel &model,
                                         UiBarScene &scene,
                                         std::optional<RectI16> navHighlight) {
  scene.Clear();
  BarBuilder builder(scene);
  const std::uint8_t titleScale = model.title.size() <= 7 ? 2 : 1;
  builder.Text(model.title, 9, 10, UiColorToken::TextPrimary, titleScale);
  if (!model.meta.empty()) {
    const std::int16_t metaX =
        model.metaX >= 0
            ? model.metaX
            : static_cast<std::int16_t>(
                  9 + UiFont5x7::TextWidth(model.title.size(), titleScale) + 7);
    builder.Text(model.meta, metaX, 10, UiColorToken::CursorPrimary);
    if (model.metaSelected) {
      const RectI16 selection =
          model.metaSelectionOverride && !model.metaSelectionRect.Empty()
              ? model.metaSelectionRect
              : MetaTargetRect(model);
      builder.Selection(selection);
      if (model.metaInkVisible) {
        builder.Text(model.meta, metaX, 10, UiColorToken::CursorInk);
      }
    }
  }

  if (model.power == UiPowerState::Playing) {
    builder.Text(">", 193, 14, UiColorToken::PlaybackActive);
    builder.Text(model.elapsed, 206, 14, UiColorToken::TextPrimary);
  } else if (model.power == UiPowerState::Navigation) {
    builder.CenteredText("P", 204, 3, UiColorToken::TextPrimary);
    builder.Selection(navHighlight.value_or(NavTargetRect(model.navTarget)));
    builder.Text("S", 202, 13,
                 model.navTarget == UiNavTarget::Song
                     ? UiColorToken::CursorInk
                     : UiColorToken::TextPrimary);
    builder.Text("C", 210, 13,
                 model.navTarget == UiNavTarget::Chain
                     ? UiColorToken::CursorInk
                     : UiColorToken::TextPrimary);
    builder.Text("P", 218, 13,
                 model.navTarget == UiNavTarget::Phrase
                     ? UiColorToken::CursorInk
                     : UiColorToken::TextPrimary);
    builder.Text("I", 226, 13,
                 model.navTarget == UiNavTarget::Instrument
                     ? UiColorToken::CursorInk
                     : UiColorToken::TextPrimary);
    builder.CenteredText("M", 204, 24,
                         model.navTarget == UiNavTarget::Mixer
                             ? UiColorToken::CursorInk
                             : UiColorToken::TextPrimary);
    if (model.navTarget == UiNavTarget::Project) {
      // Redraw selected glyph after the bubble so it uses cursor ink.
      builder.CenteredText("P", 204, 3, UiColorToken::CursorInk);
    }
  } else {
    if (model.showBatteryPercent) {
      std::array<char, 5> percent{};
      std::snprintf(percent.data(), percent.size(), "%u%%",
                    static_cast<unsigned>(model.batteryPercent));
      builder.Text(percent.data(), 184, 14, UiColorToken::TextPrimary);
    }
    DrawPower(model, builder);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
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
  }
  return {201, 12, 7, 9};
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
  return {static_cast<std::int16_t>(center - 7), 211, 15, 9};
}

UiBuildStatus UiChromeRenderer::BuildBottom(const UiBottomBarModel &model,
                                            UiBarScene &scene) {
  scene.Clear();
  BarBuilder builder(scene);
  switch (model.kind) {
  case UiBottomBarKind::Hidden:
    break;
  case UiBottomBarKind::TrackNotes: {
    for (std::int16_t index = 0; index < 8; ++index) {
      const std::int16_t center = static_cast<std::int16_t>(15 + index * 30);
      std::array<char, 3> track{'T', static_cast<char>('1' + index), 0};
      builder.CenteredText(track.data(), center, 213, UiColorToken::TextMuted);
      const std::string_view note = model.trackNotes.notes[index];
      const UiColorToken noteColor =
          note == "--" ? UiColorToken::TextDim : UiColorToken::TextPrimary;
      if (index == model.trackNotes.selectedNote) {
        const std::int16_t width = UiFont5x7::TextWidth(note.size());
        builder.Fill({static_cast<std::int16_t>(center - width / 2 - 2), 226,
                      static_cast<std::int16_t>(width + 4), 9},
                     UiColorToken::CursorPrimary);
        builder.CenteredText(note, center, 227, UiColorToken::CursorInk);
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
                             UiColorToken::CursorInk);
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
    if (model.actions.count == 0)
      break;
    const std::int16_t width = 240 / model.actions.count;
    for (std::uint8_t index = 0; index < model.actions.count; ++index) {
      builder.CenteredText(
          model.actions.actions[index],
          static_cast<std::int16_t>(width * index + width / 2), 220,
          index == model.actions.active ? UiColorToken::CursorPrimary
                                        : UiColorToken::TextMuted);
    }
    break;
  }
  case UiBottomBarKind::Selector: {
    if (model.selector.options.empty() ||
        model.selector.current >= model.selector.options.size()) {
      break;
    }
    if (model.selector.options.size() == 2) {
      for (std::uint8_t index = 0; index < 2; ++index) {
        builder.CenteredText(
            model.selector.options[index], index == 0 ? 60 : 180, 220,
            index == model.selector.current ? UiColorToken::CursorPrimary
                                            : UiColorToken::TextMuted);
      }
      break;
    }
    const auto optionAt = [&](int index) -> std::string_view {
      if (model.selector.wrap) {
        const int count = static_cast<int>(model.selector.options.size());
        return model.selector.options[(index % count + count) % count];
      }
      if (index < 0 ||
          index >= static_cast<int>(model.selector.options.size())) {
        return {};
      }
      return model.selector.options[index];
    };
    const int current = model.selector.current;
    builder.CenteredText(optionAt(current - 1), 60, 220,
                         UiColorToken::TextMuted);
    builder.CenteredText(optionAt(current), 120, 220,
                         UiColorToken::CursorPrimary);
    builder.CenteredText(optionAt(current + 1), 180, 220,
                         UiColorToken::TextMuted);
    builder.Text("<", 7, 220, UiColorToken::TextDim);
    builder.Text(">", 228, 220, UiColorToken::TextDim);
    break;
  }
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
