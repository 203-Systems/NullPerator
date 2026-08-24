/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2ApplicationRuntime.h"

#include "Application/AppWindow.h"
#include "Application/Model/Groove.h"
#include "Application/Model/Project.h"
#include "Application/Model/Table.h"
#include "Application/Player/Player.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "Application/Views/UiGridSelection.h"
#include "Application/Views/DeviceView.h"
#include "Application/Views/InstrumentView.h"
#include "Application/Views/ViewData.h"
#include "System/System/System.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

namespace ui2 {
namespace {

constexpr std::uint16_t kSongCursorDurationMs = 120;
constexpr std::uint16_t kChainCursorDurationMs = 120;
constexpr std::uint16_t kPhraseCursorDurationMs = 120;
constexpr std::uint16_t kGrooveCursorDurationMs = 120;
constexpr std::uint16_t kListCursorDurationMs = 120;

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

void FormatVolume(int value, std::array<char, 4> &text) {
  value = std::clamp(value, 0, 999);
  text.fill(0);
  if (value >= 100) {
    text[0] = static_cast<char>('0' + value / 100);
    text[1] = static_cast<char>('0' + (value / 10) % 10);
    text[2] = static_cast<char>('0' + value % 10);
    return;
  }
  text[0] = static_cast<char>('0' + value / 10);
  text[1] = static_cast<char>('0' + value % 10);
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

UiApplicationRuntime::PowerFrameState
UiApplicationRuntime::CapturePowerState(bool playing) {
  if (!batterySampleGate_.ShouldSample(playing, frameNowMs_)) {
    return playing ? PowerFrameState{.power = UiPowerState::Playing}
                   : cachedPower_;
  }

  // Treat an unavailable/error sample as invalid cached state. That preserves
  // the 1 Hz read ceiling while ensuring Web never renders a fabricated 0%.
  cachedPower_ = {};
  System *system = System::GetInstance();
  if (system == nullptr)
    return cachedPower_;
  BatteryState battery{};
  system->GetBatteryState(battery);
  if (battery.error)
    return cachedPower_;

  cachedPower_.batteryPercent = std::min<std::uint8_t>(
      battery.percentage, static_cast<std::uint8_t>(100));
  cachedPower_.batteryPercentValid = true;
  if (battery.charging)
    cachedPower_.power = UiPowerState::Charging;
  else if (battery.percentage <= 15U)
    cachedPower_.power = UiPowerState::BatteryLow;
  else if (battery.percentage >= 80U)
    cachedPower_.power = UiPowerState::BatteryHigh;
  return cachedPower_;
}

UiPowerState UiApplicationRuntime::CurrentPowerState(bool playing) {
  return CapturePowerState(playing).power;
}

void UiApplicationRuntime::ActivatePage(RuntimePage page) {
  switch (page) {
  case RuntimePage::Song:
    std::construct_at(&frames_.song);
    break;
  case RuntimePage::Chain:
    std::construct_at(&frames_.chain);
    break;
  case RuntimePage::Phrase:
    std::construct_at(&frames_.phrase);
    break;
  case RuntimePage::Table:
    std::construct_at(&frames_.table);
    break;
  case RuntimePage::Instrument:
    std::construct_at(&frames_.instrument);
    break;
  case RuntimePage::Device:
    std::construct_at(&frames_.device);
    break;
  case RuntimePage::Browser:
    std::construct_at(&frames_.browser);
    break;
  case RuntimePage::Groove:
    std::construct_at(&frames_.groove);
    break;
  case RuntimePage::Mixer:
    std::construct_at(&frames_.mixer);
    break;
  case RuntimePage::None:
    break;
  }
  previousValid_ = false;
  dialogPreviousValid_ = false;
  cursorTargetValid_ = false;
  topMetaTargetValid_ = false;
  bottomTrackTargetValid_ = false;
  activePage_ = page;
}

void UiApplicationRuntime::CaptureDialog(AppWindow &window) {
  currentDialog_ = DialogFrameState{};
  if (!window.HasModalForUi2())
    return;
  currentDialog_.snapshot = window.ModalSnapshotForUi2();
  currentDialog_.instanceId = window.ModalInstanceIdForUi2();
  currentDialog_.active = true;
}

bool UiApplicationRuntime::DialogChanged() const {
  return !dialogPreviousValid_ || currentDialog_ != previousDialog_;
}

bool UiApplicationRuntime::RequiresFullRebuild() const {
  if (!previousValid_ || !dialogPreviousValid_)
    return true;
  if (currentDialog_.active != previousDialog_.active)
    return true;
  return currentDialog_.active &&
         (currentDialog_.instanceId != previousDialog_.instanceId ||
          currentDialog_.snapshot.kind != previousDialog_.snapshot.kind);
}

bool UiApplicationRuntime::FullScreenDialogActive() const {
  return currentDialog_.active &&
         currentDialog_.snapshot.kind == UiDialogKind::FullScreen;
}

bool UiApplicationRuntime::CanCommitHiddenBaseWithoutRender() const {
  return previousValid_ && dialogPreviousValid_ &&
         FullScreenDialogActive() && currentDialog_ == previousDialog_;
}

UiBuildStatus UiApplicationRuntime::ApplyDialog() {
  if (!currentDialog_.active)
    return UiBuildStatus::Built;
  return UiDialogView::Apply(currentDialog_.snapshot.ToViewData(), scene_);
}

void UiApplicationRuntime::RenderDialogDelta() {
  if (!currentDialog_.active || !DialogChanged())
    return;
  UiDialogView::RenderDelta(previousDialog_.snapshot.ToViewData(),
                            currentDialog_.snapshot.ToViewData(), scene_,
                            engine_.Surface(), engine_.Palette());
}

void UiApplicationRuntime::CommitDialog() {
  previousDialog_ = currentDialog_;
  dialogPreviousValid_ = true;
}

bool UiApplicationRuntime::Supports(const AppWindow &window) const {
  return (window.IsCurrentViewForUi2(VT_SONG) ||
          window.IsCurrentViewForUi2(VT_CHAIN) ||
          window.IsCurrentViewForUi2(VT_PHRASE) ||
          window.IsCurrentViewForUi2(VT_TABLE) ||
          window.IsCurrentViewForUi2(VT_TABLE2) ||
          window.IsCurrentViewForUi2(VT_INSTRUMENT) ||
          window.IsCurrentViewForUi2(VT_DEVICE) ||
          window.IsCurrentViewForUi2(VT_IMPORT) ||
          window.IsCurrentViewForUi2(VT_INSTRUMENT_IMPORT) ||
          window.IsCurrentViewForUi2(VT_SELECTPROJECT) ||
          window.IsCurrentViewForUi2(VT_THEME_IMPORT) ||
          window.IsCurrentViewForUi2(VT_GROOVE) ||
          window.IsCurrentViewForUi2(VT_MIXER));
}

PresentResult UiApplicationRuntime::Present(AppWindow &window) {
  if (!Supports(window))
    return PresentResult::Deferred;
  System *system = System::GetInstance();
  const std::uint32_t nowMs = system == nullptr ? 0U : system->Millis();
  frameNowMs_ = nowMs;
  RuntimePage page = RuntimePage::Table;
  if (window.IsCurrentViewForUi2(VT_SONG))
    page = RuntimePage::Song;
  else if (window.IsCurrentViewForUi2(VT_CHAIN))
    page = RuntimePage::Chain;
  else if (window.IsCurrentViewForUi2(VT_PHRASE))
    page = RuntimePage::Phrase;
  else if (window.IsCurrentViewForUi2(VT_INSTRUMENT))
    page = RuntimePage::Instrument;
  else if (window.IsCurrentViewForUi2(VT_DEVICE))
    page = RuntimePage::Device;
  else if (window.IsCurrentViewForUi2(VT_IMPORT) ||
           window.IsCurrentViewForUi2(VT_INSTRUMENT_IMPORT) ||
           window.IsCurrentViewForUi2(VT_SELECTPROJECT) ||
           window.IsCurrentViewForUi2(VT_THEME_IMPORT))
    page = RuntimePage::Browser;
  else if (window.IsCurrentViewForUi2(VT_GROOVE))
    page = RuntimePage::Groove;
  else if (window.IsCurrentViewForUi2(VT_MIXER))
    page = RuntimePage::Mixer;
  if (page != activePage_)
    ActivatePage(page);
  CaptureDialog(window);
  switch (page) {
  case RuntimePage::Song:
    return PresentSong(window, nowMs);
  case RuntimePage::Chain:
    return PresentChain(window, nowMs);
  case RuntimePage::Phrase:
    return PresentPhrase(window, nowMs);
  case RuntimePage::Table:
    return PresentTable(window, nowMs);
  case RuntimePage::Instrument:
    return PresentInstrument(window, nowMs);
  case RuntimePage::Device:
    return PresentDevice(window, nowMs);
  case RuntimePage::Browser:
    return PresentBrowser(window, nowMs);
  case RuntimePage::Groove:
    return PresentGroove(window, nowMs);
  case RuntimePage::Mixer:
    return PresentMixer(window);
  case RuntimePage::None:
    return PresentResult::Deferred;
  }
  return PresentResult::Deferred;
}

PresentResult UiApplicationRuntime::PresentSong(AppWindow &window,
                                                std::uint32_t nowMs) {
  SongFrameState &current = frames_.song.current;
  SongFrameState &previous = frames_.song.previous;
  CaptureSong(window, current);
  const RectI16 target =
      UiSongView::CursorTargetRect(current.editTrack, current.editRow);
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kSongCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = true;
  current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }

  const UiSongViewData data = ViewDataFor(current);
  if (UiSongView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiSongViewData previousData = ViewDataFor(previous);
      UiSongView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                              engine_.Palette());
    }
    RenderDialogDelta();
  }

  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
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
  data.selectionVisualRect = state.selectionVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.playing = state.playing;
  data.liveMode = state.liveMode;
  data.power = state.power;
  return data;
}

UiChainViewData
UiApplicationRuntime::ViewDataFor(const ChainFrameState &state) {
  UiChainViewData data;
  data.number = state.number.data();
  data.elapsed = state.elapsed.data();
  data.phrases = state.phrases;
  data.transposes = state.transposes;
  for (std::size_t track = 0; track < state.trackNotes.size(); ++track) {
    data.trackNotes[track] = state.trackNotes[track].data();
  }
  data.vuLevelTop = state.vuLevelTop;
  data.editRow = state.editRow;
  data.editColumn = state.editColumn;
  data.selectedTrack = state.selectedTrack;
  data.cursorVisualRect = state.cursorVisualRect;
  data.selectionVisualRect = state.selectionVisualRect;
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

PresentResult UiApplicationRuntime::PresentChain(AppWindow &window,
                                                 std::uint32_t nowMs) {
  ChainFrameState &current = frames_.chain.current;
  ChainFrameState &previous = frames_.chain.previous;
  CaptureChain(window, current);
  if (current.numberFocus) {
    const UiTopBarModel top{.title = "CHAIN",
                            .meta = current.number.data()};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(current.selectedTrack);
    if (!topMetaTargetValid_) {
      cursors_.Snap(UiCursorRole::TopMeta, topTarget, nowMs);
      topMetaTarget_ = topTarget;
      topMetaTargetValid_ = true;
    } else if (topTarget != topMetaTarget_) {
      cursors_.Retarget(UiCursorRole::TopMeta, topTarget, nowMs,
                        kChainCursorDurationMs);
      topMetaTarget_ = topTarget;
    }
    if (!bottomTrackTargetValid_) {
      cursors_.Snap(UiCursorRole::BottomTrack, bottomTarget, nowMs);
      bottomTrackTarget_ = bottomTarget;
      bottomTrackTargetValid_ = true;
    } else if (bottomTarget != bottomTrackTarget_) {
      cursors_.Retarget(UiCursorRole::BottomTrack, bottomTarget, nowMs,
                        kChainCursorDurationMs);
      bottomTrackTarget_ = bottomTarget;
    }
    current.topMetaVisualRect = cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    current.topMetaVisualOverride = true;
    current.bottomTrackVisualOverride = true;
    current.topMetaInkVisible = !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    const UiChainViewData capture = ViewDataFor(current);
    const RectI16 target = UiChainView::CursorTargetRect(capture);
    if (!cursorTargetValid_) {
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
      cursorTarget_ = target;
      cursorTargetValid_ = true;
    } else if (target != cursorTarget_) {
      cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                        kChainCursorDurationMs);
      cursorTarget_ = target;
    }
    current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
    current.cursorVisualOverride = true;
    current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  }

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiChainViewData data = ViewDataFor(current);
  if (UiChainView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiChainViewData previousData = ViewDataFor(previous);
      UiChainView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                               engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
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
  data.editDigit = state.editDigit;
  data.selectedTrack = state.selectedTrack;
  data.activeHeader = state.activeHeader;
  data.cursorVisualRect = state.cursorVisualRect;
  data.selectionVisualRect = state.selectionVisualRect;
  data.topMetaVisualRect = state.topMetaVisualRect;
  data.bottomTrackVisualRect = state.bottomTrackVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.topMetaVisualOverride = state.topMetaVisualOverride;
  data.bottomTrackVisualOverride = state.bottomTrackVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.topMetaInkVisible = state.topMetaInkVisible;
  data.bottomTrackInkVisible = state.bottomTrackInkVisible;
  data.enterDigitFocus = state.enterDigitFocus;
  data.numberFocus = state.numberFocus;
  data.power = state.power;
  return data;
}

PresentResult UiApplicationRuntime::PresentPhrase(AppWindow &window,
                                                  std::uint32_t nowMs) {
  PhraseFrameState &current = frames_.phrase.current;
  PhraseFrameState &previous = frames_.phrase.previous;
  CapturePhrase(window, current);
  if (current.numberFocus) {
    const UiTopBarModel top{
        .title = "PHRASE", .meta = current.number.data(), .metaX = 85};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(current.selectedTrack);
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
    current.topMetaVisualRect = cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    current.topMetaVisualOverride = true;
    current.bottomTrackVisualOverride = true;
    current.topMetaInkVisible = !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    const UiPhraseViewData capture = ViewDataFor(current);
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
    current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
    current.cursorVisualOverride = true;
    current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  }

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiPhraseViewData data = ViewDataFor(current);
  if (UiPhraseView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiPhraseViewData previousData = ViewDataFor(previous);
      UiPhraseView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                                engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
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
  data.editDigit = state.editDigit;
  data.selectedTrack = state.selectedTrack;
  data.activeHeader = state.activeHeader;
  data.cursorVisualRect = state.cursorVisualRect;
  data.selectionVisualRect = state.selectionVisualRect;
  data.topMetaVisualRect = state.topMetaVisualRect;
  data.bottomTrackVisualRect = state.bottomTrackVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.topMetaVisualOverride = state.topMetaVisualOverride;
  data.bottomTrackVisualOverride = state.bottomTrackVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.topMetaInkVisible = state.topMetaInkVisible;
  data.bottomTrackInkVisible = state.bottomTrackInkVisible;
  data.enterDigitFocus = state.enterDigitFocus;
  data.numberFocus = state.numberFocus;
  data.power = state.power;
  return data;
}

PresentResult UiApplicationRuntime::PresentTable(AppWindow &window,
                                                 std::uint32_t nowMs) {
  TableFrameState &current = frames_.table.current;
  TableFrameState &previous = frames_.table.previous;
  CaptureTable(window, current);
  if (current.numberFocus) {
    const UiTopBarModel top{.title = "TABLE",
                            .meta = current.number.data()};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(current.selectedTrack);
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
    current.topMetaVisualRect = cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    current.topMetaVisualOverride = true;
    current.bottomTrackVisualOverride = true;
    current.topMetaInkVisible = !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    const UiTableViewData capture = ViewDataFor(current);
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
    current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
    current.cursorVisualOverride = true;
    current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  }
  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiTableViewData data = ViewDataFor(current);
  if (UiTableView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiTableViewData previousData = ViewDataFor(previous);
      UiTableView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                               engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiInstrumentViewData
UiApplicationRuntime::ViewDataFor(const InstrumentFrameState &state) {
  UiInstrumentViewData data;
  data.number = state.number.data();
  data.elapsed = state.elapsed.data();
  data.name = state.name.data();
  data.kind = state.kind;
  data.fieldCount = state.fieldCount;
  for (std::size_t index = 0; index < state.fieldCount; ++index) {
    data.fields[index] = {state.fields[index].label.data(),
                          state.fields[index].value.data(),
                          state.fields[index].y};
  }
  data.operatorCount = state.operatorCount;
  for (std::size_t index = 0; index < state.operatorCount; ++index) {
    data.operators[index] = {state.operators[index].label.data(),
                             state.operators[index].op1.data(),
                             state.operators[index].op2.data()};
  }
  for (std::size_t track = 0; track < state.trackNotes.size(); ++track)
    data.trackNotes[track] = state.trackNotes[track].data();
  data.selectedField = state.selectedField;
  data.selectedOperator = state.selectedOperator;
  data.nameAction = state.nameAction;
  data.selectedTrack = state.selectedTrack;
  data.cursor = state.cursor;
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
  data.scrollOffset = state.scrollOffset;
  data.power = state.power;
  return data;
}

PresentResult UiApplicationRuntime::PresentInstrument(AppWindow &window,
                                                       std::uint32_t nowMs) {
  InstrumentFrameState &current = frames_.instrument.current;
  InstrumentFrameState &previous = frames_.instrument.previous;
  const std::int16_t previousScroll =
      previousValid_ ? previous.scrollOffset : 0;
  CaptureInstrument(window, current);
  if (current.numberFocus) {
    current.scrollOffset = previousScroll;
    const UiTopBarModel top{.title = "INST", .meta = current.number.data()};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(current.selectedTrack);
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
    current.topMetaVisualRect = cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    current.topMetaVisualOverride = true;
    current.bottomTrackVisualOverride = true;
    current.topMetaInkVisible = !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    UiInstrumentViewData capture = ViewDataFor(current);
    current.scrollOffset =
        UiInstrumentView::RevealCursor(previousScroll, capture);
    capture.scrollOffset = current.scrollOffset;
    const RectI16 target = UiInstrumentView::CursorTargetRect(capture);
    if (!cursorTargetValid_) {
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
      cursorTarget_ = target;
      cursorTargetValid_ = true;
    } else if (target != cursorTarget_) {
      cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                        kPhraseCursorDurationMs);
      cursorTarget_ = target;
    }
    current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
    current.cursorVisualOverride = !target.Empty();
    current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  }

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiInstrumentViewData data = ViewDataFor(current);
  if (UiInstrumentView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiInstrumentViewData previousData = ViewDataFor(previous);
      UiInstrumentView::RenderDelta(previousData, data, scene_,
                                    engine_.Surface(), engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiDeviceViewData
UiApplicationRuntime::ViewDataFor(const DeviceFrameState &state) {
  UiDeviceViewData data;
  data.midiDevice = state.midiDevice.data();
  data.midiSync = state.midiSync.data();
  data.lineOut = state.lineOut.data();
  data.remoteUi = state.remoteUi.data();
  data.resampler = state.resampler.data();
  data.volume = state.volume.data();
  data.brightness = state.brightness.data();
  data.theme = state.theme.data();
  data.font = state.font.data();
  data.version = state.version.data();
  for (std::size_t index = 0; index < state.selectorOptions.size(); ++index)
    data.selectorOptions[index] = state.selectorOptions[index].data();
  data.selectorCount = state.selectorCount;
  data.selectorCurrent = state.selectorCurrent;
  data.selectorWrap = state.selectorWrap;
  data.showLineOut = state.showLineOut;
  data.showVolume = state.showVolume;
  data.showTheme = state.showTheme;
  data.showFont = state.showFont;
  data.showUpdateFirmware = state.showUpdateFirmware;
  data.batteryPercentValid = state.batteryPercentValid;
  data.batteryPercent = state.batteryPercent;
  data.cursor = state.cursor;
  data.cursorVisualRect = state.cursorVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.scrollOffset = state.scrollOffset;
  data.power = state.power;
  return data;
}

PresentResult UiApplicationRuntime::PresentDevice(AppWindow &window,
                                                   std::uint32_t nowMs) {
  DeviceFrameState &current = frames_.device.current;
  DeviceFrameState &previous = frames_.device.previous;
  const std::int16_t previousScroll =
      previousValid_ ? previous.scrollOffset : 0;
  CaptureDevice(window, current);
  UiDeviceViewData capture = ViewDataFor(current);
  current.scrollOffset = UiDeviceView::RevealCursor(previousScroll, capture);
  capture.scrollOffset = current.scrollOffset;
  const RectI16 target = UiDeviceView::CursorTargetRect(capture);
  const bool scrollChanged =
      previousValid_ && current.scrollOffset != previousScroll;
  if (scrollChanged && cursorTargetValid_) {
    RectI16 rebased = cursors_.Sample(UiCursorRole::Content, nowMs);
    rebased.y = static_cast<std::int16_t>(
        rebased.y + current.scrollOffset - previousScroll);
    cursors_.Snap(UiCursorRole::Content, rebased, nowMs);
  }
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_ || scrollChanged) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = !target.Empty();
  current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiDeviceViewData data = ViewDataFor(current);
  if (UiDeviceView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiDeviceViewData previousData = ViewDataFor(previous);
      UiDeviceView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                                engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiBrowserViewData
UiApplicationRuntime::ViewDataFor(const BrowserFrameState &state) {
  UiBrowserViewData data = state.snapshot.ViewData(state.power);
  data.cursorVisualRect = state.cursorVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  return data;
}

PresentResult UiApplicationRuntime::PresentBrowser(AppWindow &window,
                                                    std::uint32_t nowMs) {
  BrowserFrameState &current = frames_.browser.current;
  BrowserFrameState &previous = frames_.browser.previous;
  CaptureBrowser(window, current);
  const RectI16 target = current.snapshot.hasSelection
                              ? UiBrowserView::CursorTargetRect(
                                    current.snapshot.selectedRow)
                              : RectI16{};
  if (target.Empty()) {
    // An empty browser has no rendered cursor. End any in-flight transition
    // immediately so hidden geometry cannot keep the frame state changing
    // after RenderDelta correctly produces no pixel damage.
    if (!cursorTargetValid_ || target != cursorTarget_)
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = !target.Empty();
  current.cursorInkVisible = current.snapshot.hasSelection &&
                             !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiBrowserViewData data = ViewDataFor(current);
  if (UiBrowserView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiBrowserViewData previousData = ViewDataFor(previous);
      UiBrowserView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                                 engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiGrooveViewData
UiApplicationRuntime::ViewDataFor(const GrooveFrameState &state) {
  UiGrooveViewData data;
  data.number = state.number.data();
  data.steps = state.steps;
  data.editRow = state.editRow;
  data.cursorVisualRect = state.cursorVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.power = state.power;
  return data;
}

PresentResult UiApplicationRuntime::PresentGroove(AppWindow &window,
                                                  std::uint32_t nowMs) {
  GrooveFrameState &current = frames_.groove.current;
  GrooveFrameState &previous = frames_.groove.previous;
  CaptureGroove(window, current);
  const RectI16 target = UiGrooveView::CursorTargetRect(current.editRow);
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kGrooveCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = true;
  current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiGrooveViewData data = ViewDataFor(current);
  if (UiGrooveView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiGrooveViewData previousData = ViewDataFor(previous);
      UiGrooveView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                                engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiMixerViewData
UiApplicationRuntime::ViewDataFor(const MixerFrameState &state) {
  UiMixerViewData data;
  data.vuLevelTop = state.vuLevelTop;
  for (std::size_t channel = 0; channel < data.volumes.size(); ++channel) {
    data.volumes[channel] = state.volumes[channel].data();
  }
  data.selectedChannel = state.selectedChannel;
  data.power = state.power;
  return data;
}

PresentResult UiApplicationRuntime::PresentMixer(AppWindow &window) {
  MixerFrameState &current = frames_.mixer.current;
  MixerFrameState &previous = frames_.mixer.previous;
  CaptureMixer(window, current);
  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiMixerViewData data = ViewDataFor(current);
  if (UiMixerView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiMixerViewData previousData = ViewDataFor(previous);
      UiMixerView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                               engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
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
  const UiGridSelection selection = window.GridSelectionForUi2();
  if (selection.active) {
    state.selectionVisualRect = UiSongView::SelectionTargetRect(
        selection.left, selection.top, selection.right, selection.bottom,
        state.rowOffset);
  }
  for (std::uint8_t row = 0; row < 16U; ++row) {
    const int sourceRow = firstRow + row;
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
      state.rows[row][track] =
          viewData.song_->data_[sourceRow * SONG_CHANNEL_COUNT + track];
    }
  }

  state.playing = player != nullptr && player->IsRunning();
  state.liveMode =
      player != nullptr && player->GetSequencerMode() == SM_LIVE;
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

void UiApplicationRuntime::CaptureChain(AppWindow &window,
                                        ChainFrameState &state) {
  state = ChainFrameState{};
  ViewData &viewData = window.ViewDataForUi2();
  Player *player = Player::GetInstance();

  const std::uint8_t chainNumber = static_cast<std::uint8_t>(
      std::clamp(viewData.currentChain_, 0, CHAIN_COUNT - 1));
  hex2char(chainNumber, state.number.data());
  state.editRow = static_cast<std::uint8_t>(
      std::clamp(viewData.chainRow_, 0, PHRASES_PER_CHAIN - 1));
  state.editColumn =
      static_cast<std::uint8_t>(std::clamp(viewData.chainCol_, 0, 1));
  state.selectedTrack = static_cast<std::int8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  const UiGridSelection selection = window.GridSelectionForUi2();
  if (selection.active) {
    state.selectionVisualRect = UiChainView::SelectionTargetRect(
        selection.left, selection.top, selection.right, selection.bottom);
  }
  state.numberFocus = !selection.active &&
                      (window.ButtonMaskForUi2() & EPBM_EDIT) != 0U;

  const int base = static_cast<int>(chainNumber) * PHRASES_PER_CHAIN;
  for (std::uint8_t row = 0; row < PHRASES_PER_CHAIN; ++row) {
    state.phrases[row] = viewData.song_->chain_.data_[base + row];
    state.transposes[row] = viewData.song_->chain_.transpose_[base + row];
  }

  const bool playing = player != nullptr && player->IsRunning();
  state.power = CurrentPowerState(playing);
  FormatElapsed(player, playing, state.elapsed);
  CaptureTrackNotes(player, playing, state.trackNotes);

  const std::uint32_t level =
      playing ? static_cast<std::uint32_t>(player->GetMasterLevel()) : 0U;
  const auto scaleVu = [](std::uint8_t songTop) {
    return static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(songTop) * UiChainView::kMeterHeight +
         76U) /
        153U);
  };
  state.vuLevelTop[0] =
      scaleVu(VuTopFromAmplitude(static_cast<std::uint16_t>(level >> 16U)));
  state.vuLevelTop[1] = scaleVu(
      VuTopFromAmplitude(static_cast<std::uint16_t>(level & 0xFFFFU)));
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
  const unsigned short phraseMask = window.ButtonMaskForUi2();
  const UiGridSelection selection = window.GridSelectionForUi2();
  if (selection.active) {
    state.selectionVisualRect = UiPhraseView::SelectionTargetRect(
        selection.left, selection.top, selection.right, selection.bottom);
  }
  state.numberFocus = !selection.active && (phraseMask & EPBM_EDIT) != 0U;
  state.editDigit = static_cast<std::uint8_t>(
      std::clamp(window.PhraseParameterDigitForUi2(), 0, 3));
  state.enterDigitFocus = !state.numberFocus &&
                          (phraseMask & EPBM_ENTER) != 0U &&
                          (state.editColumn == 3U || state.editColumn == 5U);
  state.activeHeader =
      state.editColumn == 0U   ? UiPhraseHeader::Note
      : state.editColumn == 1U ? UiPhraseHeader::Instrument
      : state.editColumn <= 3U ? UiPhraseHeader::Fx1
                               : UiPhraseHeader::Fx2;

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
    }
  } else {
    const bool firstFx = state.editColumn <= 3U;
    const FourCC command =
        firstFx ? phrase.cmd1_[selectedIndex] : phrase.cmd2_[selectedIndex];
    if (command != FourCC::InstrumentCommandNone) {
      state.context = PhraseContext::Fx;
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
  const unsigned short tableMask = window.ButtonMaskForUi2();
  const UiGridSelection selection = window.GridSelectionForUi2();
  if (selection.active) {
    state.selectionVisualRect = UiTableView::SelectionTargetRect(
        selection.left, selection.top, selection.right, selection.bottom);
  }
  state.numberFocus = !selection.active && (tableMask & EPBM_EDIT) != 0U;
  state.editDigit = static_cast<std::uint8_t>(
      std::clamp(window.TableParameterDigitForUi2(), 0, 3));
  state.enterDigitFocus = !state.numberFocus &&
                          (tableMask & EPBM_ENTER) != 0U &&
                          (state.editColumn & 1U) != 0U;
  const std::uint8_t group = state.editColumn / 2U;
  state.activeHeader = group == 0U   ? UiTableHeader::Fx1
                       : group == 1U ? UiTableHeader::Fx2
                                     : UiTableHeader::Fx3;

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

  const FourCC command = group == 0U   ? table.cmd1_[state.editRow]
                         : group == 1U ? table.cmd2_[state.editRow]
                                       : table.cmd3_[state.editRow];
  if (command != FourCC::InstrumentCommandNone) {
    state.context = PhraseContext::Fx;
    CaptureHelpLegend(command, state.contextLead, state.contextTail,
                      state.contextDescription);
  }
}

void UiApplicationRuntime::CaptureInstrument(AppWindow &window,
                                             InstrumentFrameState &state) {
  state = InstrumentFrameState{};
  const InstrumentViewUi2Snapshot snapshot =
      window.InstrumentSnapshotForUi2();
  CopyText(state.number, snapshot.number.data());
  CopyText(state.name, snapshot.name.data());
  switch (snapshot.kind) {
  case InstrumentViewUi2Kind::None:
    state.kind = UiInstrumentKind::None;
    break;
  case InstrumentViewUi2Kind::Sample:
    state.kind = UiInstrumentKind::Sample;
    break;
  case InstrumentViewUi2Kind::Midi:
    state.kind = UiInstrumentKind::Midi;
    break;
  case InstrumentViewUi2Kind::Sid:
    state.kind = UiInstrumentKind::Sid;
    break;
  case InstrumentViewUi2Kind::Opal:
    state.kind = UiInstrumentKind::Opal;
    break;
  }
  state.fieldCount = std::min<std::uint8_t>(
      snapshot.fieldCount, static_cast<std::uint8_t>(state.fields.size()));
  for (std::uint8_t index = 0; index < state.fieldCount; ++index) {
    CopyText(state.fields[index].label, snapshot.fields[index].label.data());
    CopyText(state.fields[index].value, snapshot.fields[index].value.data());
    state.fields[index].y = snapshot.fields[index].y;
  }
  state.operatorCount = std::min<std::uint8_t>(
      snapshot.operatorCount,
      static_cast<std::uint8_t>(state.operators.size()));
  for (std::uint8_t index = 0; index < state.operatorCount; ++index) {
    CopyText(state.operators[index].label,
             snapshot.operators[index].label.data());
    CopyText(state.operators[index].op1,
             snapshot.operators[index].op1.data());
    CopyText(state.operators[index].op2,
             snapshot.operators[index].op2.data());
  }
  state.selectedField = snapshot.selectedField;
  state.selectedOperator = snapshot.selectedOperator;
  state.nameAction = snapshot.nameAction;
  switch (snapshot.focus) {
  case InstrumentViewUi2Focus::Name:
    state.cursor = UiInstrumentCursor::Name;
    break;
  case InstrumentViewUi2Focus::Type:
    state.cursor = UiInstrumentCursor::Type;
    break;
  case InstrumentViewUi2Focus::Field:
    state.cursor = UiInstrumentCursor::Field;
    break;
  case InstrumentViewUi2Focus::Operator1:
    state.cursor = UiInstrumentCursor::Operator1;
    break;
  case InstrumentViewUi2Focus::Operator2:
    state.cursor = UiInstrumentCursor::Operator2;
    break;
  case InstrumentViewUi2Focus::None:
  case InstrumentViewUi2Focus::Unmapped:
    state.cursor = UiInstrumentCursor::None;
    break;
  }

  ViewData &viewData = window.ViewDataForUi2();
  Player *player = Player::GetInstance();
  const bool playing = player != nullptr && player->IsRunning();
  state.selectedTrack = static_cast<std::int8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  state.numberFocus =
      (window.ButtonMaskForUi2() & EPBM_EDIT) != 0U;
  state.power = CurrentPowerState(playing);
  FormatElapsed(player, playing, state.elapsed);
  CaptureTrackNotes(player, playing, state.trackNotes);
}

void UiApplicationRuntime::CaptureDevice(AppWindow &window,
                                         DeviceFrameState &state) {
  state = DeviceFrameState{};
  const DeviceViewUi2Snapshot snapshot = window.DeviceSnapshotForUi2();
  CopyText(state.midiDevice, snapshot.midiDevice.Value());
  CopyText(state.midiSync, snapshot.midiSync.Value());
  CopyText(state.lineOut, snapshot.lineOut.Value());
  CopyText(state.remoteUi, snapshot.remoteUi.Value());
  CopyText(state.resampler, snapshot.resampler.Value());
  CopyText(state.theme, snapshot.theme.data());
  CopyText(state.font, snapshot.font.Value());
  CopyText(state.version, snapshot.version.data());
  std::snprintf(state.volume.data(), state.volume.size(), "%u",
                static_cast<unsigned>(snapshot.volume));
  std::snprintf(state.brightness.data(), state.brightness.size(), "%02X",
                static_cast<unsigned>(snapshot.brightness));

  switch (snapshot.focus) {
  case DeviceViewUi2Focus::MidiDevice:
    state.cursor = UiDeviceCursor::MidiDevice;
    break;
  case DeviceViewUi2Focus::MidiSync:
    state.cursor = UiDeviceCursor::MidiSync;
    break;
  case DeviceViewUi2Focus::LineOut:
    state.cursor = UiDeviceCursor::LineOut;
    break;
  case DeviceViewUi2Focus::RemoteUi:
    state.cursor = UiDeviceCursor::RemoteUi;
    break;
  case DeviceViewUi2Focus::Resampler:
    state.cursor = UiDeviceCursor::Resampler;
    break;
  case DeviceViewUi2Focus::Brightness:
    state.cursor = UiDeviceCursor::Brightness;
    break;
  case DeviceViewUi2Focus::Volume:
    state.cursor = UiDeviceCursor::Volume;
    break;
  case DeviceViewUi2Focus::Theme:
    state.cursor = UiDeviceCursor::Theme;
    break;
  case DeviceViewUi2Focus::UpdateFirmware:
    state.cursor = UiDeviceCursor::UpdateFirmware;
    break;
  case DeviceViewUi2Focus::Unknown:
    state.cursor = UiDeviceCursor::MidiDevice;
    break;
  }

  const DeviceViewUi2Choice choice = snapshot.FocusedChoice();
  state.selectorCount = std::min<std::uint8_t>(
      choice.count,
      static_cast<std::uint8_t>(state.selectorOptions.size()));
  state.selectorCurrent = state.selectorCount == 0U
                              ? 0U
                              : std::min<std::uint8_t>(
                                    choice.current, state.selectorCount - 1U);
  state.selectorWrap = choice.wrap;
  for (std::uint8_t index = 0; index < state.selectorCount; ++index) {
    CopyText(state.selectorOptions[index],
             choice.options == nullptr ? "" : choice.options[index]);
  }

  // Keep UI2's structure identical to DeviceView's platform-specific
  // fieldList_: Config registers both variables on every target.
#if defined(ADV) || defined(NODE)
  state.showLineOut = false;
  state.showVolume = true;
#else
  state.showLineOut = true;
  state.showVolume = false;
#endif
  state.showTheme = true;
  state.showFont = snapshot.font.count != 0U;
#if defined(NODE)
  state.showUpdateFirmware = false;
#else
  state.showUpdateFirmware = true;
#endif
  Player *player = Player::GetInstance();
  const bool playing = player != nullptr && player->IsRunning();
  const PowerFrameState power = CapturePowerState(playing);
  state.power = power.power;
  state.batteryPercent = power.batteryPercent;
  state.batteryPercentValid = power.batteryPercentValid;
}

void UiApplicationRuntime::CaptureBrowser(AppWindow &window,
                                          BrowserFrameState &state) {
  state = BrowserFrameState{};
  state.snapshot = window.BrowserSnapshotForUi2();
  Player *player = Player::GetInstance();
  const bool playing = player != nullptr && player->IsRunning();
  state.power = CurrentPowerState(playing);
}

void UiApplicationRuntime::CaptureGroove(AppWindow &window,
                                         GrooveFrameState &state) {
  state = GrooveFrameState{};
  ViewData &viewData = window.ViewDataForUi2();
  Player *player = Player::GetInstance();

  const int grooveNumber =
      std::clamp(viewData.currentGroove_, 0, MAX_GROOVES - 1);
  hex2char(static_cast<std::uint8_t>(grooveNumber), state.number.data());
  const unsigned char *steps =
      Groove::GetInstance()->GetGrooveData(grooveNumber);
  std::copy_n(steps, state.steps.size(), state.steps.begin());
  state.editRow = static_cast<std::uint8_t>(
      std::clamp(window.GrooveRowForUi2(), 0, 15));

  const bool playing = player != nullptr && player->IsRunning();
  state.power = CurrentPowerState(playing);
}

void UiApplicationRuntime::CaptureMixer(AppWindow &window,
                                        MixerFrameState &state) {
  state = MixerFrameState{};
  for (auto &channel : state.vuLevelTop)
    channel = {UiMixerView::kMeterHeight, UiMixerView::kMeterHeight};

  ViewData &viewData = window.ViewDataForUi2();
  Project &project = *viewData.project_;
  Player *player = Player::GetInstance();
  state.selectedChannel = static_cast<std::int8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT));

  for (std::uint8_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    FormatVolume(project.GetChannelVolume(channel), state.volumes[channel]);
  }
  FormatVolume(project.GetMasterVolume(), state.volumes[SONG_CHANNEL_COUNT]);

  const bool playing = player != nullptr && player->IsRunning();
  state.power = CurrentPowerState(playing);
  if (player == nullptr)
    return;

  const auto captureStereoLevel = [&](std::uint8_t channel,
                                      std::uint32_t level) {
    state.vuLevelTop[channel][0] = VuTopFromAmplitude(
        static_cast<std::uint16_t>(level >> 16U));
    state.vuLevelTop[channel][1] =
        VuTopFromAmplitude(static_cast<std::uint16_t>(level & 0xFFFFU));
  };
  const etl::array<stereosample, SONG_CHANNEL_COUNT> *levels =
      player->GetMixerLevels();
  if (levels != nullptr) {
    for (std::uint8_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
      if (!player->IsChannelMuted(channel)) {
        captureStereoLevel(channel,
                           static_cast<std::uint32_t>(levels->at(channel)));
      }
    }
  }
  captureStereoLevel(SONG_CHANNEL_COUNT,
                     static_cast<std::uint32_t>(player->GetMasterLevel()));
}

} // namespace ui2
