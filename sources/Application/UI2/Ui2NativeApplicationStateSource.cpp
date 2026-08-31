/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2NativeApplicationStateSource.h"
#include "Application/Audio/RecordingPlatform.h"
#include "Application/Model/Config.h"

#include "Application/Model/Groove.h"
#include "Application/Model/ProjectVersion.h"
#include "Application/Model/Scale.h"
#include "Application/Model/Table.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Player/Player.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Session/FirmwareLifecycleService.h"
#include "Application/Session/TrackerApplicationSession.h"
#include "Application/UI2/Ui2FixedText.h"
#include "Application/UI2/Ui2InstrumentParameters.h"
#include "Application/UI2/Ui2NotePresentation.h"
#include "Application/UI2/Ui2ProjectNamePresentation.h"
#include "Application/UI2/Ui2VuMapping.h"
#include "Application/UI2/Workflows/Ui2ThemeWorkflow.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "System/System/System.h"
#include "UI2/Views/Chain/UiChainView.h"
#include "UI2/Views/Groove/UiGrooveView.h"
#include "UI2/Views/Song/UiSongView.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace ui2 {

static_assert(Ui2FontController::TextCaseCount ==
              static_cast<std::uint8_t>(UiTextCaseMode::Lower) + 1U);

UiTextCaseMode Ui2NativeApplicationStateSource::TextCase() const {
  if (Variable *value =
          Config::GetInstance()->FindVariable(FourCC::VarUITextCase)) {
    constexpr int maximum = Ui2FontController::TextCaseCount - 1U;
    return static_cast<UiTextCaseMode>(
        std::clamp(value->GetInt(), 0, maximum));
  }
  return UiTextCaseMode::Upper;
}
namespace {

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

bool ProjectSampleInUse(void *context, const char *filename) {
  auto *project = static_cast<Project *>(context);
  return project != nullptr && filename != nullptr &&
         project->SampleInUse(
             etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>(filename));
}

void FormatElapsed(std::array<char, 6> &elapsed) {
  Player *player = Player::GetInstance();
  const int seconds = PlayerRunning()
                          ? std::max(0, static_cast<int>(player->GetPlayTime()))
                          : 0;
  FormatUiElapsed(seconds, elapsed);
}

void FormatCommand(FourCC command, std::array<char, 4> &text) {
  const char *source = command.c_str();
  if (source != nullptr && source[0] != '\0' && source[1] != '\0' &&
      source[2] != '\0' && source[3] == '\0')
    CopyUpper(text, source);
  else
    CopyUiText(text, "???");
}

UiDeviceCursor DeviceCursorFor(Ui2DeviceField field) {
  switch (field) {
  case Ui2DeviceField::MidiDevice:
    return UiDeviceCursor::MidiDevice;
  case Ui2DeviceField::MidiSync:
    return UiDeviceCursor::MidiSync;
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

std::array<std::uint8_t, 2>
MasterVu(std::uint8_t height = Ui2VuMeterHeight) {
  Player *player = Player::GetInstance();
  const std::uint32_t level =
      PlayerRunning() ? static_cast<std::uint32_t>(player->GetMasterLevel())
                      : 0U;
  const auto top = [height](std::uint16_t amplitude) {
    const std::uint8_t songTop = Ui2VuTopFromAmplitude(amplitude);
    return static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(songTop) * height +
         Ui2VuMeterHeight / 2U) /
        Ui2VuMeterHeight);
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
  if (firmwareLifecycle_.BatterySampled()) {
    const FirmwareBatterySample battery =
        firmwareLifecycle_.LastBatterySample();
    if (!battery.available)
      return {};
    return {.percentage =
                std::min<std::uint8_t>(battery.percentage, 100U),
            .available = true,
            .charging = battery.charging};
  }

  // The first frame is presented before the first application Tick(). Read
  // once here so that frame has a truthful battery state; every later frame
  // reuses the lifecycle service's 1 Hz sample and does not hit the ADC twice.
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
  std::array<char, MAX_PROJECT_NAME_LENGTH + 1U> storageName{};
  project.GetProjectName(storageName.data());
  Ui2ProjectNamePresentation(storageName.data()).CopyHeaderTo(state.name);
  state.editTrack = controller.Track();
  state.editRow = controller.VisibleRow();
  state.rowOffset = controller.RowOffset();
  state.adjustmentFocus =
      (controller.HeldMask() & TrackerActionBit(TrackerAction::Edit)) != 0U;
  state.modeFocus =
      (controller.HeldMask() & TrackerActionBit(TrackerAction::Option)) != 0U;
  state.selectionActive = controller.Selection().active;
  state.navHeld = navigationHeld_;
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
  Player *player = Player::GetInstance();
  state.playing = PlayerRunning();
  state.liveMode = controller.LiveMode();
  FormatElapsed(state.elapsed);
  CaptureUiTrackNotes(Player::GetInstance(), state.playing, state.notes);
  const PlayerTransportSnapshot transport = player->CaptureTransportSnapshot();
  // Browser previews deliberately support audio=disabled. Player still owns
  // transport and publishes the current pattern note even before a mixer
  // callback supplies the post-effect played-note text.
  if (state.liveMode && state.playing &&
      state.notes[controller.Track()][0] == '-') {
    const std::uint8_t note = transport.note[controller.Track()];
    if (note <= HIGHEST_NOTE)
      FormatUiNote(note, state.notes[controller.Track()]);
  }
  for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
    const int visible = transport.songRow[track] - controller.RowOffset();
    const bool visiblePlayback =
        state.playing && transport.mode != PM_AUDITION &&
        player->IsChannelPlaying(track) && transport.chain[track] != 0xFFU &&
        visible >= 0 && visible < 16;
    state.playbackRows[track] =
        visiblePlayback ? static_cast<std::int8_t>(visible) : -1;
    state.mutedTracks[track] = player->IsChannelMuted(track);
    const int queued = transport.queueSongRow[track] - controller.RowOffset();
    if (state.liveMode && state.playing &&
        transport.queueMode[track] != QM_NONE && queued >= 0 && queued < 16) {
      state.queuedRows[track] = static_cast<std::int8_t>(queued);
    }
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
      (controller.HeldMask() & TrackerActionBit(TrackerAction::Edit)) != 0U;
  state.selectionActive = controller.Selection().active;
  state.navHeld = navigationHeld_;
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
  CaptureUiTrackNotes(Player::GetInstance(), PlayerRunning(),
                      state.trackNotes);
  Player *player = Player::GetInstance();
  const PlayerTransportSnapshot transport = player->CaptureTransportSnapshot();
  for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track)
    state.mutedTracks[track] = player->IsChannelMuted(track);
  if (transport.running && transport.mode != PM_AUDITION) {
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
      if (!player->IsChannelPlaying(track) ||
          transport.chain[track] != controller.Number())
        continue;
      const int playbackRow = transport.chainRow[track];
      if (playbackRow >= 0 && playbackRow < PHRASES_PER_CHAIN)
        state.playbackRows[track] = static_cast<std::int8_t>(playbackRow);
    }
  }
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
      controller.Column() == 0U &&
      (controller.HeldMask() & TrackerActionBit(TrackerAction::Edit)) != 0U;
  state.selectionActive = controller.Selection().active;
  state.navHeld = navigationHeld_;
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
    FormatUiNote(phrase.note_[index], state.rows[row].note);
    if (phrase.instr_[index] == 0xFFU)
      CopyUiText(state.rows[row].instrument, "I--");
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
  CaptureUiTrackNotes(Player::GetInstance(), PlayerRunning(),
                      state.trackNotes);
  Player *player = Player::GetInstance();
  const PlayerTransportSnapshot transport = player->CaptureTransportSnapshot();
  for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track)
    state.mutedTracks[track] = player->IsChannelMuted(track);
  if (transport.running && transport.mode != PM_AUDITION) {
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
      if (!player->IsChannelPlaying(track) ||
          transport.phrase[track] != controller.Number())
        continue;
      const int playbackRow = transport.phraseRow[track];
      if (playbackRow >= 0 && playbackRow < STEPS_PER_PHRASE)
        state.playbackRows[track] = static_cast<std::int8_t>(playbackRow);
    }
  }
  const int selected = base + controller.Row();
  if (controller.Column() <= 1U) {
    const bool cellHasValue = controller.Column() == 0U
                                  ? phrase.note_[selected] != NO_NOTE
                                  : phrase.instr_[selected] != 0xFFU;
    std::uint8_t instrument = controller.Column() == 1U
                                  ? phrase.instr_[selected]
                                  : 0xFFU;
    if (cellHasValue && controller.Column() == 0U) {
      for (int row = controller.Row(); row >= 0; --row) {
        if (phrase.instr_[base + row] != 0xFFU) {
          instrument = phrase.instr_[base + row];
          break;
        }
      }
    }
    const auto &list = project.GetInstrumentBank()->InstrumentsList();
    if (cellHasValue && instrument < list.size() &&
        list[instrument] != nullptr) {
      state.context = UiPhraseContext::Instrument;
      std::snprintf(state.contextLead.data(), state.contextLead.size(),
                    "INSTRUMENT %02X", instrument);
      CopyUiText(state.contextTail, list[instrument]->GetDisplayName().c_str());
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
  // Table command and value cells always keep the command-specific help.
  // ENTER-held value editing is represented by the in-cell digit cursor.
  state.adjustmentFocus = false;
  state.selectionActive = controller.Selection().active;
  state.navHeld = navigationHeld_;
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
  Player *player = Player::GetInstance();
  CaptureUiTrackNotes(player, PlayerRunning(), state.trackNotes);
  const int selectedTrack = controller.SelectedTrack();
  if (selectedTrack >= 0 && selectedTrack < SONG_CHANNEL_COUNT)
    state.selectedTrackMuted = player->IsChannelMuted(selectedTrack);
  const PlayerTransportSnapshot transport =
      player->CaptureTransportSnapshot();
  if (transport.running && transport.mode != PM_AUDITION &&
      selectedTrack >= 0 && selectedTrack < SONG_CHANNEL_COUNT &&
      player->IsChannelPlaying(selectedTrack)) {
    Table &visibleTable =
        TableHolder::GetInstance()->GetTable(controller.Number());
    const auto capturePlayback = [&](TablePlayback &playback,
                                     auto &playbackRows) {
      const TablePlaybackSnapshot playbackSnapshot =
          playback.CapturePlaybackSnapshot();
      if (playbackSnapshot.table != &visibleTable)
        return;
      for (std::uint8_t group = 0U; group < playbackRows.size(); ++group) {
        const int playbackRow = playbackSnapshot.position[group];
        if (playbackRow >= 0 && playbackRow < TABLE_STEPS)
          playbackRows[group] = static_cast<std::int8_t>(playbackRow);
      }
    };
    capturePlayback(TablePlayback::GetTablePlayback(selectedTrack),
                    state.playbackRows);
    capturePlayback(TablePlayback::GetAutomationPlayback(selectedTrack),
                    state.automationPlaybackRows);
  }
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
  const std::uint8_t number =
      static_cast<std::uint8_t>(editor.currentInstrumentID_);
  InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
  I_Instrument *instrument = bank->GetInstrument(number);
  const InstrumentType type = instrument == nullptr ? IT_NONE
                                                     : instrument->GetType();
  hex2char(number, state.number.data());
  state.selectedTrack = editor.songX_;
  state.kind = static_cast<UiInstrumentKind>(type);
  if (instrument == nullptr || type == IT_NONE)
    CopyUiText(state.name, "--");
  else
    CopyUiText(state.name, instrument->GetDisplayName().c_str());

  const bool sidFirstChip = type != IT_SID ||
                            static_cast<SIDInstrument *>(instrument)->GetChip() ==
                                SID1;
  const auto valueFor = [instrument](FourCC::enum_type id, int fallback = 0) {
    Variable *value = id == FourCC::Default || instrument == nullptr
                          ? nullptr
                          : instrument->FindVariable(id);
    return value == nullptr ? fallback : value->GetInt();
  };
  const auto textFor = [instrument](FourCC::enum_type id) {
    Variable *value = id == FourCC::Default || instrument == nullptr
                          ? nullptr
                          : instrument->FindVariable(id);
    return value == nullptr ? etl::string<MAX_VARIABLE_STRING_LENGTH>{}
                            : value->GetString();
  };

  const std::uint8_t fieldCount = Ui2InstrumentFieldCount(type);
  for (std::uint8_t index = 0U;
       index < fieldCount && state.fieldCount < state.fields.size(); ++index) {
    const Ui2InstrumentParameterDescriptor descriptor =
        Ui2InstrumentFieldParameter(type, index, sidFirstChip);
    auto &field = state.fields[state.fieldCount++];
    CopyUiText(field.label, descriptor.label);
    int current = valueFor(descriptor.primary);
    int secondary = valueFor(descriptor.secondary);
    const char *text = nullptr;
    etl::string<MAX_VARIABLE_STRING_LENGTH> variableText;
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> filename;
    if (descriptor.format == Ui2InstrumentValueFormat::UserText &&
        type == IT_SAMPLE) {
      filename = static_cast<SampleInstrument *>(instrument)->GetSampleFileName();
      text = filename.c_str();
    } else if (descriptor.format == Ui2InstrumentValueFormat::SliceCount &&
               type == IT_SAMPLE) {
      current = 0;
      auto *sample = static_cast<SampleInstrument *>(instrument);
      for (std::size_t slice = 0U; slice < SampleInstrument::MaxSlices; ++slice)
        current += sample->IsSliceDefined(slice) ? 1 : 0;
    } else if (descriptor.format == Ui2InstrumentValueFormat::Choice) {
      variableText = textFor(descriptor.primary);
      text = variableText.c_str();
    }
    Ui2FormatInstrumentParameter(descriptor, current, secondary, text,
                                 field.value.data(), field.value.size());
    field.y = descriptor.y;
    field.userData = descriptor.userData;
  }

  for (std::uint8_t index = 0U;
       index < Ui2InstrumentOperatorCount(type) &&
       state.operatorCount < state.operators.size();
       ++index) {
    auto &row = state.operators[state.operatorCount++];
    const Ui2InstrumentParameterDescriptor op1 =
        Ui2InstrumentOperatorParameter(index, false);
    const Ui2InstrumentParameterDescriptor op2 =
        Ui2InstrumentOperatorParameter(index, true);
    CopyUiText(row.label, op1.label);
    Ui2FormatInstrumentParameter(op1, valueFor(op1.primary), 0, nullptr,
                                 row.op1.data(), row.op1.size());
    Ui2FormatInstrumentParameter(op2, valueFor(op2.primary), 0, nullptr,
                                 row.op2.data(), row.op2.size());
  }

  instrument_.Synchronize(number, editor.songX_,
                          {IT_LAST, static_cast<std::uint16_t>(type), true},
                          state.fieldCount, state.operatorCount);
  const Ui2InstrumentCursorPosition cursor = instrument_.Cursor();
  const Ui2InstrumentParameterDescriptor activeDescriptor =
      Ui2InstrumentCursorParameter(type, cursor, sidFirstChip);
  const Ui2InstrumentSubfieldSpec activeSubfields =
      Ui2InstrumentSubfields(activeDescriptor);
  const Ui2InstrumentAdjustmentSpec activeAdjustment =
      Ui2InstrumentAdjustment(activeDescriptor);
  instrument_.ConfigureValueSubfields(activeSubfields.mode,
                                      activeSubfields.count);
  state.cursor = cursor.kind == Ui2InstrumentCursorKind::Name
                     ? UiInstrumentCursor::Name
                 : cursor.kind == Ui2InstrumentCursorKind::Type
                     ? UiInstrumentCursor::Type
                 : cursor.kind == Ui2InstrumentCursorKind::Field
                     ? UiInstrumentCursor::Field
                 : cursor.kind == Ui2InstrumentCursorKind::Operator1
                     ? UiInstrumentCursor::Operator1
                     : UiInstrumentCursor::Operator2;
  state.selectedField = cursor.index;
  state.selectedOperator = cursor.index;
  state.nameAction = static_cast<std::uint8_t>(instrument_.NameAction());
  state.numberFocus = instrument_.NumberFocus();
  state.enterSubfieldFocus = instrument_.EnterSubfieldFocus();
  state.adjustmentFocus =
      !state.numberFocus && activeAdjustment.visible &&
      (instrument_.HeldMask() & TrackerActionBit(TrackerAction::Edit)) != 0U;
  state.adjustmentNote = activeAdjustment.note;
  state.adjustmentFineStep = activeAdjustment.fineStep;
  state.adjustmentCoarseStep = activeAdjustment.coarseStep;
  const bool parameterCursor =
      cursor.kind == Ui2InstrumentCursorKind::Field ||
      cursor.kind == Ui2InstrumentCursorKind::Operator1 ||
      cursor.kind == Ui2InstrumentCursorKind::Operator2;
  Variable *activeValue =
      activeDescriptor.primary == FourCC::Default || instrument == nullptr
          ? nullptr
          : instrument->FindVariable(activeDescriptor.primary);
  if (parameterCursor && activeDescriptor.Valid()) {
    if (type == IT_SAMPLE && cursor.kind == Ui2InstrumentCursorKind::Field &&
        cursor.index <= 1U) {
      state.fieldBottom = UiInstrumentFieldBottom::Open;
    } else if (activeValue != nullptr &&
               activeValue->GetType() == Variable::BOOL) {
      state.fieldBottom = UiInstrumentFieldBottom::Selector;
      state.fieldOptions = UiInstrumentFieldOptions::Boolean;
      state.fieldOptionCurrent =
          static_cast<std::uint8_t>(activeValue->GetBool() ? 1U : 0U);
      state.fieldOptionWrap = true;
    } else if (activeValue != nullptr &&
               activeValue->GetType() == Variable::CHAR_LIST) {
      state.fieldBottom = UiInstrumentFieldBottom::Selector;
      switch (activeDescriptor.format) {
      case Ui2InstrumentValueFormat::SampleLoop:
        state.fieldOptions = UiInstrumentFieldOptions::SampleLoop;
        break;
      case Ui2InstrumentValueFormat::SidWaveform:
        state.fieldOptions = UiInstrumentFieldOptions::SidWaveform;
        break;
      case Ui2InstrumentValueFormat::OpalAlgorithm:
        state.fieldOptions = UiInstrumentFieldOptions::OpalAlgorithm;
        break;
      case Ui2InstrumentValueFormat::OpalWave:
        state.fieldOptions = UiInstrumentFieldOptions::OpalWave;
        break;
      case Ui2InstrumentValueFormat::OpalKeyscale:
        state.fieldOptions = UiInstrumentFieldOptions::OpalKeyscale;
        break;
      case Ui2InstrumentValueFormat::Choice:
        state.fieldOptions =
            activeDescriptor.primary == FourCC::SampleInstrumentInterpolation
                ? UiInstrumentFieldOptions::SampleInterpolation
                : UiInstrumentFieldOptions::SidFilter;
        break;
      default:
        state.fieldOptions = UiInstrumentFieldOptions::None;
        break;
      }
      state.fieldOptionCurrent = static_cast<std::uint8_t>(std::clamp(
          activeValue->GetInt(), 0,
          std::max(0, static_cast<int>(activeValue->GetListSize()) - 1)));
      state.fieldOptionWrap = true;
    } else if (activeAdjustment.visible ||
               activeDescriptor.format == Ui2InstrumentValueFormat::SampleFilter ||
               activeDescriptor.subfieldMode != Ui2InstrumentSubfieldMode::None) {
      state.fieldBottom = UiInstrumentFieldBottom::Adjustment;
      state.adjustmentFineStep = static_cast<std::uint8_t>(
          std::min<std::uint16_t>(activeDescriptor.fineStep, 0xFFU));
      state.adjustmentCoarseStep = static_cast<std::uint8_t>(
          std::min<std::uint16_t>(activeDescriptor.coarseStep, 0xFFU));
    }
  }
  state.selectedSubfield = instrument_.Subfield();
  state.subfieldTextOffset = activeSubfields.textOffset;
  FormatElapsed(state.elapsed);
  CaptureUiTrackNotes(Player::GetInstance(), PlayerRunning(),
                      state.trackNotes);
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureProject(UiProjectFrameState &state) {
  state = {};
  Project &model = session_.ProjectModel();
  std::array<char, MAX_PROJECT_NAME_LENGTH + 1U> storageName{};
  model.GetProjectName(storageName.data());
  Ui2ProjectNamePresentation(storageName.data()).CopyHeaderTo(state.name);
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
    constexpr std::int16_t count = 12;
    const std::int16_t current = model.GetScaleRoot();
    state.selectorCount = 3U;
    state.selectorCurrent = 1U;
    state.selectorWrap = true;
    for (std::int16_t index = 0; index < 3; ++index) {
      const std::int16_t option =
          static_cast<std::int16_t>((current + index - 1 + count) % count);
      CopyUpper(state.selectorOptions[index], noteNames[option]);
    }
  }
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureDevice(UiDeviceFrameState &state) {
  state = {};
  constexpr const char *midiDevices[] = {"OFF", "TRS", "USB", "TRS+USB"};
  constexpr const char *boolean[] = {"OFF", "ON"};
  constexpr const char *resamplers[] = {"NONE", "LINEAR"};
  constexpr const char *lineOutputs[] = {"HP LOW", "HP HIGH", "LINE LEVEL"};
  const auto currentText = [&](Ui2DeviceField field, const char *const *options,
                               std::size_t count) {
    const Ui2SelectorState selector = device_.Selector(field);
    return options[std::min<std::size_t>(selector.current, count - 1U)];
  };
  CopyUiText(state.midiDevice,
             currentText(Ui2DeviceField::MidiDevice, midiDevices, 4U));
  CopyUiText(state.midiSync,
             currentText(Ui2DeviceField::MidiSync, boolean, 2U));
  CopyUiText(state.resampler,
             currentText(Ui2DeviceField::Resampler, resamplers, 2U));
  CopyUiText(state.lineOut,
             currentText(Ui2DeviceField::LineOut, lineOutputs, 3U));
  FormatUiPercent(device_.Selector(Ui2DeviceField::Volume).current,
                  state.volume);
  FormatUiPercent(device_.Selector(Ui2DeviceField::Brightness).current,
                  state.brightness);
  if (Config *config = Config::GetInstance()) {
    if (Variable *theme = config->FindVariable(FourCC::VarThemeName))
      CopyUiText(state.theme, theme->GetString().c_str());
    if (Variable *font = config->FindVariable(FourCC::VarUIFont)) {
      if (font->GetInt() == 0) {
        CopyUiText(state.font, font->GetString().c_str());
      } else {
        std::snprintf(state.font.data(), state.font.size(),
                      "%s (UNAVAILABLE; USING REGULAR)",
                      font->GetString().c_str());
      }
    }
  }
  CopyUiText(state.version, PROJECT_NUMBER);
  state.cursor = DeviceCursorFor(device_.SelectedField());
  state.editHeld =
      (device_.HeldMask() & TrackerActionBit(TrackerAction::Edit)) != 0U;
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
      optionCount = 4U;
    } else if (device_.SelectedField() == Ui2DeviceField::Resampler) {
      options = resamplers;
      optionCount = 2U;
    } else if (device_.SelectedField() == Ui2DeviceField::LineOut) {
      options = lineOutputs;
      optionCount = 3U;
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
        FormatUiPercent(static_cast<std::uint16_t>(start + index),
                        state.selectorOptions[index]);
      }
      optionCount = 0U;
    } else {
      state.selectorCount = static_cast<std::uint8_t>(optionCount);
      state.selectorCurrent = static_cast<std::uint8_t>(
          std::min<std::size_t>(bottom.current, optionCount - 1U));
    }
    state.selectorWrap = bottom.wrap;
    for (std::size_t index = 0; index < optionCount; ++index)
      CopyUiText(state.selectorOptions[index], options[index]);
  }
  const UiApplicationBatteryState battery = ReadBattery();
  state.batteryPercent = battery.percentage;
  state.batteryPercentValid = battery.available;
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureTheme(UiThemeFrameState &state) {
  state = {};
  if (Config *config = Config::GetInstance()) {
    if (Variable *name = config->FindVariable(FourCC::VarThemeName))
      CopyUiText(state.view.name, name->GetString().c_str());
    state.colors = config->GetSemanticThemeColors();
    state.colorsValid = true;
  }
  state.view.selectedColor = theme_.SelectedColor();
  state.view.nameAction = static_cast<std::uint8_t>(theme_.NameAction());
  state.view.colorComponent = theme_.ColorComponent();
  if (state.view.selectedColor >= 0 &&
      static_cast<std::size_t>(state.view.selectedColor) <
          state.colors.size() &&
      state.colorsValid) {
    state.view.selectedRgb = Ui2ThemeWorkflow::Components(
        state.colors[static_cast<std::size_t>(state.view.selectedColor)]);
  }
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureFont(UiFontFrameState &state) {
  state = {};
  if (Config *config = Config::GetInstance()) {
    if (Variable *value = config->FindVariable(FourCC::VarUIFont)) {
      if (value->GetInt() == 0) {
        std::snprintf(state.font.data(), state.font.size(), "%s",
                      value->GetString().c_str());
      } else {
        std::snprintf(state.font.data(), state.font.size(),
                      "%s (UNAVAILABLE; USING REGULAR)",
                      value->GetString().c_str());
      }
    }
  }
  constexpr std::array<const char *, Ui2FontController::TextCaseCount> cases{
      "Case", "CASE", "case"};
  const std::size_t textCase =
      std::min<std::size_t>(font_.TextCase(), cases.size() - 1U);
  std::snprintf(state.textCase.data(), state.textCase.size(), "%s",
                cases[textCase]);
  state.cursor = font_.SelectedField() == Ui2FontField::TextCase
                     ? UiFontCursor::TextCase
                     : UiFontCursor::Browse;
  state.action = font_.SelectedAction() == Ui2FontAction::Browse
                     ? UiFontAction::Browse
                     : UiFontAction::Default;
  const char *feedback = "";
  switch (font_.Feedback()) {
  case Ui2FontFeedback::BrowserUnavailable:
    feedback = "FONT BROWSER UNAVAILABLE";
    break;
  case Ui2FontFeedback::ConfigUnavailable:
    feedback = "FONT CONFIG UNAVAILABLE";
    break;
  case Ui2FontFeedback::SaveFailed:
    feedback = "FONT SAVE FAILED";
    break;
  case Ui2FontFeedback::DefaultRestored:
    feedback = "DEFAULT RESTORED";
    break;
  case Ui2FontFeedback::None:
    break;
  }
  CopyUiText(state.feedback, feedback);
  return {.active = PlayerRunning()};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureBrowser(UiBrowserFrameState &state) {
  state = {};
  if (settingsBrowser_.Active()) {
    state.snapshot = settingsBrowser_.Snapshot();
  } else if (instrumentBrowserActive_) {
    state.snapshot = instrumentBrowser_.Snapshot();
  } else if (sampleBrowser_.Active()) {
    Project &project = session_.ProjectModel();
    int previewVolume = 0;
    if (Variable *volume = project.FindVariable(FourCC::VarPreviewVolume))
      previewVolume = volume->GetInt();
    state.snapshot = sampleBrowser_.Snapshot(previewVolume, ProjectSampleInUse,
                                             &project);
  } else {
    state.snapshot = projectBrowser_.Snapshot(session_.ProjectName());
  }
  Player *player = Player::GetInstance();
  return {.active = player != nullptr &&
                    (player->IsRunning() ||
                     (sampleBrowser_.Active() && player->IsPlaying()))};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureGroove(UiGrooveFrameState &state) {
  state = {};
  hex2char(groove_.Number(), state.number.data());
  state.editRow = groove_.Row();
  state.selectionActive = groove_.Selection().active;
  if (groove_.Selection().active) {
    state.selectionVisualRect = UiGrooveView::SelectionTargetRect(
        groove_.Selection().Top(), groove_.Selection().Bottom());
  }
  Groove *groove = Groove::GetInstance();
  const unsigned char *steps = groove->GetGrooveData(groove_.Number());
  std::copy_n(steps, 16, state.steps.begin());
  Player *player = Player::GetInstance();
  if (player == nullptr)
    return {.active = false};
  const PlayerTransportSnapshot transport = player->CaptureTransportSnapshot();
  const int track = session_.EditorState().songX_;
  if (track >= 0 && track < SONG_CHANNEL_COUNT)
    state.selectedTrackMuted = player->IsChannelMuted(track);
  if (transport.running && transport.mode != PM_AUDITION && track >= 0 &&
      track < SONG_CHANNEL_COUNT && player->IsChannelPlaying(track)) {
    int playingGroove = 0;
    int playingRow = 0;
    groove->GetChannelData(track, &playingGroove, &playingRow);
    if (playingGroove == groove_.Number() && playingRow >= 0 &&
        playingRow < 16)
      state.playbackRow = static_cast<std::int8_t>(playingRow);
  }
  return {.active = transport.running};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureMixer(UiMixerFrameState &state) {
  state = {};
  for (auto &channel : state.vuLevelTop)
    channel = {UiMixerView::kMeterHeight, UiMixerView::kMeterHeight};
  Project &project = session_.ProjectModel();
  state.selectedChannel = static_cast<std::int8_t>(mixer_.SelectedChannel());
  for (std::uint8_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel)
    FormatUiVolume(project.GetChannelVolume(channel), state.volumes[channel]);
  FormatUiVolume(project.GetMasterVolume(), state.volumes[SONG_CHANNEL_COUNT]);

  Player *player = Player::GetInstance();
  if (player == nullptr)
    return {.active = false};
  // PlayerMixer retains its last peak values after transport stops.  The
  // mixer view represents current activity, so leave every meter at the
  // empty baseline initialized above while playback is inactive.
  if (!player->IsRunning())
    return {.active = false};
  const auto captureStereoLevel = [&](std::uint8_t channel,
                                      std::uint32_t level) {
    state.vuLevelTop[channel][0] =
        Ui2VuTopFromAmplitude(static_cast<std::uint16_t>(level >> 16U));
    state.vuLevelTop[channel][1] =
        Ui2VuTopFromAmplitude(static_cast<std::uint16_t>(level & 0xFFFFU));
  };
  const auto *levels = player->GetMixerLevels();
  if (levels != nullptr) {
    for (std::uint8_t channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
      if (!player->IsChannelMuted(channel))
        captureStereoLevel(channel,
                           static_cast<std::uint32_t>(levels->at(channel)));
    }
  }
  captureStereoLevel(SONG_CHANNEL_COUNT,
                     static_cast<std::uint32_t>(player->GetMasterLevel()));
  return {.active = true};
}

UiApplicationActivityState Ui2NativeApplicationStateSource::CaptureSampleEditor(
    UiSampleEditorFrameState &state) {
  const SampleEditorViewUi2Snapshot snapshot = sampleEditor_.Snapshot();
  const std::uint16_t held = sampleEditor_.HeldMask();
  state = MakeUiSampleEditorControllerState(
      snapshot, UiPowerState::BatteryNormal,
      {.enterHeld =
           (held & TrackerActionBit(TrackerAction::Edit)) != 0U,
       .editHeld =
           (held & TrackerActionBit(TrackerAction::Option)) != 0U});
  return {.active = snapshot.playing};
}

UiApplicationActivityState Ui2NativeApplicationStateSource::CaptureSampleSlices(
    UiSampleSlicesFrameState &state) {
  const SampleSlicesViewUi2Snapshot snapshot = sampleSlices_.Snapshot();
  const std::uint16_t held = sampleSlices_.HeldMask();
  state = MakeUiSampleSlicesControllerState(
      snapshot, UiPowerState::BatteryNormal,
      {.enterHeld =
           (held & TrackerActionBit(TrackerAction::Edit)) != 0U,
       .editHeld =
           (held & TrackerActionBit(TrackerAction::Option)) != 0U});
  return {.active = snapshot.previewActive};
}

UiApplicationActivityState
Ui2NativeApplicationStateSource::CaptureRecord(UiRecordFrameState &state) {
  state = {};
  Config *config = Config::GetInstance();
  const auto value = [config](FourCC::enum_type key) {
    Variable *variable =
        config == nullptr ? nullptr : config->FindVariable(FourCC(key));
    return variable == nullptr ? 0 : variable->GetInt();
  };
  const std::uint8_t source = static_cast<std::uint8_t>(
      std::clamp(value(FourCC::VarRecordSource), 0, 3));
  const std::int8_t lineGain = static_cast<std::int8_t>(std::clamp(
      value(FourCC::VarRecordLineGain), RecordingPlatform::kLineInGainMinDb,
      RecordingPlatform::kLineInGainMaxDb));
  const std::int8_t micGain = static_cast<std::int8_t>(std::clamp(
      value(FourCC::VarRecordMicGain), RecordingPlatform::kMicGainMinDb,
      RecordingPlatform::kMicGainMaxDb));
  record_.Synchronize(source, lineGain, micGain);
  constexpr const char *sources[] = {"ALL OFF", "LINE IN", "MIC", "USB IN"};
  CopyUiText(state.snapshot.source, sources[source]);
  std::snprintf(state.snapshot.lineGain.data(),
                state.snapshot.lineGain.size(), "%d DB", lineGain);
  std::snprintf(state.snapshot.micGain.data(), state.snapshot.micGain.size(),
                "%d DB", micGain);
  CopyUiText(state.snapshot.elapsed, "00:00");
  state.snapshot.sourceIndex = source;
  state.snapshot.lineGainDb = lineGain;
  state.snapshot.micGainDb = micGain;
  const bool available = record_.Available();
  state.snapshot.recordingAvailable = available;
  state.snapshot.meterAvailable = false;
  if (!available) {
    state.snapshot.focus = RecordViewUi2Focus::Unknown;
  } else {
    switch (record_.SelectedField()) {
    case Ui2RecordField::Source:
      state.snapshot.focus = RecordViewUi2Focus::Source;
      break;
    case Ui2RecordField::LineGain:
      state.snapshot.focus = RecordViewUi2Focus::LineGain;
      break;
    case Ui2RecordField::MicGain:
      state.snapshot.focus = RecordViewUi2Focus::MicGain;
      break;
    case Ui2RecordField::Count:
      state.snapshot.focus = RecordViewUi2Focus::Unknown;
      break;
    }
    if (IsSavingRecording()) {
      state.snapshot.state = RecordViewUi2State::Saving;
      state.snapshot.savingPercent = GetSavingProgressPercent();
    } else if (IsRecordingActive()) {
      state.snapshot.state = RecordViewUi2State::Recording;
    }
  }
  state.cursorInkVisible = available;
  const bool recordingBusy =
      available && state.snapshot.state != RecordViewUi2State::Idle;
  return {.active = PlayerRunning() || recordingBusy};
}

} // namespace ui2
