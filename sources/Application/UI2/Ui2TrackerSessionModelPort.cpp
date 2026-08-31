/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2TrackerSessionModelPort.h"

#include "Application/Instruments/CommandList.h"
#include "Application/Model/Scale.h"
#include "Application/Model/Table.h"
#include "Application/Player/Player.h"
#include "Application/UI2/Ui2ChainTranspose.h"
#include "Application/UI2/Ui2TransportPolicy.h"

#include <algorithm>

namespace ui2 {
namespace {

std::uint8_t AdjustByte(std::uint8_t current, std::int16_t delta,
                        std::uint8_t maximum, bool wrap,
                        bool emptyIsZero = true) {
  int value = current;
  if (emptyIsZero && current == 0xFFU && maximum != 0xFFU)
    value = 0;
  value += delta;
  if (wrap) {
    const int count = static_cast<int>(maximum) + 1;
    while (value < 0)
      value += count;
    while (value > maximum)
      value -= count;
  } else {
    value = std::clamp(value, 0, static_cast<int>(maximum));
  }
  return static_cast<std::uint8_t>(value);
}

std::int16_t DirectionDelta(Ui2TrackerEditDirection direction,
                            std::int16_t verticalStep) {
  switch (direction) {
  case Ui2TrackerEditDirection::Left:
    return -1;
  case Ui2TrackerEditDirection::Right:
    return 1;
  case Ui2TrackerEditDirection::Down:
    return static_cast<std::int16_t>(-verticalStep);
  case Ui2TrackerEditDirection::Up:
    return verticalStep;
  case Ui2TrackerEditDirection::None:
    return 0;
  }
  return 0;
}

FourCC AdjustCommand(FourCC current, Ui2TrackerEditDirection direction,
                     bool table) {
  FourCC result = current;
  switch (direction) {
  case Ui2TrackerEditDirection::Left:
    result = CommandList::GetPrev(current);
    break;
  case Ui2TrackerEditDirection::Right:
    result = CommandList::GetNext(current);
    break;
  case Ui2TrackerEditDirection::Down:
    result = CommandList::GetPrevAlpha(current);
    break;
  case Ui2TrackerEditDirection::Up:
    result = CommandList::GetNextAlpha(current);
    break;
  case Ui2TrackerEditDirection::None:
    break;
  }
  if (table && result == FourCC::InstrumentCommandTable) {
    result = direction == Ui2TrackerEditDirection::Left ||
                     direction == Ui2TrackerEditDirection::Down
                 ? CommandList::GetPrev(result)
                 : CommandList::GetNext(result);
  }
  return result;
}

struct GridBounds {
  std::uint8_t maximumColumn = 0U;
  std::uint8_t maximumRow = 0U;
};

struct SelectionRect {
  std::uint8_t left = 0U;
  std::uint8_t top = 0U;
  std::uint8_t right = 0U;
  std::uint8_t bottom = 0U;
};

bool ResolveGridBounds(Ui2TrackerPage page, GridBounds &bounds) {
  switch (page) {
  case Ui2TrackerPage::Song:
    bounds = {SONG_CHANNEL_COUNT - 1U, SONG_ROW_COUNT - 1U};
    return true;
  case Ui2TrackerPage::Chain:
    bounds = {1U, PHRASES_PER_CHAIN - 1U};
    return true;
  case Ui2TrackerPage::Phrase:
  case Ui2TrackerPage::PhraseTable:
  case Ui2TrackerPage::InstrumentTable:
    bounds = {5U, STEPS_PER_PHRASE - 1U};
    return true;
  case Ui2TrackerPage::None:
  case Ui2TrackerPage::Project:
  case Ui2TrackerPage::Mixer:
  case Ui2TrackerPage::Groove:
  case Ui2TrackerPage::Instrument:
  case Ui2TrackerPage::Record:
    return false;
  }
  return false;
}

bool IsGridCell(Ui2TrackerPage page, std::uint8_t row, std::uint8_t column) {
  GridBounds bounds{};
  return ResolveGridBounds(page, bounds) && column <= bounds.maximumColumn &&
         row <= bounds.maximumRow;
}

bool ResolveSelectionRect(Ui2TrackerPage page,
                          const Ui2GridSelectionState &selection,
                          SelectionRect &rect) {
  GridBounds bounds{};
  if (!selection.active || !ResolveGridBounds(page, bounds) ||
      selection.anchorColumn > bounds.maximumColumn ||
      selection.activeColumn > bounds.maximumColumn ||
      selection.anchorRow > bounds.maximumRow ||
      selection.activeRow > bounds.maximumRow) {
    return false;
  }
  rect = {selection.Left(), selection.Top(), selection.Right(),
          selection.Bottom()};
  // The fixed clipboard deliberately matches the largest visible tracker
  // selection (8 columns by 16 rows). Reject malformed or stale controller
  // state instead of indexing beyond that compatibility buffer.
  if (rect.right - rect.left + 1U > 8U ||
      rect.bottom - rect.top + 1U > kUi2TrackerVisibleRows) {
    return false;
  }
  return true;
}

std::int16_t SelectionCellDelta(Ui2TrackerPage page, std::uint8_t column,
                                Ui2TrackerEditDirection direction) {
  const std::int16_t verticalStep =
      page == Ui2TrackerPage::Chain && column == 1U ? 12 : 16;
  return DirectionDelta(direction, verticalStep);
}

bool ResolveEffectiveSampleInstrument(Project &project, const Phrase &phrase,
                                      std::uint8_t phraseNumber,
                                      std::uint8_t row,
                                      SampleInstrument *&sample) {
  InstrumentBank *bank = project.GetInstrumentBank();
  if (bank == nullptr)
    return false;
  const int first = phraseNumber * STEPS_PER_PHRASE;
  for (int current = row; current >= 0; --current) {
    const std::uint8_t id = phrase.instr_[first + current];
    if (id == 0xFFU)
      continue;
    I_Instrument *instrument = bank->GetInstrument(id);
    if (instrument == nullptr || instrument->GetType() != IT_SAMPLE)
      return false;
    sample = static_cast<SampleInstrument *>(instrument);
    return true;
  }
  return false;
}

std::uint8_t AdjustPhraseNote(Project &project, const Phrase &phrase,
                              std::uint8_t phraseNumber, std::uint8_t row,
                              std::uint8_t current, std::int16_t delta) {
  if (current == NO_NOTE)
    return current;
  // A direction from NOTE OFF first returns to C3; it does not apply the
  // direction again until the next input edge.
  if (current == NOTE_OFF)
    return NOTE_C3;

  SampleInstrument *sample = nullptr;
  std::uint8_t sliceFirst = 0U;
  std::uint8_t sliceLast = 0U;
  if (ResolveEffectiveSampleInstrument(project, phrase, phraseNumber, row,
                                       sample) &&
      sample->GetSliceNoteRange(sliceFirst, sliceLast)) {
    return static_cast<std::uint8_t>(std::clamp<int>(
        static_cast<int>(current) + delta, sliceFirst, sliceLast));
  }

  const int scale = std::clamp(project.GetScale(), 0, numScales - 1);
  const int root = std::min<int>(project.GetScaleRoot(), 11);
  int offset = delta;
  int candidate = static_cast<int>(current) + offset;
  const int direction = offset < 0 ? -1 : (offset > 0 ? 1 : 0);
  while (direction != 0 && candidate >= 0 &&
         !scaleSteps[scale][(candidate + 12 - root) % 12]) {
    offset += direction;
    candidate = static_cast<int>(current) + offset;
  }
  return AdjustByte(current, static_cast<std::int16_t>(offset), HIGHEST_NOTE,
                    true, false);
}

} // namespace

Ui2TrackerSessionModelPort::Ui2TrackerSessionModelPort(
    TrackerApplicationSession &session)
    : session_(session) {}

void Ui2TrackerSessionModelPort::ResetProjectBoundary() {
  activePage_ = Ui2TrackerPage::Song;
  phraseRow_ = 0U;
  phraseColumn_ = 0U;
  phraseDigit_ = 3U;
  phraseTableNumber_ = 0U;
  phraseTableRow_ = 0U;
  phraseTableColumn_ = 0U;
  phraseTableDigit_ = 3U;
  instrumentTableNumber_ = 0U;
  instrumentTableRow_ = 0U;
  instrumentTableColumn_ = 0U;
  instrumentTableDigit_ = 3U;
  lastChain_ = 0U;
  lastPhrase_ = 0U;
  lastNote_ = 60U;
  lastInstrument_ = 0U;
  lastCommand_ = FourCC::InstrumentCommandNone;
  lastParameter_ = 0U;
  selectionClipboard_.fill(0U);
  selectionClipboardPage_ = Ui2TrackerPage::None;
  selectionClipboardStartColumn_ = 0U;
  selectionClipboardWidth_ = 0U;
  selectionClipboardHeight_ = 0U;
  soloMuteMask_.fill(false);
  soloActive_ = false;
  auditionOwned_ = false;
  projectMutationGeneration_ = 0U;
}

Ui2TrackerGridSessionState Ui2TrackerSessionModelPort::LoadGridSession() const {
  const TrackerSessionState &editor = session_.EditorState();
  return {
      .activePage = activePage_,
      .track = static_cast<std::uint8_t>(
          std::clamp(editor.songX_, 0, SONG_CHANNEL_COUNT - 1)),
      .songVisibleRow =
          static_cast<std::uint8_t>(std::clamp(editor.songY_, 0, 15)),
      .songRowOffset = static_cast<std::uint8_t>(
          std::clamp(editor.songOffset_, 0, SONG_ROW_COUNT - 16)),
      .chainNumber = static_cast<std::uint8_t>(
          std::clamp(editor.currentChain_, 0, CHAIN_COUNT - 1)),
      .chainRow =
          static_cast<std::uint8_t>(std::clamp(editor.chainRow_, 0, 15)),
      .chainColumn =
          static_cast<std::uint8_t>(std::clamp(editor.chainCol_, 0, 1)),
      .phraseNumber = static_cast<std::uint8_t>(
          std::clamp(editor.currentPhrase_, 0, PHRASE_COUNT - 1)),
      .phraseRow = phraseRow_,
      .phraseColumn = phraseColumn_,
      .phraseDigit = phraseDigit_,
      .phraseTableNumber = phraseTableNumber_,
      .phraseTableRow = phraseTableRow_,
      .phraseTableColumn = phraseTableColumn_,
      .phraseTableDigit = phraseTableDigit_,
      .instrumentTableNumber = instrumentTableNumber_,
      .instrumentTableRow = instrumentTableRow_,
      .instrumentTableColumn = instrumentTableColumn_,
      .instrumentTableDigit = instrumentTableDigit_,
      .liveMode = Player::GetInstance()->GetSequencerMode() == SM_LIVE,
  };
}

void Ui2TrackerSessionModelPort::StoreGridNavigation(
    const Ui2TrackerGridNavigationState &state) {
  TrackerSessionState &editor = session_.EditorState();
  activePage_ = state.activePage;
  editor.songX_ = state.track;
  editor.songY_ = state.songVisibleRow;
  editor.songOffset_ = state.songRowOffset;
  editor.currentChain_ = state.chainNumber;
  editor.chainRow_ = state.chainRow;
  editor.chainCol_ = state.chainColumn;
  editor.currentPhrase_ = state.phraseNumber;
  phraseRow_ = state.phraseRow;
  // Player's audition path reads phraseCurPos_ directly. Keep it in lockstep
  // with UI2 navigation so audition begins on the visible row, not row 00.
  editor.phraseCurPos_ = state.phraseRow;
  phraseColumn_ = state.phraseColumn;
  phraseDigit_ = state.phraseDigit;
  if (state.tablePage == Ui2TrackerPage::InstrumentTable) {
    instrumentTableNumber_ = state.tableNumber;
    instrumentTableRow_ = state.tableRow;
    instrumentTableColumn_ = state.tableColumn;
    instrumentTableDigit_ = state.tableDigit;
  } else {
    phraseTableNumber_ = state.tableNumber;
    phraseTableRow_ = state.tableRow;
    phraseTableColumn_ = state.tableColumn;
    phraseTableDigit_ = state.tableDigit;
  }
  if (state.activePage == Ui2TrackerPage::PhraseTable)
    editor.currentTable_ = phraseTableNumber_;
  else if (state.activePage == Ui2TrackerPage::InstrumentTable)
    editor.currentTable_ = instrumentTableNumber_;
  editor.playMode_ = state.liveMode ? PM_LIVE : PM_SONG;
}

void Ui2TrackerSessionModelPort::ApplyGridCommand(
    const Ui2TrackerCommand &command) {
  bool storageMutated = false;
  switch (command.type) {
  case Ui2TrackerCommandType::AdjustCell:
    ApplyAdjustCell(command);
    break;
  case Ui2TrackerCommandType::AdjustSelection:
    ApplyAdjustSelection(command);
    break;
  case Ui2TrackerCommandType::CutCell:
    ApplyCutCell(command);
    break;
  case Ui2TrackerCommandType::PasteLast:
    ApplyPasteLast(command);
    break;
  case Ui2TrackerCommandType::AllocateNext:
    storageMutated = ApplyAllocateNext(command);
    break;
  case Ui2TrackerCommandType::CloneCell:
    storageMutated = ApplyCloneCell(command);
    break;
  case Ui2TrackerCommandType::SelectTrack:
    if (command.sourcePage == Ui2TrackerPage::Chain)
      (void)ResolveTargetPage(Ui2TrackerPage::Chain, command.value,
                              command.row);
    else if (command.sourcePage == Ui2TrackerPage::Phrase)
      (void)ResolveTargetPage(
          Ui2TrackerPage::Phrase, command.value,
          static_cast<std::uint8_t>(session_.EditorState().chainRow_));
    else if (command.sourcePage == Ui2TrackerPage::PhraseTable ||
             command.sourcePage == Ui2TrackerPage::InstrumentTable)
      (void)ResolveTableTrack(command.sourcePage, command.value);
    else
      session_.EditorState().songX_ = command.value;
    break;
  case Ui2TrackerCommandType::SelectNumber:
    if (command.sourcePage == Ui2TrackerPage::Chain)
      session_.EditorState().currentChain_ = command.value;
    else if (command.sourcePage == Ui2TrackerPage::Phrase)
      session_.EditorState().currentPhrase_ = command.value;
    else if (command.sourcePage == Ui2TrackerPage::PhraseTable)
      phraseTableNumber_ = command.value;
    else if (command.sourcePage == Ui2TrackerPage::InstrumentTable)
      instrumentTableNumber_ = command.value;
    break;
  case Ui2TrackerCommandType::WarpVertical:
    if (command.sourcePage == Ui2TrackerPage::Chain) {
      (void)WarpChainSongPosition(command.track, command.value);
    } else if (command.sourcePage == Ui2TrackerPage::Phrase) {
      TrackerSessionState &editor = session_.EditorState();
      const int previousChainRow = editor.chainRow_;
      const int targetChainRow =
          std::clamp(previousChainRow + command.value, 0,
                     PHRASES_PER_CHAIN - 1);
      if (targetChainRow != previousChainRow &&
          ResolveTargetPage(Ui2TrackerPage::Phrase, command.track,
                            static_cast<std::uint8_t>(targetChainRow))) {
        // Plain row navigation treats adjacent phrases as one continuous
        // stream: 00 + UP lands on the previous phrase's 0F, while 0F + DOWN
        // lands on the next phrase's 00. OPTION-held phrase changes keep the
        // current row and therefore do not satisfy these edge predicates.
        if (!command.flag && command.row == 0U && command.value < 0)
          phraseRow_ = STEPS_PER_PHRASE - 1U;
        else if (!command.flag &&
                 command.row == STEPS_PER_PHRASE - 1U && command.value > 0)
          phraseRow_ = 0U;
      }
    }
    break;
  case Ui2TrackerCommandType::SetLiveMode:
    Player::GetInstance()->SetSequencerMode(command.flag ? SM_LIVE : SM_SONG);
    break;
  case Ui2TrackerCommandType::SwitchPage:
    ApplySwitchPage(command);
    break;
  case Ui2TrackerCommandType::StartPlayback:
  case Ui2TrackerCommandType::StartImmediate:
  case Ui2TrackerCommandType::StopPlayback:
  case Ui2TrackerCommandType::ToggleMute:
  case Ui2TrackerCommandType::ToggleSolo:
  case Ui2TrackerCommandType::UnmuteAll:
  case Ui2TrackerCommandType::NudgeTempo:
  case Ui2TrackerCommandType::StartAudition:
  case Ui2TrackerCommandType::StopAudition:
    ApplyTransport(command);
    break;
  case Ui2TrackerCommandType::JumpSection: {
    TrackerSessionState &editor = session_.EditorState();
    Song &song = session_.ProjectModel().song_;
    int current = std::clamp<int>(command.row, 0, SONG_ROW_COUNT - 1);
    const int direction = command.value < 0 ? -1 : 1;
    bool foundGap = false;
    bool foundTarget = false;
    for (int count = 0; count < SONG_ROW_COUNT; ++count) {
      const std::uint8_t value =
          song.data_[current * SONG_CHANNEL_COUNT + command.track];
      if (foundGap && value != 0xFFU) {
        foundTarget = true;
        break;
      }
      foundGap = foundGap || value == 0xFFU;
      current = (current + direction + SONG_ROW_COUNT) % SONG_ROW_COUNT;
    }
    // A completely empty track has no section to select; a completely full
    // track has no separating gap. In both cases a full circular scan must be
    // a no-op instead of manufacturing a different cursor for UP only.
    if (!foundTarget)
      break;
    if (direction < 0) {
      while (current > 0 &&
             song.data_[current * SONG_CHANNEL_COUNT + command.track] != 0xFFU)
        --current;
      if (song.data_[current * SONG_CHANNEL_COUNT + command.track] == 0xFFU &&
          current + 1 < SONG_ROW_COUNT)
        ++current;
    }
    const int oldOffset =
        std::clamp<int>(editor.songOffset_, 0, SONG_ROW_COUNT - 16);
    int offset = oldOffset;
    if (current < offset || current >= offset + 16)
      offset = std::clamp(current - 4, 0, SONG_ROW_COUNT - 16);
    editor.songOffset_ = offset;
    editor.songY_ = current - offset;
    break;
  }
  case Ui2TrackerCommandType::CommitValueEdits:
  case Ui2TrackerCommandType::None:
    break;
  case Ui2TrackerCommandType::CopySelection:
    (void)ApplyCopySelection(command, false);
    break;
  case Ui2TrackerCommandType::CutSelection:
    storageMutated = ApplyCopySelection(command, true);
    break;
  case Ui2TrackerCommandType::PasteSelection:
    storageMutated = ApplyPasteSelection(command);
    break;
  }

  // Grid storage is raw fixed-capacity song memory and does not publish
  // Observable notifications. Expose a monotonic mutation generation so the
  // application lifecycle can mark its autosave coordinator dirty without
  // placing autosave state or timing in this model adapter. A no-op edit may
  // conservatively advance the generation; navigation and clipboard-only
  // commands never do.
  switch (command.type) {
  case Ui2TrackerCommandType::AdjustCell:
  case Ui2TrackerCommandType::AdjustSelection:
  case Ui2TrackerCommandType::CutCell:
  case Ui2TrackerCommandType::PasteLast:
    ++projectMutationGeneration_;
    break;
  case Ui2TrackerCommandType::AllocateNext:
  case Ui2TrackerCommandType::CloneCell:
  case Ui2TrackerCommandType::CutSelection:
  case Ui2TrackerCommandType::PasteSelection:
    if (storageMutated)
      ++projectMutationGeneration_;
    break;
  case Ui2TrackerCommandType::SelectTrack:
  case Ui2TrackerCommandType::SelectNumber:
  case Ui2TrackerCommandType::WarpVertical:
  case Ui2TrackerCommandType::SetLiveMode:
  case Ui2TrackerCommandType::SwitchPage:
  case Ui2TrackerCommandType::StartPlayback:
  case Ui2TrackerCommandType::StartImmediate:
  case Ui2TrackerCommandType::StopPlayback:
  case Ui2TrackerCommandType::ToggleMute:
  case Ui2TrackerCommandType::ToggleSolo:
  case Ui2TrackerCommandType::UnmuteAll:
  case Ui2TrackerCommandType::NudgeTempo:
  case Ui2TrackerCommandType::StartAudition:
  case Ui2TrackerCommandType::StopAudition:
  case Ui2TrackerCommandType::JumpSection:
  case Ui2TrackerCommandType::CommitValueEdits:
  case Ui2TrackerCommandType::CopySelection:
  case Ui2TrackerCommandType::None:
    break;
  }
}

void Ui2TrackerSessionModelPort::ApplyAdjustCell(
    const Ui2TrackerCommand &command) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (command.sourcePage == Ui2TrackerPage::Song) {
    std::uint8_t &cell =
        song.data_[command.row * SONG_CHANNEL_COUNT + command.track];
    cell = AdjustByte(cell, command.value, CHAIN_COUNT - 1U, false);
    lastChain_ = cell;
    song.chain_.SetUsed(cell);
    return;
  }
  if (command.sourcePage == Ui2TrackerPage::Chain) {
    const int index = editor.currentChain_ * PHRASES_PER_CHAIN + command.row;
    if (command.column == 0U) {
      std::uint8_t &cell = song.chain_.data_[index];
      cell = AdjustByte(cell, command.value, PHRASE_COUNT - 1U, false, true);
      lastPhrase_ = cell;
      song.phrase_.SetUsed(cell);
    } else {
      // This byte is semantically signed. Saturating in signed space keeps
      // negative octave/fine edits correct and preserves the 3-glyph UI
      // contract instead of wrapping through an unsigned FF value.
      song.chain_.transpose_[index] = Ui2ChainTranspose::Adjust(
          song.chain_.transpose_[index], command.value);
    }
    return;
  }

  if (command.sourcePage == Ui2TrackerPage::Phrase) {
    Phrase &phrase = song.phrase_;
    const int index = editor.currentPhrase_ * STEPS_PER_PHRASE + command.row;
    const std::int16_t noteDelta = DirectionDelta(command.direction, 12);
    switch (command.column) {
    case 0: {
      const bool playableCell = phrase.note_[index] != NO_NOTE;
      phrase.note_[index] = AdjustPhraseNote(
          session_.ProjectModel(), phrase,
          static_cast<std::uint8_t>(editor.currentPhrase_), command.row,
          phrase.note_[index], noteDelta);
      if (playableCell)
        lastNote_ = phrase.note_[index];
      if (Player *player = Player::GetInstance();
          playableCell && auditionOwned_ && player->IsRunning()) {
        player->Stop();
        player->OnStartButton(
            PM_AUDITION,
            static_cast<std::uint8_t>(
                std::clamp(editor.songX_, 0, SONG_CHANNEL_COUNT - 1)),
            false,
            static_cast<std::uint8_t>(
                std::clamp(editor.chainRow_, 0, PHRASES_PER_CHAIN - 1)));
      }
      break;
    }
    case 1:
      phrase.instr_[index] =
          AdjustByte(phrase.instr_[index],
                     DirectionDelta(command.direction, 16),
                     MAX_INSTRUMENT_COUNT - 1U, true);
      lastInstrument_ = phrase.instr_[index];
      break;
    case 2:
      phrase.cmd1_[index] =
          AdjustCommand(phrase.cmd1_[index], command.direction, false);
      lastCommand_ = phrase.cmd1_[index];
      break;
    case 3:
      phrase.param1_[index] = CommandList::RangeLimitCommandParam(
          phrase.cmd1_[index],
          static_cast<std::uint16_t>(phrase.param1_[index] + command.value));
      lastParameter_ = phrase.param1_[index];
      break;
    case 4:
      phrase.cmd2_[index] =
          AdjustCommand(phrase.cmd2_[index], command.direction, false);
      lastCommand_ = phrase.cmd2_[index];
      break;
    case 5:
      phrase.param2_[index] = CommandList::RangeLimitCommandParam(
          phrase.cmd2_[index],
          static_cast<std::uint16_t>(phrase.param2_[index] + command.value));
      lastParameter_ = phrase.param2_[index];
      break;
    default:
      break;
    }
    return;
  }

  if (command.sourcePage == Ui2TrackerPage::PhraseTable ||
      command.sourcePage == Ui2TrackerPage::InstrumentTable) {
    const std::uint8_t tableNumber =
        command.sourcePage == Ui2TrackerPage::InstrumentTable
            ? instrumentTableNumber_
            : phraseTableNumber_;
    Table &table = TableHolder::GetInstance()->GetTable(tableNumber);
    FourCC *commands[3] = {table.cmd1_, table.cmd2_, table.cmd3_};
    std::uint16_t *parameters[3] = {table.param1_, table.param2_,
                                    table.param3_};
    const std::uint8_t group = command.column / 2U;
    if ((command.column & 1U) == 0U) {
      commands[group][command.row] =
          AdjustCommand(commands[group][command.row], command.direction, true);
      lastCommand_ = commands[group][command.row];
    } else {
      parameters[group][command.row] = CommandList::RangeLimitCommandParam(
          commands[group][command.row],
          static_cast<std::uint16_t>(parameters[group][command.row] +
                                     command.value));
      lastParameter_ = parameters[group][command.row];
    }
  }
}

void Ui2TrackerSessionModelPort::ApplyAdjustSelection(
    const Ui2TrackerCommand &command) {
  SelectionRect selection{};
  if (!ResolveSelectionRect(command.sourcePage, command.selection, selection))
    return;
  for (unsigned column = selection.left; column <= selection.right; ++column) {
    for (unsigned row = selection.top; row <= selection.bottom; ++row) {
      Ui2TrackerCommand cell = command;
      cell.type = Ui2TrackerCommandType::AdjustCell;
      cell.column = static_cast<std::uint8_t>(column);
      cell.row = static_cast<std::uint8_t>(row);
      if (command.sourcePage == Ui2TrackerPage::Song)
        cell.track = cell.column;
      cell.value = SelectionCellDelta(command.sourcePage, cell.column,
                                      command.direction);
      ApplyAdjustCell(cell);
    }
  }
}

void Ui2TrackerSessionModelPort::ApplySwitchPage(
    const Ui2TrackerCommand &command) {
  (void)ResolveTargetPage(command.targetPage, command.track, command.row);
  activePage_ = command.targetPage;
}

void Ui2TrackerSessionModelPort::ApplyCutCell(
    const Ui2TrackerCommand &command) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (command.sourcePage == Ui2TrackerPage::Song) {
    song.data_[command.row * SONG_CHANNEL_COUNT + command.track] = 0xFFU;
  } else if (command.sourcePage == Ui2TrackerPage::Chain) {
    const int index = editor.currentChain_ * PHRASES_PER_CHAIN + command.row;
    if (command.column == 0U)
      song.chain_.data_[index] = 0xFFU;
    else
      song.chain_.transpose_[index] = 0;
  } else if (command.sourcePage == Ui2TrackerPage::Phrase) {
    Phrase &phrase = song.phrase_;
    const int index = editor.currentPhrase_ * STEPS_PER_PHRASE + command.row;
    switch (command.column) {
    case 0:
      if (phrase.note_[index] == NO_NOTE) {
        phrase.note_[index] = NOTE_OFF;
      } else {
        phrase.note_[index] = NO_NOTE;
        // Legacy single-cell Note cut deliberately spans the paired INS cell
        // so a deleted note cannot leave a hidden instrument assignment.
        phrase.instr_[index] = 0xFFU;
      }
      break;
    case 1:
      phrase.instr_[index] = 0xFFU;
      break;
    case 2:
      phrase.cmd1_[index] = FourCC::InstrumentCommandNone;
      phrase.param1_[index] = 0;
      break;
    case 3:
      phrase.param1_[index] = 0;
      break;
    case 4:
      phrase.cmd2_[index] = FourCC::InstrumentCommandNone;
      phrase.param2_[index] = 0;
      break;
    case 5:
      phrase.param2_[index] = 0;
      break;
    default:
      break;
    }
  } else if (command.sourcePage == Ui2TrackerPage::PhraseTable ||
             command.sourcePage == Ui2TrackerPage::InstrumentTable) {
    const std::uint8_t tableNumber =
        command.sourcePage == Ui2TrackerPage::InstrumentTable
            ? instrumentTableNumber_
            : phraseTableNumber_;
    Table &table = TableHolder::GetInstance()->GetTable(tableNumber);
    FourCC *commands[3] = {table.cmd1_, table.cmd2_, table.cmd3_};
    std::uint16_t *parameters[3] = {table.param1_, table.param2_,
                                    table.param3_};
    const std::uint8_t group = command.column / 2U;
    if ((command.column & 1U) == 0U) {
      commands[group][command.row] = FourCC::InstrumentCommandNone;
      parameters[group][command.row] = 0U;
    } else {
      parameters[group][command.row] = 0U;
    }
  }
}

void Ui2TrackerSessionModelPort::ApplyPasteLast(
    const Ui2TrackerCommand &command) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (command.sourcePage == Ui2TrackerPage::Song) {
    std::uint8_t &cell =
        song.data_[command.row * SONG_CHANNEL_COUNT + command.track];
    if (cell == 0xFFU) {
      cell = lastChain_;
      // A raw reference pasted into an empty cell must participate in future
      // allocation. Otherwise GetNext() can recycle and overwrite that Chain.
      song.chain_.SetUsed(cell);
    } else {
      lastChain_ = cell;
    }
  } else if (command.sourcePage == Ui2TrackerPage::Chain) {
    std::uint8_t &cell =
        song.chain_.data_[editor.currentChain_ * 16 + command.row];
    if (cell == 0xFFU) {
      cell = lastPhrase_;
      song.phrase_.SetUsed(cell);
    } else {
      lastPhrase_ = cell;
    }
  } else if (command.sourcePage == Ui2TrackerPage::Phrase) {
    Phrase &phrase = song.phrase_;
    const int index = editor.currentPhrase_ * 16 + command.row;
    if (command.column == 0U) {
      if (phrase.note_[index] == NO_NOTE) {
        phrase.note_[index] = lastNote_;
        // A newly entered note is immediately playable. Match the established
        // tracker behavior by carrying the last selected instrument into the
        // same row instead of leaving an orphan note with I--.
        phrase.instr_[index] = lastInstrument_;
      } else {
        lastNote_ = phrase.note_[index];
        // I-- inherits the previous instrument at playback time. Treat it as
        // absence here too: visiting such a note must not replace the last
        // explicit instrument with the empty sentinel, or the next new note
        // would be created without the instrument UI2 promises to carry.
        if (phrase.instr_[index] != 0xFFU)
          lastInstrument_ = phrase.instr_[index];
      }
    } else if (command.column == 1U) {
      if (phrase.instr_[index] == 0xFFU)
        phrase.instr_[index] = lastInstrument_;
      else
        lastInstrument_ = phrase.instr_[index];
    } else if (command.column == 2U || command.column == 4U) {
      FourCC &cell =
          command.column == 2U ? phrase.cmd1_[index] : phrase.cmd2_[index];
      if (cell == FourCC::InstrumentCommandNone)
        cell = lastCommand_;
      else
        lastCommand_ = cell;
    } else if (command.column == 3U || command.column == 5U) {
      const FourCC effect =
          command.column == 3U ? phrase.cmd1_[index] : phrase.cmd2_[index];
      std::uint16_t &cell = command.column == 3U ? phrase.param1_[index]
                                                : phrase.param2_[index];
      if (cell == 0U)
        cell = CommandList::RangeLimitCommandParam(effect, lastParameter_);
      else
        lastParameter_ = cell;
    }
  } else if (command.sourcePage == Ui2TrackerPage::PhraseTable ||
             command.sourcePage == Ui2TrackerPage::InstrumentTable) {
    const std::uint8_t tableNumber =
        command.sourcePage == Ui2TrackerPage::InstrumentTable
            ? instrumentTableNumber_
            : phraseTableNumber_;
    Table &table = TableHolder::GetInstance()->GetTable(tableNumber);
    FourCC *commands[3] = {table.cmd1_, table.cmd2_, table.cmd3_};
    std::uint16_t *parameters[3] = {table.param1_, table.param2_,
                                    table.param3_};
    const std::uint8_t group = command.column / 2U;
    if ((command.column & 1U) == 0U) {
      FourCC &cell = commands[group][command.row];
      if (cell == FourCC::InstrumentCommandNone)
        cell = lastCommand_;
      else
        lastCommand_ = cell;
    } else {
      std::uint16_t &cell = parameters[group][command.row];
      if (cell == 0U) {
        cell = CommandList::RangeLimitCommandParam(
            commands[group][command.row], lastParameter_);
      } else {
        lastParameter_ = cell;
      }
    }
  }
}

bool Ui2TrackerSessionModelPort::ApplyAllocateNext(
    const Ui2TrackerCommand &command) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (!IsGridCell(command.sourcePage, command.row, command.column))
    return false;
  if (command.sourcePage == Ui2TrackerPage::Song) {
    if (command.track >= SONG_CHANNEL_COUNT)
      return false;
    const unsigned short next = song.chain_.GetNext();
    if (next != NO_MORE_CHAIN) {
      song.data_[command.row * SONG_CHANNEL_COUNT + command.track] = next;
      lastChain_ = next;
      return true;
    }
  } else if (command.sourcePage == Ui2TrackerPage::Chain &&
             command.column == 0U) {
    const unsigned short next = song.phrase_.GetNext();
    if (next != NO_MORE_PHRASE) {
      song.chain_.data_[editor.currentChain_ * 16 + command.row] = next;
      lastPhrase_ = next;
      return true;
    }
  } else if (command.sourcePage == Ui2TrackerPage::Phrase) {
    const int index = editor.currentPhrase_ * STEPS_PER_PHRASE + command.row;
    if (command.column == 1U) {
      InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
      if (bank == nullptr)
        return false;
      const unsigned short slot = bank->GetNextFreeInstrumentSlotId();
      if (slot == NO_MORE_INSTRUMENT)
        return false;
      const unsigned short next = bank->GetNextAndAssignID(
          IT_NONE, static_cast<unsigned char>(slot));
      if (next == NO_MORE_INSTRUMENT)
        return false;
      song.phrase_.instr_[index] = static_cast<std::uint8_t>(next);
      lastInstrument_ = static_cast<std::uint8_t>(next);
      return true;
    }
    if (command.column == 3U || command.column == 5U) {
      FourCC &effect = command.column == 3U ? song.phrase_.cmd1_[index]
                                            : song.phrase_.cmd2_[index];
      if (effect != FourCC::InstrumentCommandTable)
        return false;
      const unsigned short next = TableHolder::GetInstance()->GetNext();
      if (next == NO_MORE_TABLE)
        return false;
      std::uint16_t &parameter =
          command.column == 3U ? song.phrase_.param1_[index]
                               : song.phrase_.param2_[index];
      parameter = next;
      lastParameter_ = next;
      return true;
    }
  }
  return false;
}

bool Ui2TrackerSessionModelPort::ApplyCloneCell(
    const Ui2TrackerCommand &command) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (!IsGridCell(command.sourcePage, command.row, command.column))
    return false;
  const auto cloneTableReference = [this](FourCC effect,
                                          std::uint16_t &parameter) {
    if (effect != FourCC::InstrumentCommandTable || parameter >= TABLE_COUNT)
      return false;
    TableHolder *tables = TableHolder::GetInstance();
    // Persisted references do not rebuild TableHolder's allocation bitmap.
    // Mark the referenced source before allocating so Clone() cannot select
    // the source slot as its destination. Keep it marked even if allocation
    // is exhausted: a referenced empty table is still not a free table.
    tables->SetUsed(parameter);
    const int next = tables->Clone(parameter);
    if (next == NO_MORE_TABLE)
      return false;
    parameter = static_cast<std::uint16_t>(next);
    lastParameter_ = parameter;
    return true;
  };
  if (command.sourcePage == Ui2TrackerPage::Song) {
    if (command.track >= SONG_CHANNEL_COUNT)
      return false;
    std::uint8_t &cell =
        song.data_[command.row * SONG_CHANNEL_COUNT + command.track];
    if (cell == 0xFFU)
      return false;
    // Persisted projects store raw slot references. Register the referenced
    // source before allocation so clone never mistakes loaded data for a free
    // slot; the referenced Chain/Phrase bytes themselves stay unchanged.
    song.chain_.SetUsed(cell);
    const unsigned short next = song.chain_.GetNext();
    if (next == NO_MORE_CHAIN)
      return false;
    std::copy_n(song.chain_.data_ + cell * 16, 16,
                song.chain_.data_ + next * 16);
    std::copy_n(song.chain_.transpose_ + cell * 16, 16,
                song.chain_.transpose_ + next * 16);
    cell = next;
    lastChain_ = static_cast<std::uint8_t>(next);
    return true;
  } else if (command.sourcePage == Ui2TrackerPage::Chain &&
             command.column == 0U) {
    std::uint8_t &cell =
        song.chain_.data_[editor.currentChain_ * 16 + command.row];
    if (cell >= PHRASE_COUNT)
      return false;
    // Apply the same loaded-project rule to Phrase references.
    song.phrase_.SetUsed(cell);
    const unsigned short next = song.phrase_.GetNext();
    if (next == NO_MORE_PHRASE)
      return false;
    const int source = cell * 16;
    const int destination = next * 16;
    std::copy_n(song.phrase_.note_ + source, 16,
                song.phrase_.note_ + destination);
    std::copy_n(song.phrase_.instr_ + source, 16,
                song.phrase_.instr_ + destination);
    std::copy_n(song.phrase_.cmd1_ + source, 16,
                song.phrase_.cmd1_ + destination);
    std::copy_n(song.phrase_.param1_ + source, 16,
                song.phrase_.param1_ + destination);
    std::copy_n(song.phrase_.cmd2_ + source, 16,
                song.phrase_.cmd2_ + destination);
    std::copy_n(song.phrase_.param2_ + source, 16,
                song.phrase_.param2_ + destination);
    cell = next;
    lastPhrase_ = static_cast<std::uint8_t>(next);
    return true;
  } else if (command.sourcePage == Ui2TrackerPage::Phrase &&
             command.column == 1U) {
    InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
    if (bank == nullptr)
      return false;
    const int index = editor.currentPhrase_ * STEPS_PER_PHRASE + command.row;
    std::uint8_t &cell = song.phrase_.instr_[index];
    if (cell >= MAX_INSTRUMENT_COUNT)
      return false;
    I_Instrument *source = bank->GetInstrument(cell);
    if (source == nullptr || source->GetType() == IT_NONE ||
        bank->GetNextFreeInstrumentSlotId() == NO_MORE_INSTRUMENT) {
      return false;
    }
    const unsigned short next = bank->Clone(cell);
    if (next == NO_MORE_INSTRUMENT || next >= MAX_INSTRUMENT_COUNT)
      return false;
    cell = static_cast<std::uint8_t>(next);
    lastInstrument_ = cell;
    return true;
  } else if (command.sourcePage == Ui2TrackerPage::Phrase &&
             (command.column == 3U || command.column == 5U)) {
    const int index = editor.currentPhrase_ * STEPS_PER_PHRASE + command.row;
    FourCC &effect = command.column == 3U ? song.phrase_.cmd1_[index]
                                          : song.phrase_.cmd2_[index];
    std::uint16_t &parameter =
        command.column == 3U ? song.phrase_.param1_[index]
                             : song.phrase_.param2_[index];
    return cloneTableReference(effect, parameter);
  } else if ((command.sourcePage == Ui2TrackerPage::PhraseTable ||
              command.sourcePage == Ui2TrackerPage::InstrumentTable) &&
             (command.column & 1U) != 0U) {
    const std::uint8_t tableNumber =
        command.sourcePage == Ui2TrackerPage::PhraseTable
            ? phraseTableNumber_
            : instrumentTableNumber_;
    Table &table = TableHolder::GetInstance()->GetTable(tableNumber);
    const std::uint8_t group = command.column / 2U;
    FourCC *commands[3] = {table.cmd1_, table.cmd2_, table.cmd3_};
    std::uint16_t *parameters[3] = {table.param1_, table.param2_,
                                    table.param3_};
    return cloneTableReference(commands[group][command.row],
                               parameters[group][command.row]);
  }
  return false;
}

bool Ui2TrackerSessionModelPort::ApplyCopySelection(
    const Ui2TrackerCommand &command, bool cut) {
  SelectionRect selection{};
  if (!ResolveSelectionRect(command.sourcePage, command.selection, selection))
    return false;
  selectionClipboardPage_ = command.sourcePage;
  selectionClipboardStartColumn_ = selection.left;
  selectionClipboardWidth_ =
      static_cast<std::uint8_t>(selection.right - selection.left + 1U);
  selectionClipboardHeight_ =
      static_cast<std::uint8_t>(selection.bottom - selection.top + 1U);
  bool storageMutated = false;
  for (std::uint8_t y = 0; y < selectionClipboardHeight_; ++y) {
    for (std::uint8_t x = 0; x < selectionClipboardWidth_; ++x) {
      const std::uint8_t row = static_cast<std::uint8_t>(selection.top + y);
      const std::uint8_t column = static_cast<std::uint8_t>(selection.left + x);
      const std::uint32_t value = ReadCell(command.sourcePage, row, column);
      selectionClipboard_[y * 8U + x] = value;
      if (cut) {
        ClearCell(command.sourcePage, row, column);
        storageMutated = storageMutated ||
                         ReadCell(command.sourcePage, row, column) != value;
      }
    }
  }
  return storageMutated;
}

bool Ui2TrackerSessionModelPort::ApplyPasteSelection(
    const Ui2TrackerCommand &command) {
  const auto isTablePage = [](Ui2TrackerPage page) {
    return page == Ui2TrackerPage::PhraseTable ||
           page == Ui2TrackerPage::InstrumentTable;
  };
  const auto isPhrasePage = [](Ui2TrackerPage page) {
    return page == Ui2TrackerPage::Phrase;
  };
  const bool phraseTableTransfer =
      (isPhrasePage(selectionClipboardPage_) &&
       isTablePage(command.sourcePage)) ||
      (isTablePage(selectionClipboardPage_) &&
       isPhrasePage(command.sourcePage));
  const bool compatiblePage =
      selectionClipboardPage_ == command.sourcePage ||
      (isTablePage(selectionClipboardPage_) &&
       isTablePage(command.sourcePage)) ||
      phraseTableTransfer;
  if (!compatiblePage || selectionClipboardWidth_ == 0U ||
      selectionClipboardHeight_ == 0U)
    return false;
  GridBounds bounds{};
  if (!ResolveGridBounds(command.sourcePage, bounds) ||
      command.column > bounds.maximumColumn || command.row > bounds.maximumRow)
    return false;
  // A tracker cell's raw integer is only meaningful together with its field
  // kind. Preserve the useful FX1->FX2 and Phrase<->Table workflows, but
  // reject NOTE->INS and command->parameter reinterpretation instead of
  // silently corrupting the destination.
  const auto fieldKind = [](Ui2TrackerPage page,
                            std::uint8_t column) -> std::uint8_t {
    if (page == Ui2TrackerPage::Song)
      return 0U;
    if (page == Ui2TrackerPage::Chain)
      return column;
    if (page == Ui2TrackerPage::Phrase) {
      if (column < 2U)
        return column;
      return static_cast<std::uint8_t>(2U + ((column - 2U) & 1U));
    }
    if (page == Ui2TrackerPage::PhraseTable ||
        page == Ui2TrackerPage::InstrumentTable)
      return static_cast<std::uint8_t>(2U + (column & 1U));
    return column;
  };
  for (std::uint8_t x = 0U; x < selectionClipboardWidth_; ++x) {
    const unsigned destination = static_cast<unsigned>(command.column) + x;
    if (destination > bounds.maximumColumn)
      break;
    if (fieldKind(
            selectionClipboardPage_,
            static_cast<std::uint8_t>(selectionClipboardStartColumn_ + x)) !=
        fieldKind(command.sourcePage, static_cast<std::uint8_t>(destination)))
      return false;
  }
  bool storageMutated = false;
  for (std::uint8_t y = 0; y < selectionClipboardHeight_; ++y) {
    const unsigned row = static_cast<unsigned>(command.row) + y;
    if (row > bounds.maximumRow)
      break;
    for (std::uint8_t x = 0; x < selectionClipboardWidth_; ++x) {
      const unsigned column = static_cast<unsigned>(command.column) + x;
      if (column > bounds.maximumColumn)
        break;
      const std::uint32_t value = selectionClipboard_[y * 8U + x];
      storageMutated =
          storageMutated ||
          ReadCell(command.sourcePage, static_cast<std::uint8_t>(row),
                   static_cast<std::uint8_t>(column)) != value;
      WriteCell(command.sourcePage, static_cast<std::uint8_t>(row),
                static_cast<std::uint8_t>(column), value);
    }
  }
  Song &song = session_.ProjectModel().song_;
  const auto registerReference = [&](std::uint8_t row, std::uint8_t column) {
    if (command.sourcePage == Ui2TrackerPage::Song) {
      const std::uint8_t chain = song.data_[row * SONG_CHANNEL_COUNT + column];
      if (chain < CHAIN_COUNT)
        song.chain_.SetUsed(chain);
      return;
    }
    if (command.sourcePage == Ui2TrackerPage::Chain) {
      if (column == 0U) {
        const std::uint8_t phrase = song.chain_.data_[
            session_.EditorState().currentChain_ * PHRASES_PER_CHAIN + row];
        if (phrase < PHRASE_COUNT)
          song.phrase_.SetUsed(phrase);
      }
      return;
    }
    FourCC effect = FourCC::InstrumentCommandNone;
    std::uint16_t parameter = 0U;
    if (command.sourcePage == Ui2TrackerPage::Phrase) {
      if (column < 2U)
        return;
      const int index = session_.EditorState().currentPhrase_ *
                            STEPS_PER_PHRASE +
                        row;
      const std::uint8_t group = static_cast<std::uint8_t>((column - 2U) / 2U);
      effect = group == 0U ? song.phrase_.cmd1_[index]
                           : song.phrase_.cmd2_[index];
      parameter = group == 0U ? song.phrase_.param1_[index]
                              : song.phrase_.param2_[index];
    } else {
      const std::uint8_t tableNumber =
          command.sourcePage == Ui2TrackerPage::PhraseTable
              ? phraseTableNumber_
              : instrumentTableNumber_;
      Table &table = TableHolder::GetInstance()->GetTable(tableNumber);
      const std::uint8_t group = column / 2U;
      const FourCC *commands[3] = {table.cmd1_, table.cmd2_, table.cmd3_};
      const std::uint16_t *parameters[3] = {table.param1_, table.param2_,
                                           table.param3_};
      effect = commands[group][row];
      parameter = parameters[group][row];
    }
    if (effect == FourCC::InstrumentCommandTable && parameter < TABLE_COUNT)
      TableHolder::GetInstance()->SetUsed(parameter);
  };
  for (std::uint8_t y = 0; y < selectionClipboardHeight_; ++y) {
    const unsigned row = static_cast<unsigned>(command.row) + y;
    if (row > bounds.maximumRow)
      break;
    for (std::uint8_t x = 0; x < selectionClipboardWidth_; ++x) {
      const unsigned column = static_cast<unsigned>(command.column) + x;
      if (column > bounds.maximumColumn)
        break;
      registerReference(static_cast<std::uint8_t>(row),
                        static_cast<std::uint8_t>(column));
    }
  }
  return storageMutated;
}

std::uint32_t Ui2TrackerSessionModelPort::ReadCell(Ui2TrackerPage page,
                                                   std::uint8_t row,
                                                   std::uint8_t column) const {
  const TrackerSessionState &editor = session_.EditorState();
  const Song &song = session_.ProjectModel().song_;
  if (page == Ui2TrackerPage::Song)
    return song.data_[row * SONG_CHANNEL_COUNT + column];
  if (page == Ui2TrackerPage::Chain) {
    const int index = editor.currentChain_ * 16 + row;
    return column == 0U ? song.chain_.data_[index]
                        : song.chain_.transpose_[index];
  }
  if (page == Ui2TrackerPage::Phrase) {
    const Phrase &phrase = song.phrase_;
    const int index = editor.currentPhrase_ * 16 + row;
    switch (column) {
    case 0:
      return phrase.note_[index];
    case 1:
      return phrase.instr_[index];
    case 2:
      return static_cast<std::uint32_t>(phrase.cmd1_[index]);
    case 3:
      return phrase.param1_[index];
    case 4:
      return static_cast<std::uint32_t>(phrase.cmd2_[index]);
    case 5:
      return phrase.param2_[index];
    default:
      return 0U;
    }
  }
  if (page == Ui2TrackerPage::PhraseTable ||
      page == Ui2TrackerPage::InstrumentTable) {
    const std::uint8_t number = page == Ui2TrackerPage::PhraseTable
                                    ? phraseTableNumber_
                                    : instrumentTableNumber_;
    const Table &table = TableHolder::GetInstance()->GetTable(number);
    const FourCC *commands[3] = {table.cmd1_, table.cmd2_, table.cmd3_};
    const std::uint16_t *parameters[3] = {table.param1_, table.param2_,
                                          table.param3_};
    const std::uint8_t group = column / 2U;
    return (column & 1U) == 0U
               ? static_cast<std::uint32_t>(commands[group][row])
               : parameters[group][row];
  }
  return 0U;
}

void Ui2TrackerSessionModelPort::WriteCell(Ui2TrackerPage page,
                                           std::uint8_t row,
                                           std::uint8_t column,
                                           std::uint32_t value) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (page == Ui2TrackerPage::Song) {
    song.data_[row * SONG_CHANNEL_COUNT + column] =
        static_cast<std::uint8_t>(value);
    return;
  }
  if (page == Ui2TrackerPage::Chain) {
    const int index = editor.currentChain_ * 16 + row;
    if (column == 0U)
      song.chain_.data_[index] = static_cast<std::uint8_t>(value);
    else
      song.chain_.transpose_[index] = static_cast<std::uint8_t>(value);
    return;
  }
  if (page == Ui2TrackerPage::Phrase) {
    Phrase &phrase = song.phrase_;
    const int index = editor.currentPhrase_ * 16 + row;
    switch (column) {
    case 0:
      phrase.note_[index] = static_cast<std::uint8_t>(value);
      break;
    case 1:
      phrase.instr_[index] = static_cast<std::uint8_t>(value);
      break;
    case 2:
      phrase.cmd1_[index] = static_cast<FourCC::enum_type>(value);
      break;
    case 3:
      phrase.param1_[index] = static_cast<std::uint16_t>(value);
      break;
    case 4:
      phrase.cmd2_[index] = static_cast<FourCC::enum_type>(value);
      break;
    case 5:
      phrase.param2_[index] = static_cast<std::uint16_t>(value);
      break;
    }
    return;
  }
  if (page == Ui2TrackerPage::PhraseTable ||
      page == Ui2TrackerPage::InstrumentTable) {
    const std::uint8_t number = page == Ui2TrackerPage::PhraseTable
                                    ? phraseTableNumber_
                                    : instrumentTableNumber_;
    Table &table = TableHolder::GetInstance()->GetTable(number);
    FourCC *commands[3] = {table.cmd1_, table.cmd2_, table.cmd3_};
    std::uint16_t *parameters[3] = {table.param1_, table.param2_,
                                    table.param3_};
    const std::uint8_t group = column / 2U;
    if ((column & 1U) == 0U)
      commands[group][row] = static_cast<FourCC::enum_type>(value);
    else
      parameters[group][row] = static_cast<std::uint16_t>(value);
  }
}

void Ui2TrackerSessionModelPort::ClearCell(Ui2TrackerPage page,
                                           std::uint8_t row,
                                           std::uint8_t column) {
  if (!IsGridCell(page, row, column))
    return;
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (page == Ui2TrackerPage::Song) {
    song.data_[row * SONG_CHANNEL_COUNT + column] = 0xFFU;
    return;
  }
  if (page == Ui2TrackerPage::Chain) {
    const int index = editor.currentChain_ * PHRASES_PER_CHAIN + row;
    if (column == 0U)
      song.chain_.data_[index] = 0xFFU;
    else
      song.chain_.transpose_[index] = 0U;
    return;
  }
  if (page == Ui2TrackerPage::Phrase) {
    Phrase &phrase = song.phrase_;
    const int index = editor.currentPhrase_ * STEPS_PER_PHRASE + row;
    switch (column) {
    case 0:
      phrase.note_[index] = NO_NOTE;
      return;
    case 1:
      phrase.instr_[index] = 0xFFU;
      return;
    case 2:
      phrase.cmd1_[index] = FourCC::InstrumentCommandNone;
      return;
    case 3:
      phrase.param1_[index] = 0U;
      return;
    case 4:
      phrase.cmd2_[index] = FourCC::InstrumentCommandNone;
      return;
    case 5:
      phrase.param2_[index] = 0U;
      return;
    default:
      return;
    }
  }
  const std::uint8_t tableNumber = page == Ui2TrackerPage::PhraseTable
                                       ? phraseTableNumber_
                                       : instrumentTableNumber_;
  Table &table = TableHolder::GetInstance()->GetTable(tableNumber);
  FourCC *commands[3] = {table.cmd1_, table.cmd2_, table.cmd3_};
  std::uint16_t *parameters[3] = {table.param1_, table.param2_, table.param3_};
  const std::uint8_t group = column / 2U;
  if ((column & 1U) == 0U)
    commands[group][row] = FourCC::InstrumentCommandNone;
  else
    parameters[group][row] = 0U;
}

void Ui2TrackerSessionModelPort::ApplyTransport(
    const Ui2TrackerCommand &command) {
  Player *player = Player::GetInstance();
  switch (command.type) {
  case Ui2TrackerCommandType::StartPlayback:
    auditionOwned_ = false;
    if (command.sourcePage == Ui2TrackerPage::Song) {
      const std::uint8_t from =
          command.selection.active ? command.selection.Left() : command.track;
      const std::uint8_t to =
          command.selection.active ? command.selection.Right() : command.track;
      player->OnSongStartButton(from, to, command.flag, false);
    } else if (command.flag) {
      // SHIFT+PLAY is global on M8-style context pages. Reuse the same global
      // transport boundary as Project/Mixer so Player reads the visible Song
      // cursor instead of its legacy lastSongPos_ continuation register.
      Ui2ToggleSongTransportAtCursor(
          *player, PM_SONG, session_.EditorState().songX_,
          static_cast<std::uint8_t>(SONG_CHANNEL_COUNT));
    } else {
      // Phrase and both Table views edit a step inside the current phrase,
      // but Player::OnStartButton expects the position of that phrase inside
      // its Chain. The legacy views therefore pass chainRow_, not the visible
      // Phrase/Table cursor row. Chain itself is the only non-Song page whose
      // cursor row is already the required chain position.
      const std::uint8_t chainPosition =
          command.sourcePage == Ui2TrackerPage::Chain
              ? command.row
              : static_cast<std::uint8_t>(std::clamp(
                    session_.EditorState().chainRow_, 0,
                    PHRASES_PER_CHAIN - 1));
      player->OnStartButton(
          command.sourcePage == Ui2TrackerPage::Chain ? PM_CHAIN : PM_PHRASE,
          command.track, false, chainPosition);
    }
    break;
  case Ui2TrackerCommandType::StartImmediate:
    auditionOwned_ = false;
    player->OnSongStartButton(command.track, command.track, false, true);
    break;
  case Ui2TrackerCommandType::StopPlayback:
    auditionOwned_ = false;
    player->Stop();
    break;
  case Ui2TrackerCommandType::StopAudition:
    if (auditionOwned_) {
      player->Stop();
      auditionOwned_ = false;
    }
    break;
  case Ui2TrackerCommandType::ToggleMute:
    {
      const std::uint8_t from =
          command.sourcePage == Ui2TrackerPage::Song && command.selection.active
              ? std::min<std::uint8_t>(command.selection.Left(),
                                       SONG_CHANNEL_COUNT - 1U)
              : std::min<std::uint8_t>(command.track,
                                       SONG_CHANNEL_COUNT - 1U);
      const std::uint8_t to =
          command.sourcePage == Ui2TrackerPage::Song && command.selection.active
              ? std::min<std::uint8_t>(command.selection.Right(),
                                       SONG_CHANNEL_COUNT - 1U)
              : from;
      for (std::uint8_t track = from; track <= to; ++track)
        player->SetChannelMute(track, !player->IsChannelMuted(track));
    }
    break;
  case Ui2TrackerCommandType::ToggleSolo: {
    if (soloActive_) {
      for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track)
        player->SetChannelMute(track, soloMuteMask_[track]);
      soloActive_ = false;
      break;
    }
    const std::uint8_t from =
        command.sourcePage == Ui2TrackerPage::Song && command.selection.active
            ? std::min<std::uint8_t>(command.selection.Left(),
                                     SONG_CHANNEL_COUNT - 1U)
            : std::min<std::uint8_t>(command.track, SONG_CHANNEL_COUNT - 1U);
    const std::uint8_t to =
        command.sourcePage == Ui2TrackerPage::Song && command.selection.active
            ? std::min<std::uint8_t>(command.selection.Right(),
                                     SONG_CHANNEL_COUNT - 1U)
            : from;
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track) {
      soloMuteMask_[track] = player->IsChannelMuted(track);
      player->SetChannelMute(track, track < from || track > to);
    }
    soloActive_ = true;
    break;
  }
  case Ui2TrackerCommandType::UnmuteAll:
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track)
      player->SetChannelMute(track, false);
    soloActive_ = false;
    break;
  case Ui2TrackerCommandType::NudgeTempo:
    session_.ProjectModel().NudgeTempo(command.value);
    break;
  case Ui2TrackerCommandType::StartAudition:
    // EDIT is also a data-entry gesture. Never let its transient audition
    // commandeer or stop an already-running Song/Chain/Phrase transport.
    if (player->IsRunning() && session_.EditorState().playMode_ != PM_AUDITION) {
      auditionOwned_ = false;
      break;
    }
    if (player->IsRunning())
      player->Stop();
    player->OnStartButton(
        PM_AUDITION, command.track, false,
        static_cast<std::uint8_t>(std::clamp(
            session_.EditorState().chainRow_, 0, PHRASES_PER_CHAIN - 1)));
    auditionOwned_ = true;
    break;
  default:
    break;
  }
}

bool Ui2TrackerSessionModelPort::ResolveTableTrack(Ui2TrackerPage page,
                                                   std::uint8_t track) {
  TrackerSessionState &editor = session_.EditorState();
  const int savedTrack = editor.songX_;
  const int savedChain = editor.currentChain_;
  const int savedPhrase = editor.currentPhrase_;
  const int savedInstrument = editor.currentInstrumentID_;
  const int savedTable = editor.currentTable_;
  const std::uint8_t savedPhraseTable = phraseTableNumber_;
  const std::uint8_t savedInstrumentTable = instrumentTableNumber_;

  const auto rollback = [&]() {
    editor.songX_ = savedTrack;
    editor.currentChain_ = savedChain;
    editor.currentPhrase_ = savedPhrase;
    editor.currentInstrumentID_ = savedInstrument;
    editor.currentTable_ = savedTable;
    phraseTableNumber_ = savedPhraseTable;
    instrumentTableNumber_ = savedInstrumentTable;
  };

  if (!ResolveTargetPage(
          Ui2TrackerPage::Phrase, track,
          static_cast<std::uint8_t>(
              std::clamp(editor.chainRow_, 0, PHRASES_PER_CHAIN - 1)))) {
    rollback();
    return false;
  }

  const std::uint8_t phraseRow =
      std::min<std::uint8_t>(phraseRow_, STEPS_PER_PHRASE - 1U);
  bool resolved = false;
  if (page == Ui2TrackerPage::PhraseTable) {
    resolved = PreparePageNavigation(Ui2TrackerPage::Phrase,
                                     Ui2TrackerPage::PhraseTable, track,
                                     phraseRow);
  } else {
    resolved = PreparePageNavigation(Ui2TrackerPage::Phrase,
                                     Ui2TrackerPage::Instrument, track,
                                     phraseRow) &&
               PreparePageNavigation(Ui2TrackerPage::Instrument,
                                     Ui2TrackerPage::InstrumentTable, track,
                                     phraseRow);
  }
  if (!resolved)
    rollback();
  return resolved;
}

bool Ui2TrackerSessionModelPort::WarpChainSongPosition(std::uint8_t track,
                                                       std::int16_t delta) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  const int previousAbsolute =
      std::clamp(editor.songOffset_ + editor.songY_, 0, SONG_ROW_COUNT - 1);
  const int targetAbsolute =
      std::clamp(previousAbsolute + static_cast<int>(delta), 0,
                 SONG_ROW_COUNT - 1);
  if (targetAbsolute == previousAbsolute)
    return false;

  const std::uint8_t targetTrack =
      std::min<std::uint8_t>(track, SONG_CHANNEL_COUNT - 1U);
  const std::uint8_t chain =
      song.data_[targetAbsolute * SONG_CHANNEL_COUNT + targetTrack];
  if (chain == 0xFFU)
    return false;

  int offset = std::clamp(editor.songOffset_, 0, SONG_ROW_COUNT - 16);
  if (targetAbsolute < offset)
    offset = targetAbsolute;
  else if (targetAbsolute >= offset + 16)
    offset = targetAbsolute - 15;
  editor.songOffset_ = offset;
  editor.songY_ = targetAbsolute - offset;
  editor.songX_ = targetTrack;
  editor.currentChain_ = chain;
  return true;
}

bool Ui2TrackerSessionModelPort::PreparePageNavigation(Ui2TrackerPage source,
                                                       Ui2TrackerPage target,
                                                       std::uint8_t track,
                                                       std::uint8_t row) {
  if (source == Ui2TrackerPage::Phrase &&
      target == Ui2TrackerPage::Instrument) {
    TrackerSessionState &editor = session_.EditorState();
    const Phrase &phrase = session_.ProjectModel().song_.phrase_;
    std::uint8_t instrument =
        phrase.instr_[editor.currentPhrase_ * STEPS_PER_PHRASE +
                      std::min<std::uint8_t>(row, STEPS_PER_PHRASE - 1U)];
    if (instrument == 0xFFU)
      instrument = lastInstrument_;
    if (instrument >= MAX_INSTRUMENT_COUNT)
      return false;
    editor.currentInstrumentID_ = instrument;
    return true;
  }
  if (source == Ui2TrackerPage::Phrase &&
      target == Ui2TrackerPage::PhraseTable) {
    TrackerSessionState &editor = session_.EditorState();
    const Phrase &phrase = session_.ProjectModel().song_.phrase_;
    const int index = editor.currentPhrase_ * STEPS_PER_PHRASE +
                      std::min<std::uint8_t>(row, STEPS_PER_PHRASE - 1U);
    const FourCC commands[2] = {phrase.cmd1_[index], phrase.cmd2_[index]};
    const std::uint16_t parameters[2] = {phrase.param1_[index],
                                         phrase.param2_[index]};
    for (std::uint8_t effect = 0U; effect < 2U; ++effect) {
      if (commands[effect] == FourCC::InstrumentCommandTable) {
        phraseTableNumber_ = static_cast<std::uint8_t>(
            parameters[effect] & (TABLE_COUNT - 1U));
        editor.currentTable_ = phraseTableNumber_;
        return true;
      }
    }
    // Legacy still opens Table when neither FX slot is TBL, preserving the
    // last selected table number.
    return true;
  }
  if (source == Ui2TrackerPage::Instrument &&
      target == Ui2TrackerPage::InstrumentTable) {
    TrackerSessionState &editor = session_.EditorState();
    InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
    if (bank == nullptr || editor.currentInstrumentID_ < 0 ||
        editor.currentInstrumentID_ >= MAX_INSTRUMENT_COUNT)
      return false;
    I_Instrument *instrument = bank->GetInstrument(editor.currentInstrumentID_);
    if (instrument == nullptr)
      return false;
    const int table = instrument->GetTable();
    if (table != VAR_OFF && table >= 0 && table < TABLE_COUNT) {
      instrumentTableNumber_ = static_cast<std::uint8_t>(table);
      editor.currentTable_ = instrumentTableNumber_;
    }
    return true;
  }
  if (source == Ui2TrackerPage::Song && target == Ui2TrackerPage::Chain)
    return ResolveTargetPage(Ui2TrackerPage::Chain, track, row);
  if (source == Ui2TrackerPage::Chain && target == Ui2TrackerPage::Phrase)
    return ResolveTargetPage(Ui2TrackerPage::Phrase, track, row);
  return true;
}

bool Ui2TrackerSessionModelPort::ResolveTargetPage(Ui2TrackerPage page,
                                                   std::uint8_t track,
                                                   std::uint8_t row) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  const std::uint8_t targetTrack =
      std::min<std::uint8_t>(track, SONG_CHANNEL_COUNT - 1U);
  if (page == Ui2TrackerPage::Chain) {
    const std::uint8_t chain =
        song.data_[(editor.songOffset_ + editor.songY_) * SONG_CHANNEL_COUNT +
                   targetTrack];
    if (chain == 0xFFU)
      return false;
    editor.songX_ = targetTrack;
    editor.currentChain_ = chain;
    return true;
  } else if (page == Ui2TrackerPage::Phrase) {
    const std::uint8_t chainRow = std::min<std::uint8_t>(row, 15U);
    std::uint8_t chain = static_cast<std::uint8_t>(editor.currentChain_);
    if (targetTrack != static_cast<std::uint8_t>(editor.songX_)) {
      chain =
          song.data_[(editor.songOffset_ + editor.songY_) * SONG_CHANNEL_COUNT +
                     targetTrack];
      if (chain == 0xFFU)
        return false;
    }
    const std::uint8_t phrase = song.chain_.data_[chain * 16 + chainRow];
    if (phrase == 0xFFU)
      return false;
    editor.songX_ = targetTrack;
    editor.currentChain_ = chain;
    editor.chainRow_ = chainRow;
    editor.currentPhrase_ = phrase;
    return true;
  }
  return true;
}

} // namespace ui2
