/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2NativeApplicationStateSource.h"

#include "Application/Model/Groove.h"
#include "Application/Model/Scale.h"
#include "Application/Model/Table.h"
#include "Application/Player/Player.h"
#include "Application/Session/TrackerApplicationSession.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "System/System/System.h"
#include "UI2/Views/Chain/UiChainView.h"
#include "UI2/Views/Song/UiSongView.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace ui2 {
namespace {

template <std::size_t Size>
void CopyText(std::array<char, Size> &destination, const char *source) {
  destination.fill(0);
  if (source != nullptr && Size > 0U)
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

bool PlayerRunning() {
  Player *player = Player::GetInstance();
  return player != nullptr && player->IsRunning();
}

void FormatElapsed(std::array<char, 6> &elapsed) {
  Player *player = Player::GetInstance();
  const int seconds = PlayerRunning()
                          ? std::max(0, static_cast<int>(player->GetPlayTime()))
                          : 0;
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
  if (pitch[1] == ' ')
    std::snprintf(text.data(), text.size(), "%c%d", pitch[0], octave);
  else
    std::snprintf(text.data(), text.size(), "%c%c%d", pitch[0], pitch[1],
                  octave);
}

void FormatCommand(FourCC command, std::array<char, 4> &text) {
  const char *source = command.c_str();
  if (source != nullptr && source[0] != '\0' && source[1] != '\0' &&
      source[2] != '\0' && source[3] == '\0')
    CopyUpper(text, source);
  else
    CopyText(text, "???");
}

void FormatVolume(int value, std::array<char, 4> &text) {
  std::snprintf(text.data(), text.size(), "%d", std::clamp(value, 0, 999));
}

UiDeviceCursor DeviceCursorFor(Ui2DeviceField field) {
  switch (field) {
  case Ui2DeviceField::MidiDevice:
    return UiDeviceCursor::MidiDevice;
  case Ui2DeviceField::MidiSync:
    return UiDeviceCursor::MidiSync;
  case Ui2DeviceField::RemoteUi:
    return UiDeviceCursor::RemoteUi;
  case Ui2DeviceField::Resampler:
    return UiDeviceCursor::Resampler;
  case Ui2DeviceField::LineOut:
    return UiDeviceCursor::LineOut;
  case Ui2DeviceField::Volume:
    return UiDeviceCursor::Volume;
  case Ui2DeviceField::Brightness:
    return UiDeviceCursor::Brightness;
  case Ui2DeviceField::Theme:
    return UiDeviceCursor::Theme;
  case Ui2DeviceField::Font:
    return UiDeviceCursor::Font;
  case Ui2DeviceField::UpdateFirmware:
  case Ui2DeviceField::Count:
    return UiDeviceCursor::UpdateFirmware;
  }
  return UiDeviceCursor::MidiDevice;
}

template <typename Notes> void CaptureTrackNotes(Notes &notes) {
  Player *player = Player::GetInstance();
  const bool playing = PlayerRunning();
  for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
    if (!playing) {
      CopyText(notes[track], "--");
      continue;
    }
    const char *pitch = player->GetPlayedNote(track);
    const char *octave = player->GetPlayedOctive(track);
    if (pitch[0] == ' ' || octave[1] == '-') {
      CopyText(notes[track], "--");
    } else if (pitch[1] == ' ') {
      std::snprintf(notes[track].data(), notes[track].size(),
                    octave[0] == '-' ? "%c-%c" : "%c%c", pitch[0],
                    octave[1]);
    } else {
      std::snprintf(notes[track].data(), notes[track].size(),
                    octave[0] == '-' ? "%c%c-%c" : "%c%c%c", pitch[0],
                    pitch[1], octave[1]);
    }
  }
}

template <std::size_t LeadSize, std::size_t TailSize,
          std::size_t DescriptionSize>
void CaptureHelp(FourCC command, std::array<char, LeadSize> &lead,
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
  return static_cast<std::uint8_t>(153U - (steps * 153U) / 160U);
}

std::array<std::uint8_t, 2> MasterVu(std::uint8_t height = 153U) {
  Player *player = Player::GetInstance();
  const std::uint32_t level =
      PlayerRunning() ? static_cast<std::uint32_t>(player->GetMasterLevel())
                      : 0U;
  const auto top = [height](std::uint16_t amplitude) {
    const std::uint8_t songTop = VuTopFromAmplitude(amplitude);
    return static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(songTop) * height + 76U) / 153U);
  };
  return {top(static_cast<std::uint16_t>(level >> 16U)),
          top(static_cast<std::uint16_t>(level & 0xFFFFU))};
}

} // namespace

UiApplicationPage Ui2NativeApplicationStateSource::ActivePage() const {
  return activePage_;
}

std::uint32_t Ui2NativeApplicationStateSource::NowMs() const {
  System *system = System::GetInstance();
  return system == nullptr ? 0U : system->Millis();
}

UiApplicationBatteryState
Ui2NativeApplicationStateSource::ReadBattery() const {
  System *system = System::GetInstance();
  if (system == nullptr)
    return {};
  BatteryState battery{};
  system->GetBatteryState(battery);
  if (battery.error)
    return {};
  return {.percentage = std::min<std::uint8_t>(battery.percentage, 100U),
          .available = true,
          .charging = battery.charging};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureSong(UiSongFrameState &state) {
  state = {};
  const Ui2SongController &controller = tracker_.Hub().Song();
  Project &project = session_.ProjectModel();
  TrackerSessionState &editor = session_.EditorState();
  project.GetProjectName(state.name.data());
  state.editTrack = controller.Track();
  state.editRow = controller.VisibleRow();
  state.rowOffset = controller.RowOffset();
  state.adjustmentFocus =
      (controller.HeldMask() & TrackerActionBit(TrackerAction::Enter)) != 0U;
  for (std::uint8_t row = 0; row < 16U; ++row) {
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
      state.rows[row][track] = project.song_.data_[
          (controller.RowOffset() + row) * SONG_CHANNEL_COUNT + track];
    }
  }
  if (controller.Selection().active) {
    const auto &selection = controller.Selection();
    state.selectionVisualRect = UiSongView::SelectionTargetRect(
        selection.Left(), selection.Top(), selection.Right(),
        selection.Bottom(), controller.RowOffset());
  }
  state.playing = PlayerRunning();
  state.liveMode = controller.LiveMode();
  FormatElapsed(state.elapsed);
  CaptureTrackNotes(state.notes);
  for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
    const int visible = editor.songPlayPos_[track] - controller.RowOffset();
    state.playbackRows[track] =
        state.playing && visible >= 0 && visible < 16
            ? static_cast<std::int8_t>(visible)
            : -1;
  }
  state.vuLevelTop = MasterVu();
  return {.active = state.playing};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureChain(UiChainFrameState &state) {
  state = {};
  const Ui2ChainController &controller = tracker_.Hub().Chain();
  Song &song = session_.ProjectModel().song_;
  hex2char(controller.Number(), state.number.data());
  state.editRow = controller.Row();
  state.editColumn = controller.Column();
  state.selectedTrack = controller.SelectedTrack();
  state.numberFocus = controller.NumberFocus();
  state.adjustmentFocus =
      !state.numberFocus &&
      (controller.HeldMask() & TrackerActionBit(TrackerAction::Enter)) != 0U;
  if (controller.Selection().active) {
    const auto &selection = controller.Selection();
    state.selectionVisualRect = UiChainView::SelectionTargetRect(
        selection.Left(), selection.Top(), selection.Right(),
        selection.Bottom());
  }
  const int base = controller.Number() * PHRASES_PER_CHAIN;
  std::copy_n(song.chain_.data_ + base, 16, state.phrases.begin());
  std::copy_n(song.chain_.transpose_ + base, 16, state.transposes.begin());
  FormatElapsed(state.elapsed);
  CaptureTrackNotes(state.trackNotes);
  state.vuLevelTop = MasterVu(UiChainView::kMeterHeight);
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CapturePhrase(UiPhraseFrameState &state) {
  state = {};
  const Ui2PhraseController &controller = tracker_.Hub().Phrase();
  Project &project = session_.ProjectModel();
  Phrase &phrase = project.song_.phrase_;
  hex2char(controller.Number(), state.number.data());
  state.editRow = controller.Row();
  state.editColumn = controller.Column();
  state.editDigit = controller.ParameterDigit();
  state.selectedTrack = controller.SelectedTrack();
  state.numberFocus = controller.NumberFocus();
  state.enterDigitFocus = controller.EnterDigitFocus();
  state.adjustmentFocus =
      !state.numberFocus && !state.enterDigitFocus &&
      (controller.HeldMask() & TrackerActionBit(TrackerAction::Enter)) != 0U;
  state.activeHeader = controller.Column() == 0U   ? UiPhraseHeader::Note
                       : controller.Column() == 1U ? UiPhraseHeader::Instrument
                       : controller.Column() <= 3U ? UiPhraseHeader::Fx1
                                                   : UiPhraseHeader::Fx2;
  if (controller.Selection().active) {
    const auto &selection = controller.Selection();
    state.selectionVisualRect = UiPhraseView::SelectionTargetRect(
        selection.Left(), selection.Top(), selection.Right(),
        selection.Bottom());
  }
  const int base = controller.Number() * STEPS_PER_PHRASE;
  for (std::uint8_t row = 0; row < STEPS_PER_PHRASE; ++row) {
    const int index = base + row;
    FormatNote(phrase.note_[index], state.rows[row].note);
    if (phrase.instr_[index] == 0xFFU)
      CopyText(state.rows[row].instrument, "I--");
    else {
      state.rows[row].instrument[0] = 'I';
      hex2char(phrase.instr_[index], state.rows[row].instrument.data() + 1);
    }
    FormatCommand(phrase.cmd1_[index], state.rows[row].fx1);
    hexshort2char(phrase.param1_[index], state.rows[row].parameter1.data());
    FormatCommand(phrase.cmd2_[index], state.rows[row].fx2);
    hexshort2char(phrase.param2_[index], state.rows[row].parameter2.data());
  }
  FormatElapsed(state.elapsed);
  CaptureTrackNotes(state.trackNotes);
  const int selected = base + controller.Row();
  if (controller.Column() <= 1U) {
    std::uint8_t instrument = phrase.instr_[selected];
    if (controller.Column() == 0U && phrase.note_[selected] != NO_NOTE) {
      for (int row = controller.Row(); row >= 0; --row) {
        if (phrase.instr_[base + row] != 0xFFU) {
          instrument = phrase.instr_[base + row];
          break;
        }
      }
    }
    const auto &list = project.GetInstrumentBank()->InstrumentsList();
    if (instrument < list.size() && list[instrument] != nullptr) {
      state.context = UiPhraseContext::Instrument;
      std::snprintf(state.contextLead.data(), state.contextLead.size(),
                    "INSTRUMENT %02X", instrument);
      CopyUpper(state.contextTail,
                list[instrument]->GetDisplayName().c_str());
    }
  } else {
    const FourCC command = controller.Column() <= 3U ? phrase.cmd1_[selected]
                                                     : phrase.cmd2_[selected];
    if (command != FourCC::InstrumentCommandNone) {
      state.context = UiPhraseContext::Fx;
      CaptureHelp(command, state.contextLead, state.contextTail,
                  state.contextDescription);
    }
  }
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureTable(UiTableFrameState &state) {
  state = {};
  const Ui2TableController &controller = tracker_.Hub().Table();
  state.number[0] = controller.Page() == Ui2TrackerPage::InstrumentTable ? 'I'
                                                                         : 'P';
  hex2char(controller.Number(), state.number.data() + 1);
  state.editRow = controller.Row();
  state.editColumn = controller.Column();
  state.editDigit = controller.ParameterDigit();
  state.selectedTrack = controller.SelectedTrack();
  state.numberFocus = controller.NumberFocus();
  state.enterDigitFocus = controller.EnterDigitFocus();
  state.adjustmentFocus =
      !state.numberFocus && !state.enterDigitFocus &&
      (controller.HeldMask() & TrackerActionBit(TrackerAction::Enter)) != 0U;
  state.activeHeader = controller.Column() < 2U   ? UiTableHeader::Fx1
                       : controller.Column() < 4U ? UiTableHeader::Fx2
                                                   : UiTableHeader::Fx3;
  if (controller.Selection().active) {
    const auto &selection = controller.Selection();
    state.selectionVisualRect = UiTableView::SelectionTargetRect(
        selection.Left(), selection.Top(), selection.Right(),
        selection.Bottom());
  }
  Table &table = TableHolder::GetInstance()->GetTable(controller.Number());
  for (std::uint8_t row = 0; row < TABLE_STEPS; ++row) {
    FormatCommand(table.cmd1_[row], state.rows[row].fx1);
    hexshort2char(table.param1_[row], state.rows[row].parameter1.data());
    FormatCommand(table.cmd2_[row], state.rows[row].fx2);
    hexshort2char(table.param2_[row], state.rows[row].parameter2.data());
    FormatCommand(table.cmd3_[row], state.rows[row].fx3);
    hexshort2char(table.param3_[row], state.rows[row].parameter3.data());
  }
  FormatElapsed(state.elapsed);
  CaptureTrackNotes(state.trackNotes);
  const std::uint8_t group = controller.Column() / 2U;
  const FourCC command = group == 0U   ? table.cmd1_[controller.Row()]
                         : group == 1U ? table.cmd2_[controller.Row()]
                                       : table.cmd3_[controller.Row()];
  if (command != FourCC::InstrumentCommandNone) {
    state.context = UiPhraseContext::Fx;
    CaptureHelp(command, state.contextLead, state.contextTail,
                state.contextDescription);
  }
  return {.active = PlayerRunning()};
}

UiApplicationActivityState Ui2NativeApplicationStateSource::CaptureInstrument(
    UiInstrumentFrameState &state) {
  state = {};
  const TrackerSessionState &editor = session_.EditorState();
  hex2char(static_cast<std::uint8_t>(editor.currentInstrumentID_),
           state.number.data());
  state.selectedTrack = editor.songX_;
  FormatElapsed(state.elapsed);
  CaptureTrackNotes(state.trackNotes);
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureProject(UiProjectFrameState &state) {
  state = {};
  Project &model = session_.ProjectModel();
  model.GetProjectName(state.name.data());
  std::snprintf(state.tempo.data(), state.tempo.size(), "%d",
                model.GetTempo());
  std::snprintf(state.transpose.data(), state.transpose.size(), "%02d",
                model.GetTranspose());
  CopyUpper(state.scale, scaleNames[model.GetScale()]);
  CopyUpper(state.root, noteNames[model.GetScaleRoot()]);
  state.cursor = static_cast<UiProjectCursor>(project_.ContentCursor());
  state.nameAction = static_cast<std::uint8_t>(project_.NameAction());
  state.sampleAction = static_cast<std::uint8_t>(project_.SampleAction());
  state.renderOption = static_cast<std::uint8_t>(project_.RenderSelection());
  state.enterHeld = project_.EnterHeld();
  constexpr std::uint16_t visible = 5U;
  const auto selectorWindow = [&](std::uint16_t current,
                                  std::uint16_t count) {
    const std::uint16_t maximum = static_cast<std::uint16_t>(count - 1U);
    const std::uint16_t start = static_cast<std::uint16_t>(
        std::min<std::uint16_t>(current > 2U ? current - 2U : 0U,
                                maximum >= visible - 1U
                                    ? maximum - (visible - 1U)
                                    : 0U));
    state.selectorCount =
        static_cast<std::uint8_t>(std::min<std::uint16_t>(visible, count));
    state.selectorCurrent = static_cast<std::uint8_t>(current - start);
    return start;
  };
  if (state.cursor == UiProjectCursor::Tempo) {
    const std::uint16_t count = MAX_TEMPO - MIN_TEMPO + 1U;
    const std::uint16_t current = model.GetTempo() - MIN_TEMPO;
    const std::uint16_t start = selectorWindow(current, count);
    for (std::uint8_t index = 0; index < state.selectorCount; ++index)
      std::snprintf(state.selectorOptions[index].data(),
                    state.selectorOptions[index].size(), "%u",
                    static_cast<unsigned>(MIN_TEMPO + start + index));
  } else if (state.cursor == UiProjectCursor::Transpose) {
    constexpr std::int16_t minimum = -48;
    constexpr std::uint16_t count = 97U;
    const std::uint16_t current =
        static_cast<std::uint16_t>(model.GetTranspose() - minimum);
    const std::uint16_t start = selectorWindow(current, count);
    for (std::uint8_t index = 0; index < state.selectorCount; ++index)
      std::snprintf(state.selectorOptions[index].data(),
                    state.selectorOptions[index].size(), "%02d",
                    minimum + start + index);
  } else if (state.cursor == UiProjectCursor::Scale) {
    state.selectorCount = 1U;
    state.selectorCurrent = 0U;
    state.selectorWrap = true;
    CopyUpper(state.selectorOptions[0], scaleNames[model.GetScale()]);
  } else if (state.cursor == UiProjectCursor::Root) {
    const std::uint16_t current = model.GetScaleRoot();
    const std::uint16_t start = selectorWindow(current, 12U);
    for (std::uint8_t index = 0; index < state.selectorCount; ++index)
      CopyUpper(state.selectorOptions[index], noteNames[start + index]);
  }
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureDevice(UiDeviceFrameState &state) {
  state = {};
  constexpr const char *midiDevices[] = {"OFF", "MIDI"};
  constexpr const char *boolean[] = {"OFF", "ON"};
  constexpr const char *resamplers[] = {"NONE", "LINEAR", "SINC"};
  constexpr const char *lineOutputs[] = {"HEADPHONE", "LINE"};
  const auto currentText = [&](Ui2DeviceField field, const char *const *options,
                               std::size_t count) {
    const Ui2SelectorState selector = device_.Selector(field);
    return options[std::min<std::size_t>(selector.current, count - 1U)];
  };
  CopyText(state.midiDevice,
           currentText(Ui2DeviceField::MidiDevice, midiDevices, 2U));
  CopyText(state.midiSync,
           currentText(Ui2DeviceField::MidiSync, boolean, 2U));
  CopyText(state.remoteUi,
           currentText(Ui2DeviceField::RemoteUi, boolean, 2U));
  CopyText(state.resampler,
           currentText(Ui2DeviceField::Resampler, resamplers, 3U));
  CopyText(state.lineOut,
           currentText(Ui2DeviceField::LineOut, lineOutputs, 2U));
  std::snprintf(state.volume.data(), state.volume.size(), "%u%%",
                device_.Selector(Ui2DeviceField::Volume).current);
  std::snprintf(state.brightness.data(), state.brightness.size(), "%u%%",
                device_.Selector(Ui2DeviceField::Brightness).current);
  CopyText(state.theme, "DEFAULT");
  CopyText(state.font, "REGULAR");
  CopyText(state.version, PROJECT_NUMBER);
  state.cursor = DeviceCursorFor(device_.SelectedField());
  state.showLineOut =
      (device_.VisibleFields() &
       (std::uint32_t{1}
        << static_cast<std::uint8_t>(Ui2DeviceField::LineOut))) != 0U;
  state.showUpdateFirmware =
      (device_.VisibleFields() &
       (std::uint32_t{1}
        << static_cast<std::uint8_t>(Ui2DeviceField::UpdateFirmware))) != 0U;
  const Ui2DeviceBottomState bottom = device_.Bottom();
  if (bottom.kind == Ui2DeviceBottomKind::Selector) {
    const char *const *options = boolean;
    std::size_t optionCount = 2U;
    if (device_.SelectedField() == Ui2DeviceField::MidiDevice) {
      options = midiDevices;
    } else if (device_.SelectedField() == Ui2DeviceField::Resampler) {
      options = resamplers;
      optionCount = 3U;
    } else if (device_.SelectedField() == Ui2DeviceField::LineOut) {
      options = lineOutputs;
    }
    if (device_.SelectedField() == Ui2DeviceField::Volume ||
        device_.SelectedField() == Ui2DeviceField::Brightness) {
      constexpr std::uint16_t visible = 5U;
      const std::uint16_t maximum = static_cast<std::uint16_t>(bottom.count - 1U);
      const std::uint16_t start = static_cast<std::uint16_t>(
          std::min<std::uint16_t>(bottom.current > 2U ? bottom.current - 2U : 0U,
                                  maximum >= visible - 1U
                                      ? maximum - (visible - 1U)
                                      : 0U));
      state.selectorCount = static_cast<std::uint8_t>(
          std::min<std::uint16_t>(visible, bottom.count));
      state.selectorCurrent = static_cast<std::uint8_t>(bottom.current - start);
      for (std::uint8_t index = 0; index < state.selectorCount; ++index) {
        std::snprintf(state.selectorOptions[index].data(),
                      state.selectorOptions[index].size(), "%u%%",
                      start + index);
      }
      optionCount = 0U;
    } else {
      state.selectorCount = static_cast<std::uint8_t>(optionCount);
      state.selectorCurrent = static_cast<std::uint8_t>(
          std::min<std::size_t>(bottom.current, optionCount - 1U));
    }
    state.selectorWrap = bottom.wrap;
    for (std::size_t index = 0; index < optionCount; ++index)
      CopyText(state.selectorOptions[index], options[index]);
  }
  const UiApplicationBatteryState battery = ReadBattery();
  state.batteryPercent = battery.percentage;
  state.batteryPercentValid = battery.available;
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureTheme(UiThemeFrameState &state) {
  state = {};
  state.view.name = {'D', 'E', 'F', 'A', 'U', 'L', 'T', '\0'};
  state.view.selectedColor = theme_.SelectedColor();
  state.view.nameAction = static_cast<std::uint8_t>(theme_.NameAction());
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureFont(UiFontFrameState &state) {
  state = {};
  const char *name = "REGULAR 5X7";
  std::snprintf(state.font.data(), state.font.size(), "%s", name);
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureBrowser(UiBrowserFrameState &state) {
  state = {};
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureGroove(UiGrooveFrameState &state) {
  state = {};
  hex2char(groove_.Number(), state.number.data());
  state.editRow = groove_.Row();
  const unsigned char *steps =
      Groove::GetInstance()->GetGrooveData(groove_.Number());
  std::copy_n(steps, 16, state.steps.begin());
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureMixer(UiMixerFrameState &state) {
  state = {};
  for (auto &channel : state.vuLevelTop)
    channel = {UiMixerView::kMeterHeight, UiMixerView::kMeterHeight};
  Project &project = session_.ProjectModel();
  state.selectedChannel = static_cast<std::int8_t>(mixer_.SelectedChannel());
  for (std::uint8_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel)
    FormatVolume(project.GetChannelVolume(channel), state.volumes[channel]);
  FormatVolume(project.GetMasterVolume(), state.volumes[SONG_CHANNEL_COUNT]);
  return {.active = PlayerRunning()};
}

UiApplicationActivityState Ui2NativeApplicationStateSource::CaptureSampleEditor(
    UiSampleEditorFrameState &state) {
  state = {};
  return {};
}

UiApplicationActivityState Ui2NativeApplicationStateSource::CaptureSampleSlices(
    UiSampleSlicesFrameState &state) {
  state = {};
  return {};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureRecord(UiRecordFrameState &state) {
  state = {};
  return {};
}

} // namespace ui2
