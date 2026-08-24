/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2ApplicationRuntime.h"

#include "Application/AppWindow.h"
#include "Application/Model/Project.h"
#include "Application/Model/Table.h"
#include "Application/Player/Player.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "Application/Views/ViewData.h"
#include "System/System/System.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace ui2 {
namespace {

constexpr std::uint16_t kSongCursorDurationMs = 120;
constexpr std::uint16_t kPhraseCursorDurationMs = 120;

UiPowerState CurrentPowerState(bool playing) {
  if (playing)
    return UiPowerState::Playing;
  BatteryState battery{};
  System::GetInstance()->GetBatteryState(battery);
  if (battery.error)
    return UiPowerState::BatteryNormal;
  if (battery.charging)
    return UiPowerState::Charging;
  if (battery.percentage <= 15U)
    return UiPowerState::BatteryLow;
  if (battery.percentage >= 80U)
    return UiPowerState::BatteryHigh;
  return UiPowerState::BatteryNormal;
}

template <std::size_t Size>
void CopyText(std::array<char, Size> &destination, const char *source) {
  destination.fill(0);
  if (source == nullptr || Size == 0U)
    return;
  std::snprintf(destination.data(), destination.size(), "%s", source);
}

template <std::size_t Size>
void CopyUpper(std::array<char, Size> &destination, const char *source,
               std::size_t length = static_cast<std::size_t>(-1)) {
  destination.fill(0);
  if (source == nullptr || Size == 0U)
    return;
  std::size_t index = 0;
  while (index + 1U < Size && source[index] != '\0' && index < length) {
    destination[index] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(source[index])));
    ++index;
  }
}

void FormatElapsed(Player *player, bool playing, std::array<char, 6> &elapsed) {
  const int seconds =
      playing ? std::max(0, static_cast<int>(player->GetPlayTime())) : 0;
  std::snprintf(elapsed.data(), elapsed.size(), "%02d:%02d",
                (seconds / 60) % 100, seconds % 60);
}

void FormatNote(std::uint8_t value, std::array<char, 5> &text) {
  text.fill(0);
  if (value == NO_NOTE) {
    CopyText(text, "----");
    return;
  }
  if (value == NOTE_OFF) {
    CopyText(text, "OFF");
    return;
  }
  if (value > HIGHEST_NOTE) {
    CopyText(text, "????");
    return;
  }
  const char *pitch = noteNames[value % 12U];
  const int octave = static_cast<int>(value / 12U) - 2;
  if (pitch[1] == ' ') {
    std::snprintf(text.data(), text.size(), "%c%d", pitch[0], octave);
  } else {
    std::snprintf(text.data(), text.size(), "%c%c%d", pitch[0], pitch[1],
                  octave);
  }
}

void FormatCommand(FourCC command, std::array<char, 4> &text) {
  const char *source = command.c_str();
  if (source != nullptr && source[0] != '\0' && source[1] != '\0' &&
      source[2] != '\0' && source[3] == '\0') {
    CopyUpper(text, source);
  } else {
    CopyText(text, "???");
  }
}

template <typename Notes>
void CaptureTrackNotes(Player *player, bool playing, Notes &notes) {
  for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
    if (!playing) {
      CopyText(notes[track], "--");
      continue;
    }
    const char *playedNote = player->GetPlayedNote(track);
    const std::array<char, 2> pitch{playedNote[0], playedNote[1]};
    const char *playedOctave = player->GetPlayedOctive(track);
    if (pitch[0] == ' ' || playedOctave[1] == '-') {
      CopyText(notes[track], "--");
    } else if (pitch[1] == ' ') {
      if (playedOctave[0] == '-') {
        std::snprintf(notes[track].data(), notes[track].size(), "%c-%c",
                      pitch[0], playedOctave[1]);
      } else {
        std::snprintf(notes[track].data(), notes[track].size(), "%c%c",
                      pitch[0], playedOctave[1]);
      }
    } else if (playedOctave[0] == '-') {
      std::snprintf(notes[track].data(), notes[track].size(), "%c%c-%c",
                    pitch[0], pitch[1], playedOctave[1]);
    } else {
      std::snprintf(notes[track].data(), notes[track].size(), "%c%c%c",
                    pitch[0], pitch[1], playedOctave[1]);
    }
  }
}

template <std::size_t LeadSize, std::size_t TailSize,
          std::size_t DescriptionSize>
void CaptureHelpLegend(FourCC command, std::array<char, LeadSize> &lead,
                       std::array<char, TailSize> &tail,
                       std::array<char, DescriptionSize> &description) {
  char **legend = getHelpLegend(command);
  const char *title = legend == nullptr ? nullptr : legend[0];
  const char *detail = legend == nullptr ? nullptr : legend[1];
  const char *colon = title == nullptr ? nullptr : std::strchr(title, ':');
  if (colon == nullptr) {
    CopyUpper(lead, title);
  } else {
    std::size_t leadLength = static_cast<std::size_t>(colon - title);
    while (leadLength > 0U && title[leadLength - 1U] == ' ')
      --leadLength;
    CopyUpper(lead, title, leadLength);
    const char *suffix = colon + 1;
    while (*suffix == ' ')
      ++suffix;
    CopyUpper(tail, suffix);
  }
  CopyUpper(description, detail);
}

} // namespace

bool UiApplicationRuntime::Supports(const AppWindow &window) const {
  return (window.IsCurrentViewForUi2(VT_SONG) ||
          window.IsCurrentViewForUi2(VT_PHRASE) ||
          window.IsCurrentViewForUi2(VT_TABLE) ||
          window.IsCurrentViewForUi2(VT_TABLE2)) &&
         !window.HasModalForUi2();
}

PresentResult UiApplicationRuntime::Present(AppWindow &window) {
  if (!Supports(window))
    return PresentResult::Deferred;
  System *system = System::GetInstance();
  const std::uint32_t nowMs = system == nullptr ? 0U : system->Millis();
  const RuntimePage page =
      window.IsCurrentViewForUi2(VT_SONG)
          ? RuntimePage::Song
          : (window.IsCurrentViewForUi2(VT_PHRASE) ? RuntimePage::Phrase
                                                   : RuntimePage::Table);
  if (page != activePage_) {
    previousValid_ = false;
    cursorTargetValid_ = false;
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    activePage_ = page;
  }
  switch (page) {
  case RuntimePage::Song:
    return PresentSong(window, nowMs);
  case RuntimePage::Phrase:
    return PresentPhrase(window, nowMs);
  case RuntimePage::Table:
    return PresentTable(window, nowMs);
  case RuntimePage::None:
    return PresentResult::Deferred;
  }
  return PresentResult::Deferred;
}

PresentResult UiApplicationRuntime::PresentSong(AppWindow &window,
                                                std::uint32_t nowMs) {
  CaptureSong(window, currentSong_);
  const RectI16 target = UiSongView::CursorTargetRect(currentSong_.editTrack,
                                                      currentSong_.editRow);
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kSongCursorDurationMs);
    cursorTarget_ = target;
  }
  currentSong_.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  currentSong_.cursorVisualOverride = true;
  currentSong_.cursorInkVisible =
      !cursors_.Active(UiCursorRole::Content, nowMs);
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

UiSongViewData UiApplicationRuntime::ViewDataFor(const SongFrameState &state) {
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
  data.cursorVisualRect = state.cursorVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.playing = state.playing;
  data.power = state.power;
  return data;
}

UiPhraseViewData
UiApplicationRuntime::ViewDataFor(const PhraseFrameState &state) {
  UiPhraseViewData data;
  data.number = state.number.data();
  data.elapsed = state.elapsed.data();
  for (std::size_t row = 0; row < state.rows.size(); ++row) {
    data.rows[row] = {
        state.rows[row].note.data(), state.rows[row].instrument.data(),
        state.rows[row].fx1.data(),  state.rows[row].parameter1.data(),
        state.rows[row].fx2.data(),  state.rows[row].parameter2.data()};
  }
  for (std::size_t track = 0; track < state.trackNotes.size(); ++track) {
    data.trackNotes[track] = state.trackNotes[track].data();
  }
  data.cursorBottom.kind = UiBottomBarKind::Hidden;
  if (state.context == PhraseContext::Instrument) {
    data.cursorBottom.kind = UiBottomBarKind::Context;
    data.cursorBottom.context.firstLineCount = 2;
    data.cursorBottom.context.firstLine[0] = {.text = state.contextLead.data(),
                                              .color =
                                                  UiColorToken::CursorPrimary,
                                              .x = 9};
    data.cursorBottom.context.firstLine[1] = {.text = state.contextTail.data(),
                                              .color =
                                                  UiColorToken::TextNormal,
                                              .x = 94};
  } else if (state.context == PhraseContext::Fx) {
    data.cursorBottom.kind = UiBottomBarKind::Context;
    data.cursorBottom.context.firstLineCount =
        state.contextTail[0] == '\0' ? 1 : 2;
    data.cursorBottom.context.firstLine[0] = {.text = state.contextLead.data(),
                                              .color =
                                                  UiColorToken::CursorPrimary,
                                              .x = 9};
    if (data.cursorBottom.context.firstLineCount == 2) {
      data.cursorBottom.context.firstLine[1] = {
          .text = state.contextTail.data(),
          .color = UiColorToken::TextNormal,
          .x = static_cast<std::int16_t>(
              9 + UiFont5x7::TextWidth(std::strlen(state.contextLead.data())) +
              7)};
    }
    if (state.contextDescription[0] != '\0') {
      data.cursorBottom.context.secondLineCount = 1;
      data.cursorBottom.context.secondLine[0] = {
          .text = state.contextDescription.data(),
          .color = UiColorToken::TextNormal,
          .x = 9};
    }
  }
  data.editRow = state.editRow;
  data.editColumn = state.editColumn;
  data.selectedTrack = state.selectedTrack;
  data.activeHeader = state.activeHeader;
  data.cursorVisualRect = state.cursorVisualRect;
  data.topMetaVisualRect = state.topMetaVisualRect;
  data.bottomTrackVisualRect = state.bottomTrackVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.topMetaVisualOverride = state.topMetaVisualOverride;
  data.bottomTrackVisualOverride = state.bottomTrackVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.topMetaInkVisible = state.topMetaInkVisible;
  data.bottomTrackInkVisible = state.bottomTrackInkVisible;
  data.numberFocus = state.numberFocus;
  data.power = state.power;
  return data;
}

PresentResult UiApplicationRuntime::PresentPhrase(AppWindow &window,
                                                  std::uint32_t nowMs) {
  CapturePhrase(window, currentPhrase_);
  if (currentPhrase_.numberFocus) {
    const UiTopBarModel top{
        .title = "PHRASE", .meta = currentPhrase_.number.data(), .metaX = 85};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(currentPhrase_.selectedTrack);
    if (!topMetaTargetValid_) {
      cursors_.Snap(UiCursorRole::TopMeta, topTarget, nowMs);
      topMetaTarget_ = topTarget;
      topMetaTargetValid_ = true;
    } else if (topTarget != topMetaTarget_) {
      cursors_.Retarget(UiCursorRole::TopMeta, topTarget, nowMs,
                        kPhraseCursorDurationMs);
      topMetaTarget_ = topTarget;
    }
    if (!bottomTrackTargetValid_) {
      cursors_.Snap(UiCursorRole::BottomTrack, bottomTarget, nowMs);
      bottomTrackTarget_ = bottomTarget;
      bottomTrackTargetValid_ = true;
    } else if (bottomTarget != bottomTrackTarget_) {
      cursors_.Retarget(UiCursorRole::BottomTrack, bottomTarget, nowMs,
                        kPhraseCursorDurationMs);
      bottomTrackTarget_ = bottomTarget;
    }
    currentPhrase_.topMetaVisualRect =
        cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    currentPhrase_.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    currentPhrase_.topMetaVisualOverride = true;
    currentPhrase_.bottomTrackVisualOverride = true;
    currentPhrase_.topMetaInkVisible =
        !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    currentPhrase_.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    const UiPhraseViewData capture = ViewDataFor(currentPhrase_);
    const RectI16 target = UiPhraseView::CursorTargetRect(capture);
    if (!cursorTargetValid_) {
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
      cursorTarget_ = target;
      cursorTargetValid_ = true;
    } else if (target != cursorTarget_) {
      cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                        kPhraseCursorDurationMs);
      cursorTarget_ = target;
    }
    currentPhrase_.cursorVisualRect =
        cursors_.Sample(UiCursorRole::Content, nowMs);
    currentPhrase_.cursorVisualOverride = true;
    currentPhrase_.cursorInkVisible =
        !cursors_.Active(UiCursorRole::Content, nowMs);
  }

  if (previousValid_ && currentPhrase_ == previousPhrase_) {
    return engine_.PresentDirty();
  }
  const UiPhraseViewData data = ViewDataFor(currentPhrase_);
  if (UiPhraseView::Build(data, engine_.Palette(), scene_) !=
      UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (!previousValid_) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    const UiPhraseViewData previousData = ViewDataFor(previousPhrase_);
    UiPhraseView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                              engine_.Palette());
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previousPhrase_ = currentPhrase_;
    previousValid_ = true;
  }
  return result;
}

UiTableViewData
UiApplicationRuntime::ViewDataFor(const TableFrameState &state) {
  UiTableViewData data;
  data.number = state.number.data();
  data.elapsed = state.elapsed.data();
  for (std::size_t row = 0; row < state.rows.size(); ++row) {
    data.rows[row] = {
        state.rows[row].fx1.data(), state.rows[row].parameter1.data(),
        state.rows[row].fx2.data(), state.rows[row].parameter2.data(),
        state.rows[row].fx3.data(), state.rows[row].parameter3.data()};
  }
  for (std::size_t track = 0; track < state.trackNotes.size(); ++track) {
    data.trackNotes[track] = state.trackNotes[track].data();
  }
  data.cursorBottom.kind = UiBottomBarKind::Hidden;
  if (state.context == PhraseContext::Fx) {
    data.cursorBottom.kind = UiBottomBarKind::Context;
    data.cursorBottom.context.firstLineCount =
        state.contextTail[0] == '\0' ? 1 : 2;
    data.cursorBottom.context.firstLine[0] = {.text = state.contextLead.data(),
                                              .color =
                                                  UiColorToken::CursorPrimary,
                                              .x = 9};
    if (data.cursorBottom.context.firstLineCount == 2) {
      data.cursorBottom.context.firstLine[1] = {
          .text = state.contextTail.data(),
          .color = UiColorToken::TextNormal,
          .x = static_cast<std::int16_t>(
              9 + UiFont5x7::TextWidth(std::strlen(state.contextLead.data())) +
              7)};
    }
    if (state.contextDescription[0] != '\0') {
      data.cursorBottom.context.secondLineCount = 1;
      data.cursorBottom.context.secondLine[0] = {
          .text = state.contextDescription.data(),
          .color = UiColorToken::TextNormal,
          .x = 9};
    }
  }
  data.editRow = state.editRow;
  data.editColumn = state.editColumn;
  data.selectedTrack = state.selectedTrack;
  data.activeHeader = state.activeHeader;
  data.cursorVisualRect = state.cursorVisualRect;
  data.topMetaVisualRect = state.topMetaVisualRect;
  data.bottomTrackVisualRect = state.bottomTrackVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.topMetaVisualOverride = state.topMetaVisualOverride;
  data.bottomTrackVisualOverride = state.bottomTrackVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.topMetaInkVisible = state.topMetaInkVisible;
  data.bottomTrackInkVisible = state.bottomTrackInkVisible;
  data.numberFocus = state.numberFocus;
  data.power = state.power;
  return data;
}

PresentResult UiApplicationRuntime::PresentTable(AppWindow &window,
                                                 std::uint32_t nowMs) {
  CaptureTable(window, currentTable_);
  if (currentTable_.numberFocus) {
    const UiTopBarModel top{.title = "TABLE",
                            .meta = currentTable_.number.data()};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(currentTable_.selectedTrack);
    if (!topMetaTargetValid_) {
      cursors_.Snap(UiCursorRole::TopMeta, topTarget, nowMs);
      topMetaTarget_ = topTarget;
      topMetaTargetValid_ = true;
    } else if (topTarget != topMetaTarget_) {
      cursors_.Retarget(UiCursorRole::TopMeta, topTarget, nowMs,
                        kPhraseCursorDurationMs);
      topMetaTarget_ = topTarget;
    }
    if (!bottomTrackTargetValid_) {
      cursors_.Snap(UiCursorRole::BottomTrack, bottomTarget, nowMs);
      bottomTrackTarget_ = bottomTarget;
      bottomTrackTargetValid_ = true;
    } else if (bottomTarget != bottomTrackTarget_) {
      cursors_.Retarget(UiCursorRole::BottomTrack, bottomTarget, nowMs,
                        kPhraseCursorDurationMs);
      bottomTrackTarget_ = bottomTarget;
    }
    currentTable_.topMetaVisualRect =
        cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    currentTable_.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    currentTable_.topMetaVisualOverride = true;
    currentTable_.bottomTrackVisualOverride = true;
    currentTable_.topMetaInkVisible =
        !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    currentTable_.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    const UiTableViewData capture = ViewDataFor(currentTable_);
    const RectI16 target = UiTableView::CursorTargetRect(capture);
    if (!cursorTargetValid_) {
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
      cursorTarget_ = target;
      cursorTargetValid_ = true;
    } else if (target != cursorTarget_) {
      cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                        kPhraseCursorDurationMs);
      cursorTarget_ = target;
    }
    currentTable_.cursorVisualRect =
        cursors_.Sample(UiCursorRole::Content, nowMs);
    currentTable_.cursorVisualOverride = true;
    currentTable_.cursorInkVisible =
        !cursors_.Active(UiCursorRole::Content, nowMs);
  }
  if (previousValid_ && currentTable_ == previousTable_) {
    return engine_.PresentDirty();
  }
  const UiTableViewData data = ViewDataFor(currentTable_);
  if (UiTableView::Build(data, engine_.Palette(), scene_) !=
      UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (!previousValid_) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    const UiTableViewData previousData = ViewDataFor(previousTable_);
    UiTableView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                             engine_.Palette());
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previousTable_ = currentTable_;
    previousValid_ = true;
  }
  return result;
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
  state.editRow = static_cast<std::uint8_t>(std::clamp(viewData.songY_, 0, 15));

  const int firstRow = std::clamp(viewData.songOffset_, 0, SONG_ROW_COUNT - 16);
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

void UiApplicationRuntime::CapturePhrase(AppWindow &window,
                                         PhraseFrameState &state) {
  state = PhraseFrameState{};
  ViewData &viewData = window.ViewDataForUi2();
  Project &project = *viewData.project_;
  Phrase &phrase = viewData.song_->phrase_;
  Player *player = Player::GetInstance();

  const std::uint8_t phraseNumber = static_cast<std::uint8_t>(
      std::clamp(viewData.currentPhrase_, 0, PHRASE_COUNT - 1));
  hex2char(phraseNumber, state.number.data());
  state.editRow = static_cast<std::uint8_t>(
      std::clamp(window.PhraseRowForUi2(), 0, STEPS_PER_PHRASE - 1));
  state.editColumn =
      static_cast<std::uint8_t>(std::clamp(window.PhraseColumnForUi2(), 0, 5));
  state.selectedTrack = static_cast<std::int8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  state.numberFocus = (window.ButtonMaskForUi2() & EPBM_ENTER) != 0U;

  const int base = static_cast<int>(phraseNumber) * STEPS_PER_PHRASE;
  for (std::uint8_t row = 0; row < STEPS_PER_PHRASE; ++row) {
    const int index = base + row;
    FormatNote(phrase.note_[index], state.rows[row].note);
    if (phrase.instr_[index] == 0xFFU) {
      CopyText(state.rows[row].instrument, "I--");
    } else {
      state.rows[row].instrument[0] = 'I';
      hex2char(phrase.instr_[index], state.rows[row].instrument.data() + 1);
    }
    FormatCommand(phrase.cmd1_[index], state.rows[row].fx1);
    hexshort2char(phrase.param1_[index], state.rows[row].parameter1.data());
    FormatCommand(phrase.cmd2_[index], state.rows[row].fx2);
    hexshort2char(phrase.param2_[index], state.rows[row].parameter2.data());
  }

  const bool playing = player != nullptr && player->IsRunning();
  state.power = CurrentPowerState(playing);
  FormatElapsed(player, playing, state.elapsed);
  CaptureTrackNotes(player, playing, state.trackNotes);

  const int selectedIndex = base + state.editRow;
  if (state.editColumn <= 1U) {
    std::uint8_t instrumentId = 0xFFU;
    bool cellHasValue = false;
    if (state.editColumn == 0U) {
      cellHasValue = phrase.note_[selectedIndex] != NO_NOTE;
      if (cellHasValue) {
        for (int row = state.editRow; row >= 0; --row) {
          const std::uint8_t candidate = phrase.instr_[base + row];
          if (candidate != 0xFFU) {
            instrumentId = candidate;
            break;
          }
        }
      }
    } else {
      cellHasValue = phrase.instr_[selectedIndex] != 0xFFU;
      instrumentId = phrase.instr_[selectedIndex];
    }
    const auto &instruments = project.GetInstrumentBank()->InstrumentsList();
    if (cellHasValue && instrumentId < instruments.size() &&
        instruments[instrumentId] != nullptr) {
      state.context = PhraseContext::Instrument;
      std::snprintf(state.contextLead.data(), state.contextLead.size(),
                    "INSTRUMENT %02X", instrumentId);
      const auto name = instruments[instrumentId]->GetDisplayName();
      CopyUpper(state.contextTail, name.c_str());
      state.activeHeader = state.editColumn == 0U ? UiPhraseHeader::Note
                                                  : UiPhraseHeader::Instrument;
    }
  } else {
    const bool firstFx = state.editColumn <= 3U;
    const FourCC command =
        firstFx ? phrase.cmd1_[selectedIndex] : phrase.cmd2_[selectedIndex];
    if (command != FourCC::InstrumentCommandNone) {
      state.context = PhraseContext::Fx;
      state.activeHeader = firstFx ? UiPhraseHeader::Fx1 : UiPhraseHeader::Fx2;
      CaptureHelpLegend(command, state.contextLead, state.contextTail,
                        state.contextDescription);
    }
  }
}

void UiApplicationRuntime::CaptureTable(AppWindow &window,
                                        TableFrameState &state) {
  state = TableFrameState{};
  ViewData &viewData = window.ViewDataForUi2();
  Player *player = Player::GetInstance();
  const int tableNumber =
      std::clamp(viewData.currentTable_, 0, TABLE_COUNT - 1);
  Table &table = TableHolder::GetInstance()->GetTable(tableNumber);

  state.number[0] = window.IsCurrentViewForUi2(VT_TABLE2) ? 'I' : 'P';
  hex2char(static_cast<std::uint8_t>(tableNumber), state.number.data() + 1);
  state.editRow = static_cast<std::uint8_t>(
      std::clamp(window.TableRowForUi2(), 0, TABLE_STEPS - 1));
  state.editColumn = static_cast<std::uint8_t>(
      std::clamp(window.TableColumnForUi2(), 0, TABLE_COLUMNS * 2 - 1));
  state.selectedTrack = static_cast<std::int8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  state.numberFocus = (window.ButtonMaskForUi2() & EPBM_ENTER) != 0U;

  for (std::uint8_t row = 0; row < TABLE_STEPS; ++row) {
    FormatCommand(table.cmd1_[row], state.rows[row].fx1);
    hexshort2char(table.param1_[row], state.rows[row].parameter1.data());
    FormatCommand(table.cmd2_[row], state.rows[row].fx2);
    hexshort2char(table.param2_[row], state.rows[row].parameter2.data());
    FormatCommand(table.cmd3_[row], state.rows[row].fx3);
    hexshort2char(table.param3_[row], state.rows[row].parameter3.data());
  }

  const bool playing = player != nullptr && player->IsRunning();
  state.power = CurrentPowerState(playing);
  FormatElapsed(player, playing, state.elapsed);
  CaptureTrackNotes(player, playing, state.trackNotes);

  const std::uint8_t group = state.editColumn / 2U;
  const FourCC command = group == 0U   ? table.cmd1_[state.editRow]
                         : group == 1U ? table.cmd2_[state.editRow]
                                       : table.cmd3_[state.editRow];
  if (command != FourCC::InstrumentCommandNone) {
    state.context = PhraseContext::Fx;
    state.activeHeader = group == 0U   ? UiTableHeader::Fx1
                         : group == 1U ? UiTableHeader::Fx2
                                       : UiTableHeader::Fx3;
    CaptureHelpLegend(command, state.contextLead, state.contextTail,
                      state.contextDescription);
  }
}

} // namespace ui2
