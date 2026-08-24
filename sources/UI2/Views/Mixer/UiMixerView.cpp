/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Mixer/UiMixerView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Render/UiVuGradient.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

constexpr std::array<std::int16_t, UiMixerView::kChannelCount> kCenters{
    15, 39, 63, 87, 111, 135, 159, 183, 217};
constexpr std::array<std::string_view, UiMixerView::kChannelCount> kLabels{
    "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "MASTER"};

RectI16 CenteredDamage(std::uint8_t channel, std::int16_t y,
                       std::int16_t height) {
  if (channel >= UiMixerView::kChannelCount)
    return {};
  const std::int16_t halfWidth = channel == 8U ? 23 : 12;
  return Intersect({static_cast<std::int16_t>(kCenters[channel] - halfWidth), y,
                    static_cast<std::int16_t>(halfWidth * 2 + 1), height},
                   RectI16::Screen());
}

} // namespace

RectI16 UiMixerView::MeterDamageRect(std::uint8_t channel, std::uint8_t side) {
  if (channel >= kChannelCount || side >= 2U)
    return {};
  return {static_cast<std::int16_t>(kCenters[channel] - 8 + side * 10),
          kMeterTop, 7, kMeterHeight};
}

RectI16 UiMixerView::MeterLevelDamageRect(std::uint8_t channel,
                                          std::uint8_t side,
                                          std::uint8_t previousLevelTop,
                                          std::uint8_t currentLevelTop) {
  const RectI16 meter = MeterDamageRect(channel, side);
  if (meter.Empty())
    return {};
  const std::int16_t previous =
      std::min<std::int16_t>(previousLevelTop, kMeterHeight);
  const std::int16_t current =
      std::min<std::int16_t>(currentLevelTop, kMeterHeight);
  const std::int16_t top = std::min(previous, current);
  const std::int16_t bottom = std::max(previous, current);
  return {meter.x, static_cast<std::int16_t>(meter.y + top), meter.width,
          static_cast<std::int16_t>(bottom - top)};
}

RectI16 UiMixerView::ValueDamageRect(std::uint8_t channel) {
  return CenteredDamage(channel, 205, 11);
}

RectI16 UiMixerView::LabelDamageRect(std::uint8_t channel) {
  return CenteredDamage(channel, 222, 11);
}

void UiMixerView::RenderDelta(const UiMixerViewData &previous,
                              const UiMixerViewData &current,
                              const UiFrameScene &currentScene,
                              UiIndexedSurface &surface,
                              const UiPalette &palette) {
  if (previous.power != current.power ||
      previous.navCursor != current.navCursor) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette,
                                  {184, 0, 56, 34});
  }
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  for (std::uint8_t channel = 0; channel < kChannelCount; ++channel) {
    for (std::uint8_t side = 0; side < 2U; ++side) {
      if (previous.vuLevelTop[channel][side] !=
          current.vuLevelTop[channel][side]) {
        render(MeterLevelDamageRect(channel, side,
                                    previous.vuLevelTop[channel][side],
                                    current.vuLevelTop[channel][side]));
      }
    }
    if (previous.volumes[channel] != current.volumes[channel]) {
      render(ValueDamageRect(channel));
    }
  }
  if (previous.selectedChannel != current.selectedChannel) {
    if (previous.selectedChannel >= 0 &&
        previous.selectedChannel < kChannelCount) {
      const auto channel = static_cast<std::uint8_t>(previous.selectedChannel);
      render(ValueDamageRect(channel));
      render(LabelDamageRect(channel));
    }
    if (current.selectedChannel >= 0 &&
        current.selectedChannel < kChannelCount) {
      const auto channel = static_cast<std::uint8_t>(current.selectedChannel);
      render(ValueDamageRect(channel));
      render(LabelDamageRect(channel));
    }
  }
}

UiBuildStatus UiMixerView::Build(const UiMixerViewData &data,
                                 UiPalette &palette, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomVisible = false;
  scene.topBackground = UiColorToken::SurfaceTopBar;

  const UiTopBarModel top{
      .title = "MIXER",
      .power = data.power,
      .navTarget = UiNavTarget::Mixer,
      .navCursor = data.navCursor,
  };
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;

  if (!UiVuGradient::Configure(palette, kMeterHeight)) {
    return UiBuildStatus::CommandOverflow;
  }
  UiSceneBuilder<256, 1024> builder(scene.content);
  for (std::uint8_t channel = 0; channel < kChannelCount; ++channel) {
    for (std::uint8_t side = 0; side < 2U; ++side) {
      const RectI16 meter = MeterDamageRect(channel, side);
      builder.Fill(meter, UiColorToken::DerivedVuTrack);
      const std::uint8_t level =
          std::min<std::uint8_t>(data.vuLevelTop[channel][side], kMeterHeight);
      builder.VerticalPaletteRamp(
          {meter.x, static_cast<std::int16_t>(meter.y + level), meter.width,
           static_cast<std::int16_t>(meter.height - level)},
          UiVuGradient::IndexAt(level));
    }
    const bool selected = data.selectedChannel == channel;
    builder.CenteredText(data.volumes[channel], kCenters[channel], 207,
                         selected ? UiColorToken::TextColored
                                  : UiColorToken::TextNormal);
    builder.CenteredText(kLabels[channel], kCenters[channel], 224,
                         selected ? UiColorToken::TextColored
                                  : UiColorToken::TextDim);
  }

  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
