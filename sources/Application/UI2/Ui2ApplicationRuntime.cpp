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

  CaptureSong(window, currentSong_);
  if (previousValid_ && currentSong_ == previousSong_) {
    return engine_.PresentDirty();
  }

  const UiSongViewData data = ViewDataFor(currentSong_);
  if (UiSongView::Build(data, engine_.Palette(), scene_) !=
      UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (!previousValid_) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    const UiSongViewData previousData = ViewDataFor(previousSong_);
    UiSongView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                            engine_.Palette());
  }

  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previousSong_ = currentSong_;
    previousValid_ = true;
  }
  return result;
}

UiSongViewData
UiApplicationRuntime::ViewDataFor(const SongFrameState &state) {
  UiSongViewData data;
  data.name = state.name.data();
  data.elapsed = state.elapsed.data();
  data.rows = state.rows;
  for (std::size_t track = 0; track < data.notes.size(); ++track) {
    data.notes[track] = state.notes[track].data();
  }
  data.playbackRows = state.playbackRows;
  data.vuLevelTop = state.vuLevelTop;
  data.rowOffset = state.rowOffset;
  data.editRow = state.editRow;
  data.editTrack = state.editTrack;
  data.playing = state.playing;
  data.power = state.power;
  return data;
}

void UiApplicationRuntime::CaptureSong(AppWindow &window,
                                       SongFrameState &state) {
  state = SongFrameState{};
  ViewData &viewData = window.ViewDataForUi2();
  Project &project = *viewData.project_;
  Player *player = Player::GetInstance();

  project.GetProjectName(state.name.data());
  state.editTrack = static_cast<std::uint8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  state.editRow =
      static_cast<std::uint8_t>(std::clamp(viewData.songY_, 0, 15));

  const int firstRow =
      std::clamp(viewData.songOffset_, 0, SONG_ROW_COUNT - 16);
  state.rowOffset = static_cast<std::uint8_t>(firstRow);
  for (std::uint8_t row = 0; row < 16U; ++row) {
    const int sourceRow = firstRow + row;
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
      state.rows[row][track] =
          viewData.song_->data_[sourceRow * SONG_CHANNEL_COUNT + track];
    }
  }

  state.playing = player != nullptr && player->IsRunning();
  state.power = CurrentPowerState(state.playing);
  const int elapsed =
      state.playing ? std::max(0, static_cast<int>(player->GetPlayTime())) : 0;
  std::snprintf(state.elapsed.data(), state.elapsed.size(), "%02d:%02d",
                (elapsed / 60) % 100, elapsed % 60);

  for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
    state.playbackRows[track] = -1;
    if (state.playing && player->IsChannelPlaying(track) &&
        viewData.currentPlayChain_[track] != 0xFFU &&
        viewData.playMode_ != PM_AUDITION) {
      const int visibleRow = viewData.songPlayPos_[track] - firstRow;
      if (visibleRow >= 0 && visibleRow < 16) {
        state.playbackRows[track] = static_cast<std::int8_t>(visibleRow);
      }
    }

    if (state.playing) {
      const char *playedNote = player->GetPlayedNote(track);
      const std::array<char, 2> pitch{playedNote[0], playedNote[1]};
      const char *playedOctave = player->GetPlayedOctive(track);
      if (pitch[0] == ' ' || playedOctave[1] == '-') {
        std::snprintf(state.notes[track].data(), state.notes[track].size(),
                      "--");
      } else if (pitch[1] == ' ') {
        if (playedOctave[0] == '-') {
          std::snprintf(state.notes[track].data(), state.notes[track].size(),
                        "%c-%c", pitch[0], playedOctave[1]);
        } else {
          std::snprintf(state.notes[track].data(), state.notes[track].size(),
                        "%c%c", pitch[0], playedOctave[1]);
        }
      } else if (playedOctave[0] == '-') {
        std::snprintf(state.notes[track].data(), state.notes[track].size(),
                      "%c%c-%c", pitch[0], pitch[1], playedOctave[1]);
      } else {
        std::snprintf(state.notes[track].data(), state.notes[track].size(),
                      "%c%c%c", pitch[0], pitch[1], playedOctave[1]);
      }
    } else {
      std::snprintf(state.notes[track].data(), state.notes[track].size(), "--");
    }
  }

  const std::uint32_t level =
      state.playing ? static_cast<std::uint32_t>(player->GetMasterLevel()) : 0U;
  state.vuLevelTop[0] =
      VuTopFromAmplitude(static_cast<std::uint16_t>(level >> 16U));
  state.vuLevelTop[1] =
      VuTopFromAmplitude(static_cast<std::uint16_t>(level & 0xFFFFU));
}

} // namespace ui2
