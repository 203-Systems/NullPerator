/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2ApplicationRuntime.h"

#include "Application/AppWindow.h"
#include "Application/Model/Project.h"
#include "Application/Player/Player.h"
#include "Application/Views/ViewData.h"
#include "System/System/System.h"

#include <algorithm>
#include <cstdio>

namespace ui2 {
namespace {

UiPowerState CurrentPowerState(bool playing) {
  if (playing) return UiPowerState::Playing;
  BatteryState battery{};
  System::GetInstance()->GetBatteryState(battery);
  if (battery.error) return UiPowerState::BatteryNormal;
  if (battery.charging) return UiPowerState::Charging;
  if (battery.percentage <= 15U) return UiPowerState::BatteryLow;
  if (battery.percentage >= 80U) return UiPowerState::BatteryHigh;
  return UiPowerState::BatteryNormal;
}

} // namespace

bool UiApplicationRuntime::Supports(const AppWindow &window) const {
  return window.IsCurrentViewForUi2(VT_SONG) && !window.HasModalForUi2();
}

PresentResult UiApplicationRuntime::Present(AppWindow &window) {
  if (!Supports(window)) return PresentResult::Deferred;

  UiSongViewData data;
  CaptureSong(window, data);
  if (UiSongView::Build(data, engine_.Palette(), scene_) !=
      UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  return engine_.PresentDirty();
}

void UiApplicationRuntime::CaptureSong(AppWindow &window,
                                       UiSongViewData &data) {
  ViewData &viewData = window.ViewDataForUi2();
  Project &project = *viewData.project_;
  Player *player = Player::GetInstance();

  project.GetProjectName(songName_.data());
  data.name = songName_.data();
  data.editTrack = static_cast<std::uint8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  data.editRow =
      static_cast<std::uint8_t>(std::clamp(viewData.songY_, 0, 15));

  const int firstRow =
      std::clamp(viewData.songOffset_, 0, SONG_ROW_COUNT - 16);
  for (std::uint8_t row = 0; row < 16U; ++row) {
    const int sourceRow = firstRow + row;
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
      data.rows[row][track] =
          viewData.song_->data_[sourceRow * SONG_CHANNEL_COUNT + track];
    }
  }

  data.playing = player != nullptr && player->IsRunning();
  data.power = CurrentPowerState(data.playing);
  const int elapsed =
      data.playing ? std::max(0, static_cast<int>(player->GetPlayTime())) : 0;
  std::snprintf(elapsed_.data(), elapsed_.size(), "%02d:%02d",
                (elapsed / 60) % 100, elapsed % 60);
  data.elapsed = elapsed_.data();

  for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
    data.playbackRows[track] = -1;
    if (data.playing && player->IsChannelPlaying(track) &&
        viewData.currentPlayChain_[track] != 0xFFU &&
        viewData.playMode_ != PM_AUDITION) {
      const int visibleRow = viewData.songPlayPos_[track] - firstRow;
      if (visibleRow >= 0 && visibleRow < 16) {
        data.playbackRows[track] = static_cast<std::int8_t>(visibleRow);
      }
    }

    if (data.playing) {
      const char *playedNote = player->GetPlayedNote(track);
      const std::array<char, 2> pitch{playedNote[0], playedNote[1]};
      const char *playedOctave = player->GetPlayedOctive(track);
      if (pitch[0] == ' ' || playedOctave[1] == '-') {
        std::snprintf(notes_[track].data(), notes_[track].size(), "--");
      } else if (pitch[1] == ' ') {
        if (playedOctave[0] == '-') {
          std::snprintf(notes_[track].data(), notes_[track].size(), "%c-%c",
                        pitch[0], playedOctave[1]);
        } else {
          std::snprintf(notes_[track].data(), notes_[track].size(), "%c%c",
                        pitch[0], playedOctave[1]);
        }
      } else if (playedOctave[0] == '-') {
        std::snprintf(notes_[track].data(), notes_[track].size(), "%c%c-%c",
                      pitch[0], pitch[1], playedOctave[1]);
      } else {
        std::snprintf(notes_[track].data(), notes_[track].size(), "%c%c%c",
                      pitch[0], pitch[1], playedOctave[1]);
      }
    } else {
      std::snprintf(notes_[track].data(), notes_[track].size(), "--");
    }
    data.notes[track] = notes_[track].data();
  }

  const std::uint32_t level =
      data.playing ? static_cast<std::uint32_t>(player->GetMasterLevel()) : 0U;
  data.vuLevelTop[0] =
      VuTopFromAmplitude(static_cast<std::uint16_t>(level >> 16U));
  data.vuLevelTop[1] =
      VuTopFromAmplitude(static_cast<std::uint16_t>(level & 0xFFFFU));
}

} // namespace ui2
