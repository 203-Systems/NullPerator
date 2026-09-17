/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Scene/UiSceneBuilder.h"
#include "UI2/Views/Instrument/UiInstrumentCapacity.h"
#include "UI2/Views/Instrument/UiInstrumentSections.h"

#include <array>
#include <string_view>

namespace ui2 {
inline constexpr std::array<std::string_view, kUiInstrumentTypeCount>
    kUiInstrumentTypeNames{"NONE",    "SAMPLE",   "MIDI",    "SID",
                           "OPAL",    "DRUM",     "STACK",   "CHIPTUNE",
                           "GB-WAVE", "GB-PULSE", "GB-NOISE"};
inline constexpr std::array<std::string_view, kUiInstrumentTypeCount>
    kUiInstrumentTypeHelp{"EMPTY INSTRUMENT SLOT",
                          "PLAY AND LOOP AUDIO SAMPLES",
                          "SEND NOTES TO EXTERNAL MIDI DEVICES",
                          "THREE-OSCILLATOR CHIP SYNTHESIS",
                          "TWO-OPERATOR FM SYNTHESIS",
                          "12-VOICE SYNTH DRUM KIT",
                          "FIVE-OSCILLATOR CHORD / UNISON SYNTH",
                          "RETRO CHIP LEADS, BASS AND ARPEGGIOS",
                          "GAME BOY-STYLE CUSTOM 4-BIT WAVES",
                          "GAME BOY-STYLE PULSE LEADS AND BASS",
                          "GAME BOY-STYLE NOISE DRUMS AND FX"};

[[nodiscard]] constexpr RectI16
InstrumentTypeSelectorCursorRect(UiInstrumentKind kind) {
  const auto index = static_cast<unsigned>(kind);
  if (index >= kUiInstrumentTypeCount)
    return {};
  return {static_cast<std::int16_t>(13 + index % 3 * 74),
          static_cast<std::int16_t>(42 + index / 3 * 23), 66, 17};
}

inline UiBuildStatus
BuildInstrumentTypeSelector(UiInstrumentKind selected, UiPowerState power,
                            std::string_view elapsed, UiFrameScene &scene,
                            RectI16 cursor = {}, bool cursorOverride = false,
                            bool inkVisible = true) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  auto status = UiChromeRenderer::BuildTop(
      {.title = "INST SELECT", .elapsed = elapsed, .power = power}, scene.top);
  if (status != UiBuildStatus::Built)
    return status;

  const unsigned index =
      static_cast<unsigned>(selected) < kUiInstrumentTypeCount
          ? static_cast<unsigned>(selected)
          : 0;
  UiBottomBarModel bottom{.kind = UiBottomBarKind::Context};
  bottom.context.firstLineCount = bottom.context.secondLineCount = 1;
  bottom.context.firstLine[0] = {kUiInstrumentTypeNames[index],
                                 UiColorToken::TextColored, 9};
  bottom.context.secondLine[0] = {kUiInstrumentTypeHelp[index],
                                  UiColorToken::TextNormal, 9};
  status = UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (status != UiBuildStatus::Built)
    return status;

  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Selection(
      cursorOverride ? cursor : InstrumentTypeSelectorCursorRect(selected));
  for (unsigned i = 0; i < kUiInstrumentTypeCount; ++i) {
    const auto cell =
        InstrumentTypeSelectorCursorRect(static_cast<UiInstrumentKind>(i));
    builder.CenteredText(kUiInstrumentTypeNames[i], cell.x + 33, cell.y + 4,
                         i == index && inkVisible
                             ? UiColorToken::TextHighlighted
                             : UiColorToken::TextNormal);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}
} // namespace ui2
