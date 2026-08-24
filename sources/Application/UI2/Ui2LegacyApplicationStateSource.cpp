/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2LegacyApplicationStateSource.h"

#include "Application/Views/ViewData.h"

#include "Application/AppWindow.h"
#include "Application/Model/Groove.h"
#include "Application/Model/Project.h"
#include "Application/Model/Table.h"
#include "Application/Player/Player.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "Application/Views/DeviceView.h"
#include "Application/Views/InstrumentView.h"
#include "Application/Views/ProjectView.h"
#include "Application/Views/UiGridSelection.h"
#include "System/System/System.h"
#include "UI2/Views/Song/UiSongView.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace ui2 {
namespace {

constexpr std::uint8_t VuTopFromAmplitude(std::uint16_t amplitude) {
  if (amplitude < 33U)
    return 153U;
  if (amplitude >= 32700U)
    return 0U;
  std::uint8_t exponent = 0;
  std::uint16_t value = amplitude;
  while (value > 1U) {
    value >>= 1U;
    ++exponent;
  }
  const std::uint32_t base = 1U << exponent;
  const std::uint32_t fraction =
      ((static_cast<std::uint32_t>(amplitude) - base) * 16U) / base;
  const std::uint32_t steps =
      (static_cast<std::uint32_t>(exponent - 5U) * 16U) + fraction;
  const std::uint32_t active = (steps * 153U) / 160U;
  return static_cast<std::uint8_t>(153U - active);
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

UiApplicationPage UiLegacyApplicationStateSource::ActivePage() const {
  if (window_.IsCurrentViewForUi2(VT_SONG))
    return UiApplicationPage::Song;
  if (window_.IsCurrentViewForUi2(VT_CHAIN))
    return UiApplicationPage::Chain;
  if (window_.IsCurrentViewForUi2(VT_PHRASE))
    return UiApplicationPage::Phrase;
  if (window_.IsCurrentViewForUi2(VT_TABLE) ||
      window_.IsCurrentViewForUi2(VT_TABLE2))
    return UiApplicationPage::Table;
  if (window_.IsCurrentViewForUi2(VT_INSTRUMENT))
    return UiApplicationPage::Instrument;
  if (window_.IsCurrentViewForUi2(VT_PROJECT))
    return UiApplicationPage::Project;
  if (window_.IsCurrentViewForUi2(VT_DEVICE))
    return UiApplicationPage::Device;
  if (window_.IsCurrentViewForUi2(VT_THEME) ||
      window_.IsCurrentViewForUi2(VT_SELECTTHEME))
    return UiApplicationPage::Theme;
  if (window_.IsCurrentViewForUi2(VT_IMPORT) ||
      window_.IsCurrentViewForUi2(VT_INSTRUMENT_IMPORT) ||
      window_.IsCurrentViewForUi2(VT_SELECTPROJECT) ||
      window_.IsCurrentViewForUi2(VT_THEME_IMPORT))
    return UiApplicationPage::Browser;
  if (window_.IsCurrentViewForUi2(VT_GROOVE))
    return UiApplicationPage::Groove;
  if (window_.IsCurrentViewForUi2(VT_MIXER))
    return UiApplicationPage::Mixer;
  if (window_.IsCurrentViewForUi2(VT_SAMPLE_EDITOR))
    return UiApplicationPage::SampleEditor;
  if (window_.IsCurrentViewForUi2(VT_SAMPLE_SLICES))
    return UiApplicationPage::SampleSlices;
  if (window_.IsCurrentViewForUi2(VT_RECORD))
    return UiApplicationPage::Record;
  return UiApplicationPage::None;
}

std::uint32_t UiLegacyApplicationStateSource::NowMs() const {
  System *system = System::GetInstance();
  return system == nullptr ? 0U : system->Millis();
}

UiApplicationBatteryState UiLegacyApplicationStateSource::ReadBattery() const {
  System *system = System::GetInstance();
  if (system == nullptr)
    return {};
  BatteryState battery{};
  system->GetBatteryState(battery);
  if (battery.error)
    return {};
  return {
      .percentage = std::min<std::uint8_t>(battery.percentage, 100U),
      .available = true,
      .charging = battery.charging,
  };
}

bool UiLegacyApplicationStateSource::HasDialog() const {
  return window_.HasModalForUi2();
}

Ui2DialogSnapshot UiLegacyApplicationStateSource::DialogSnapshot() const {
  return window_.ModalSnapshotForUi2();
}

std::uint32_t UiLegacyApplicationStateSource::DialogInstanceId() const {
  return window_.ModalInstanceIdForUi2();
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureSong(UiSongFrameState &state) {
  state = UiSongFrameState{};
  ViewData &viewData = window_.ViewDataForUi2();
  Project &project = *viewData.project_;
  Player *player = Player::GetInstance();

  project.GetProjectName(state.name.data());
  state.editTrack = static_cast<std::uint8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  state.editRow = static_cast<std::uint8_t>(std::clamp(viewData.songY_, 0, 15));

  const int firstRow = std::clamp(viewData.songOffset_, 0, SONG_ROW_COUNT - 16);
  state.rowOffset = static_cast<std::uint8_t>(firstRow);
  const UiGridSelection selection = window_.GridSelectionForUi2();
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
  state.liveMode = player != nullptr && player->GetSequencerMode() == SM_LIVE;
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
  return {.active = state.playing};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureChain(UiChainFrameState &state) {
  state = UiChainFrameState{};
  ViewData &viewData = window_.ViewDataForUi2();
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
  const UiGridSelection selection = window_.GridSelectionForUi2();
  if (selection.active) {
    state.selectionVisualRect = UiChainView::SelectionTargetRect(
        selection.left, selection.top, selection.right, selection.bottom);
  }
  state.numberFocus =
      !selection.active && (window_.ButtonMaskForUi2() & EPBM_EDIT) != 0U;

  const int base = static_cast<int>(chainNumber) * PHRASES_PER_CHAIN;
  for (std::uint8_t row = 0; row < PHRASES_PER_CHAIN; ++row) {
    state.phrases[row] = viewData.song_->chain_.data_[base + row];
    state.transposes[row] = viewData.song_->chain_.transpose_[base + row];
  }

  const bool playing = player != nullptr && player->IsRunning();
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
  state.vuLevelTop[1] =
      scaleVu(VuTopFromAmplitude(static_cast<std::uint16_t>(level & 0xFFFFU)));
  return {.active = playing};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CapturePhrase(UiPhraseFrameState &state) {
  state = UiPhraseFrameState{};
  ViewData &viewData = window_.ViewDataForUi2();
  Project &project = *viewData.project_;
  Phrase &phrase = viewData.song_->phrase_;
  Player *player = Player::GetInstance();

  const std::uint8_t phraseNumber = static_cast<std::uint8_t>(
      std::clamp(viewData.currentPhrase_, 0, PHRASE_COUNT - 1));
  hex2char(phraseNumber, state.number.data());
  state.editRow = static_cast<std::uint8_t>(
      std::clamp(window_.PhraseRowForUi2(), 0, STEPS_PER_PHRASE - 1));
  state.editColumn =
      static_cast<std::uint8_t>(std::clamp(window_.PhraseColumnForUi2(), 0, 5));
  state.selectedTrack = static_cast<std::int8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  const unsigned short phraseMask = window_.ButtonMaskForUi2();
  const UiGridSelection selection = window_.GridSelectionForUi2();
  if (selection.active) {
    state.selectionVisualRect = UiPhraseView::SelectionTargetRect(
        selection.left, selection.top, selection.right, selection.bottom);
  }
  state.numberFocus = !selection.active && (phraseMask & EPBM_EDIT) != 0U;
  state.editDigit = static_cast<std::uint8_t>(
      std::clamp(window_.PhraseParameterDigitForUi2(), 0, 3));
  state.enterDigitFocus = !state.numberFocus &&
                          (phraseMask & EPBM_ENTER) != 0U &&
                          (state.editColumn == 3U || state.editColumn == 5U);
  state.activeHeader = state.editColumn == 0U   ? UiPhraseHeader::Note
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
      state.context = UiPhraseContext::Instrument;
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
      state.context = UiPhraseContext::Fx;
      CaptureHelpLegend(command, state.contextLead, state.contextTail,
                        state.contextDescription);
    }
  }
  return {.active = playing};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureTable(UiTableFrameState &state) {
  state = UiTableFrameState{};
  ViewData &viewData = window_.ViewDataForUi2();
  Player *player = Player::GetInstance();
  const int tableNumber =
      std::clamp(viewData.currentTable_, 0, TABLE_COUNT - 1);
  Table &table = TableHolder::GetInstance()->GetTable(tableNumber);

  state.number[0] = window_.IsCurrentViewForUi2(VT_TABLE2) ? 'I' : 'P';
  hex2char(static_cast<std::uint8_t>(tableNumber), state.number.data() + 1);
  state.editRow = static_cast<std::uint8_t>(
      std::clamp(window_.TableRowForUi2(), 0, TABLE_STEPS - 1));
  state.editColumn = static_cast<std::uint8_t>(
      std::clamp(window_.TableColumnForUi2(), 0, TABLE_COLUMNS * 2 - 1));
  state.selectedTrack = static_cast<std::int8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  const unsigned short tableMask = window_.ButtonMaskForUi2();
  const UiGridSelection selection = window_.GridSelectionForUi2();
  if (selection.active) {
    state.selectionVisualRect = UiTableView::SelectionTargetRect(
        selection.left, selection.top, selection.right, selection.bottom);
  }
  state.numberFocus = !selection.active && (tableMask & EPBM_EDIT) != 0U;
  state.editDigit = static_cast<std::uint8_t>(
      std::clamp(window_.TableParameterDigitForUi2(), 0, 3));
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
  FormatElapsed(player, playing, state.elapsed);
  CaptureTrackNotes(player, playing, state.trackNotes);

  const FourCC command = group == 0U   ? table.cmd1_[state.editRow]
                         : group == 1U ? table.cmd2_[state.editRow]
                                       : table.cmd3_[state.editRow];
  if (command != FourCC::InstrumentCommandNone) {
    state.context = UiPhraseContext::Fx;
    CaptureHelpLegend(command, state.contextLead, state.contextTail,
                      state.contextDescription);
  }
  return {.active = playing};
}

UiApplicationActivityState UiLegacyApplicationStateSource::CaptureInstrument(
    UiInstrumentFrameState &state) {
  state = UiInstrumentFrameState{};
  const InstrumentViewUi2Snapshot snapshot = window_.InstrumentSnapshotForUi2();
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
  state.operatorCount =
      std::min<std::uint8_t>(snapshot.operatorCount,
                             static_cast<std::uint8_t>(state.operators.size()));
  for (std::uint8_t index = 0; index < state.operatorCount; ++index) {
    CopyText(state.operators[index].label,
             snapshot.operators[index].label.data());
    CopyText(state.operators[index].op1, snapshot.operators[index].op1.data());
    CopyText(state.operators[index].op2, snapshot.operators[index].op2.data());
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

  ViewData &viewData = window_.ViewDataForUi2();
  Player *player = Player::GetInstance();
  const bool playing = player != nullptr && player->IsRunning();
  state.selectedTrack = static_cast<std::int8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT - 1));
  state.numberFocus = (window_.ButtonMaskForUi2() & EPBM_EDIT) != 0U;
  FormatElapsed(player, playing, state.elapsed);
  CaptureTrackNotes(player, playing, state.trackNotes);
  return {.active = playing};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureProject(UiProjectFrameState &state) {
  state = UiProjectFrameState{};
  const ProjectViewUi2Snapshot snapshot = window_.ProjectSnapshotForUi2();
  CopyText(state.name, snapshot.name.data());
  std::snprintf(state.tempo.data(), state.tempo.size(), "%d / %02X",
                static_cast<int>(snapshot.tempo),
                static_cast<unsigned>(snapshot.tempo) & 0xFFU);
  std::snprintf(state.transpose.data(), state.transpose.size(), "%02d",
                static_cast<int>(snapshot.transpose));
  CopyText(state.scale, snapshot.scale.Value());
  CopyText(state.root, snapshot.root.Value());
  state.nameAction = std::min<std::uint8_t>(snapshot.nameAction, 3U);

  switch (snapshot.focus) {
  case ProjectViewUi2Focus::Name:
  case ProjectViewUi2Focus::Browse:
  case ProjectViewUi2Focus::Save:
  case ProjectViewUi2Focus::NewProject:
  case ProjectViewUi2Focus::RandomName:
    state.cursor = UiProjectCursor::Name;
    break;
  case ProjectViewUi2Focus::Tempo:
  case ProjectViewUi2Focus::MasterVolume:
    state.cursor = UiProjectCursor::Tempo;
    break;
  case ProjectViewUi2Focus::Transpose:
    state.cursor = UiProjectCursor::Transpose;
    break;
  case ProjectViewUi2Focus::Scale:
    state.cursor = UiProjectCursor::Scale;
    break;
  case ProjectViewUi2Focus::Root:
    state.cursor = UiProjectCursor::Root;
    break;
  case ProjectViewUi2Focus::SamplePool:
    state.cursor = UiProjectCursor::SamplePool;
    break;
  case ProjectViewUi2Focus::PurgeSamples:
    state.cursor = UiProjectCursor::Samples;
    break;
  case ProjectViewUi2Focus::PurgeInstruments:
    state.cursor = UiProjectCursor::Instruments;
    break;
  case ProjectViewUi2Focus::RenderMixdown:
    state.cursor = UiProjectCursor::Render;
    state.renderOption = 0U;
    break;
  case ProjectViewUi2Focus::RenderStems:
    state.cursor = UiProjectCursor::Render;
    state.renderOption = 1U;
    break;
  case ProjectViewUi2Focus::Unknown:
    state.cursor = UiProjectCursor::Name;
    break;
  }

  Player *player = Player::GetInstance();
  const bool playing = player != nullptr && player->IsRunning();
  return {.active = playing};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureDevice(UiDeviceFrameState &state) {
  state = UiDeviceFrameState{};
  const DeviceViewUi2Snapshot snapshot = window_.DeviceSnapshotForUi2();
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
      choice.count, static_cast<std::uint8_t>(state.selectorOptions.size()));
  state.selectorCurrent =
      state.selectorCount == 0U
          ? 0U
          : std::min<std::uint8_t>(choice.current, state.selectorCount - 1U);
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
  return {.active = playing};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureTheme(UiThemeFrameState &state) {
  state = UiThemeFrameState{};
  const ThemeViewUi2Snapshot snapshot = window_.ThemeSnapshotForUi2();
  Player *player = Player::GetInstance();
  const bool playing = player != nullptr && player->IsRunning();
  state.view = MakeUiThemeViewState(snapshot);
  if (snapshot.focus == ThemeViewUi2Focus::Font ||
      snapshot.focus == ThemeViewUi2Focus::Unknown) {
    // Font has no independent ViewType/controller yet. Keep the Theme page
    // honest by showing no fabricated NAME/color cursor for that legacy focus.
    state.view.selectedColor = 127;
    state.view.cursorInkVisible = false;
  }
  state.colors = snapshot.colors;
  state.colorsValid = snapshot.colorsValid;
  return {.active = playing};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureBrowser(UiBrowserFrameState &state) {
  state = UiBrowserFrameState{};
  state.snapshot = window_.BrowserSnapshotForUi2();
  Player *player = Player::GetInstance();
  const bool playing = player != nullptr && player->IsRunning();
  return {.active = playing};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureGroove(UiGrooveFrameState &state) {
  state = UiGrooveFrameState{};
  ViewData &viewData = window_.ViewDataForUi2();
  Player *player = Player::GetInstance();

  const int grooveNumber =
      std::clamp(viewData.currentGroove_, 0, MAX_GROOVES - 1);
  hex2char(static_cast<std::uint8_t>(grooveNumber), state.number.data());
  const unsigned char *steps =
      Groove::GetInstance()->GetGrooveData(grooveNumber);
  std::copy_n(steps, state.steps.size(), state.steps.begin());
  state.editRow =
      static_cast<std::uint8_t>(std::clamp(window_.GrooveRowForUi2(), 0, 15));

  const bool playing = player != nullptr && player->IsRunning();
  return {.active = playing};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureMixer(UiMixerFrameState &state) {
  state = UiMixerFrameState{};
  for (auto &channel : state.vuLevelTop)
    channel = {UiMixerView::kMeterHeight, UiMixerView::kMeterHeight};

  ViewData &viewData = window_.ViewDataForUi2();
  Project &project = *viewData.project_;
  Player *player = Player::GetInstance();
  state.selectedChannel = static_cast<std::int8_t>(
      std::clamp(viewData.songX_, 0, SONG_CHANNEL_COUNT));

  for (std::uint8_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    FormatVolume(project.GetChannelVolume(channel), state.volumes[channel]);
  }
  FormatVolume(project.GetMasterVolume(), state.volumes[SONG_CHANNEL_COUNT]);

  const bool playing = player != nullptr && player->IsRunning();
  if (player == nullptr)
    return {.active = false};

  const auto captureStereoLevel = [&](std::uint8_t channel,
                                      std::uint32_t level) {
    state.vuLevelTop[channel][0] =
        VuTopFromAmplitude(static_cast<std::uint16_t>(level >> 16U));
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
  return {.active = playing};
}

UiApplicationActivityState UiLegacyApplicationStateSource::CaptureSampleEditor(
    UiSampleEditorFrameState &state) {
  const SampleEditorViewUi2Snapshot snapshot =
      window_.SampleEditorSnapshotForUi2();
  const unsigned short mask = window_.ButtonMaskForUi2();
  state =
      MakeUiSampleEditorControllerState(snapshot, UiPowerState::BatteryNormal,
                                        {.enterHeld = (mask & EPBM_ENTER) != 0U,
                                         .editHeld = (mask & EPBM_EDIT) != 0U});
  return {.active = snapshot.playing};
}

UiApplicationActivityState UiLegacyApplicationStateSource::CaptureSampleSlices(
    UiSampleSlicesFrameState &state) {
  const SampleSlicesViewUi2Snapshot snapshot =
      window_.SampleSlicesSnapshotForUi2();
  const unsigned short mask = window_.ButtonMaskForUi2();
  state =
      MakeUiSampleSlicesControllerState(snapshot, UiPowerState::BatteryNormal,
                                        {.enterHeld = (mask & EPBM_ENTER) != 0U,
                                         .editHeld = (mask & EPBM_EDIT) != 0U});
  return {.active = snapshot.previewActive};
}

UiApplicationActivityState
UiLegacyApplicationStateSource::CaptureRecord(UiRecordFrameState &state) {
  state = UiRecordFrameState{};
  state.snapshot = window_.RecordSnapshotForUi2();
  Player *player = Player::GetInstance();
  const bool playing = player != nullptr && player->IsRunning();
  state.cursorInkVisible = state.snapshot.focus != RecordViewUi2Focus::Unknown;
  return {.active = playing};
}

} // namespace ui2
