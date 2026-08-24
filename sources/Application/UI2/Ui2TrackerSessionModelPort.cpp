/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2TrackerSessionModelPort.h"

#include "Application/Instruments/CommandList.h"
#include "Application/Model/Table.h"
#include "Application/Player/Player.h"

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

} // namespace

Ui2TrackerSessionModelPort::Ui2TrackerSessionModelPort(
    TrackerApplicationSession &session)
    : session_(session) {}

Ui2TrackerGridSessionState
Ui2TrackerSessionModelPort::LoadGridSession() const {
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
    ApplyAllocateNext(command);
    break;
  case Ui2TrackerCommandType::CloneCell:
    ApplyCloneCell(command);
    break;
  case Ui2TrackerCommandType::SelectTrack:
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
    ResolveTargetPage(command.sourcePage, command.track,
                      static_cast<std::uint8_t>(
                          std::clamp<int>(command.row + command.value, 0, 15)));
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
  case Ui2TrackerCommandType::OpenRecord:
  case Ui2TrackerCommandType::ToggleMute:
  case Ui2TrackerCommandType::ToggleSolo:
  case Ui2TrackerCommandType::UnmuteAll:
  case Ui2TrackerCommandType::NudgeTempo:
  case Ui2TrackerCommandType::StartAudition:
  case Ui2TrackerCommandType::StopAudition:
    ApplyTransport(command);
    break;
  case Ui2TrackerCommandType::JumpSection:
  case Ui2TrackerCommandType::CommitValueEdits:
  case Ui2TrackerCommandType::None:
    break;
  case Ui2TrackerCommandType::CopySelection:
    ApplyCopySelection(command, false);
    break;
  case Ui2TrackerCommandType::CutSelection:
    ApplyCopySelection(command, true);
    break;
  case Ui2TrackerCommandType::PasteSelection:
    ApplyPasteSelection(command);
    break;
  }
}

void Ui2TrackerSessionModelPort::ApplyAdjustCell(
    const Ui2TrackerCommand &command) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (command.sourcePage == Ui2TrackerPage::Song) {
    std::uint8_t &cell = song.data_[command.row * SONG_CHANNEL_COUNT +
                                   command.track];
    cell = AdjustByte(cell, command.value, CHAIN_COUNT - 1U, false);
    lastChain_ = cell;
    song.chain_.SetUsed(cell);
    return;
  }
  if (command.sourcePage == Ui2TrackerPage::Chain) {
    const int index = editor.currentChain_ * PHRASES_PER_CHAIN + command.row;
    std::uint8_t &cell = command.column == 0U ? song.chain_.data_[index]
                                              : song.chain_.transpose_[index];
    cell = AdjustByte(cell, command.value,
                      command.column == 0U ? PHRASE_COUNT - 1U : 0xFFU,
                      command.column != 0U,
                      command.column == 0U);
    if (command.column == 0U) {
      lastPhrase_ = cell;
      song.phrase_.SetUsed(cell);
    }
    return;
  }

  if (command.sourcePage == Ui2TrackerPage::Phrase) {
    Phrase &phrase = song.phrase_;
    const int index = editor.currentPhrase_ * STEPS_PER_PHRASE + command.row;
    const std::int16_t delta = DirectionDelta(command.direction, 12);
    switch (command.column) {
    case 0:
      phrase.note_[index] = AdjustByte(
          phrase.note_[index] == NOTE_OFF ? NOTE_C3 : phrase.note_[index],
          delta, HIGHEST_NOTE, true, false);
      lastNote_ = phrase.note_[index];
      break;
    case 1:
      phrase.instr_[index] = AdjustByte(phrase.instr_[index], delta,
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
      commands[group][command.row] = AdjustCommand(
          commands[group][command.row], command.direction, true);
      lastCommand_ = commands[group][command.row];
    } else {
      parameters[group][command.row] = CommandList::RangeLimitCommandParam(
          commands[group][command.row], static_cast<std::uint16_t>(
                                            parameters[group][command.row] +
                                            command.value));
      lastParameter_ = parameters[group][command.row];
    }
  }
}

void Ui2TrackerSessionModelPort::ApplyAdjustSelection(
    const Ui2TrackerCommand &command) {
  for (std::uint8_t column = command.selection.Left();
       column <= command.selection.Right(); ++column) {
    for (std::uint8_t row = command.selection.Top();
         row <= command.selection.Bottom(); ++row) {
      Ui2TrackerCommand cell = command;
      cell.type = Ui2TrackerCommandType::AdjustCell;
      cell.column = column;
      cell.row = row;
      ApplyAdjustCell(cell);
    }
  }
}

void Ui2TrackerSessionModelPort::ApplySwitchPage(
    const Ui2TrackerCommand &command) {
  ResolveTargetPage(command.targetPage, command.track, command.row);
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
      phrase.note_[index] =
          phrase.note_[index] == NO_NOTE ? NOTE_OFF : NO_NOTE;
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
  }
}

void Ui2TrackerSessionModelPort::ApplyPasteLast(
    const Ui2TrackerCommand &command) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (command.sourcePage == Ui2TrackerPage::Song) {
    std::uint8_t &cell = song.data_[command.row * SONG_CHANNEL_COUNT +
                                   command.track];
    if (cell == 0xFFU)
      cell = lastChain_;
    else
      lastChain_ = cell;
  } else if (command.sourcePage == Ui2TrackerPage::Chain) {
    std::uint8_t &cell = song.chain_.data_[editor.currentChain_ * 16 +
                                          command.row];
    if (cell == 0xFFU)
      cell = lastPhrase_;
    else
      lastPhrase_ = cell;
  } else if (command.sourcePage == Ui2TrackerPage::Phrase) {
    Phrase &phrase = song.phrase_;
    const int index = editor.currentPhrase_ * 16 + command.row;
    if (command.column == 0U) {
      if (phrase.note_[index] == NO_NOTE)
        phrase.note_[index] = lastNote_;
      else
        lastNote_ = phrase.note_[index];
    } else if (command.column == 1U) {
      if (phrase.instr_[index] == 0xFFU)
        phrase.instr_[index] = lastInstrument_;
      else
        lastInstrument_ = phrase.instr_[index];
    } else if (command.column == 2U || command.column == 4U) {
      FourCC &cell = command.column == 2U ? phrase.cmd1_[index]
                                          : phrase.cmd2_[index];
      if (cell == FourCC::InstrumentCommandNone)
        cell = lastCommand_;
      else
        lastCommand_ = cell;
    }
  }
}

void Ui2TrackerSessionModelPort::ApplyAllocateNext(
    const Ui2TrackerCommand &command) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (command.sourcePage == Ui2TrackerPage::Song) {
    const unsigned short next = song.chain_.GetNext();
    if (next != NO_MORE_CHAIN) {
      song.data_[command.row * SONG_CHANNEL_COUNT + command.track] = next;
      lastChain_ = next;
    }
  } else if (command.sourcePage == Ui2TrackerPage::Chain) {
    const unsigned short next = song.phrase_.GetNext();
    if (next != NO_MORE_PHRASE) {
      song.chain_.data_[editor.currentChain_ * 16 + command.row] = next;
      lastPhrase_ = next;
    }
  }
}

void Ui2TrackerSessionModelPort::ApplyCloneCell(
    const Ui2TrackerCommand &command) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  if (command.sourcePage == Ui2TrackerPage::Song) {
    std::uint8_t &cell = song.data_[command.row * SONG_CHANNEL_COUNT +
                                   command.track];
    if (cell == 0xFFU)
      return;
    const unsigned short next = song.chain_.GetNext();
    if (next == NO_MORE_CHAIN)
      return;
    std::copy_n(song.chain_.data_ + cell * 16, 16,
                song.chain_.data_ + next * 16);
    std::copy_n(song.chain_.transpose_ + cell * 16, 16,
                song.chain_.transpose_ + next * 16);
    cell = next;
  } else if (command.sourcePage == Ui2TrackerPage::Chain &&
             command.column == 0U) {
    std::uint8_t &cell =
        song.chain_.data_[editor.currentChain_ * 16 + command.row];
    if (cell == 0xFFU)
      return;
    const unsigned short next = song.phrase_.GetNext();
    if (next == NO_MORE_PHRASE)
      return;
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
  }
}

void Ui2TrackerSessionModelPort::ApplyCopySelection(
    const Ui2TrackerCommand &command, bool cut) {
  if (!command.selection.active)
    return;
  selectionClipboardPage_ = command.sourcePage;
  selectionClipboardWidth_ = static_cast<std::uint8_t>(
      command.selection.Right() - command.selection.Left() + 1U);
  selectionClipboardHeight_ = static_cast<std::uint8_t>(
      command.selection.Bottom() - command.selection.Top() + 1U);
  for (std::uint8_t y = 0; y < selectionClipboardHeight_; ++y) {
    for (std::uint8_t x = 0; x < selectionClipboardWidth_; ++x) {
      const std::uint8_t row =
          static_cast<std::uint8_t>(command.selection.Top() + y);
      const std::uint8_t column =
          static_cast<std::uint8_t>(command.selection.Left() + x);
      selectionClipboard_[y * 8U + x] =
          ReadCell(command.sourcePage, row, column);
      if (cut)
        ClearCell(command.sourcePage, row, column);
    }
  }
}

void Ui2TrackerSessionModelPort::ApplyPasteSelection(
    const Ui2TrackerCommand &command) {
  if (selectionClipboardPage_ != command.sourcePage ||
      selectionClipboardWidth_ == 0U || selectionClipboardHeight_ == 0U)
    return;
  const std::uint8_t maximumColumn =
      command.sourcePage == Ui2TrackerPage::Song
          ? 7U
          : command.sourcePage == Ui2TrackerPage::Chain ? 1U : 5U;
  const std::uint8_t maximumRow =
      command.sourcePage == Ui2TrackerPage::Song ? 127U : 15U;
  for (std::uint8_t y = 0; y < selectionClipboardHeight_; ++y) {
    const unsigned row = static_cast<unsigned>(command.row) + y;
    if (row > maximumRow)
      break;
    for (std::uint8_t x = 0; x < selectionClipboardWidth_; ++x) {
      const unsigned column = static_cast<unsigned>(command.column) + x;
      if (column > maximumColumn)
        break;
      WriteCell(command.sourcePage, static_cast<std::uint8_t>(row),
                static_cast<std::uint8_t>(column),
                selectionClipboard_[y * 8U + x]);
    }
  }
}

std::uint32_t Ui2TrackerSessionModelPort::ReadCell(
    Ui2TrackerPage page, std::uint8_t row, std::uint8_t column) const {
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
  Ui2TrackerCommand clear{};
  clear.type = Ui2TrackerCommandType::CutCell;
  clear.sourcePage = page;
  clear.row = row;
  clear.column = column;
  ApplyCutCell(clear);
}

void Ui2TrackerSessionModelPort::ApplyTransport(
    const Ui2TrackerCommand &command) {
  Player *player = Player::GetInstance();
  switch (command.type) {
  case Ui2TrackerCommandType::StartPlayback:
    if (command.sourcePage == Ui2TrackerPage::Song)
      player->OnSongStartButton(command.track, command.track, false, false);
    else
      player->OnStartButton(command.sourcePage == Ui2TrackerPage::Chain
                                ? PM_CHAIN
                                : PM_PHRASE,
                            command.track, true, command.row);
    break;
  case Ui2TrackerCommandType::StartImmediate:
    player->OnSongStartButton(command.track, command.track, false, true);
    break;
  case Ui2TrackerCommandType::StopPlayback:
  case Ui2TrackerCommandType::StopAudition:
    player->Stop();
    break;
  case Ui2TrackerCommandType::OpenRecord:
    if (!player->IsRunning())
      activePage_ = Ui2TrackerPage::Record;
    break;
  case Ui2TrackerCommandType::ToggleMute:
    player->SetChannelMute(command.track,
                           !player->IsChannelMuted(command.track));
    break;
  case Ui2TrackerCommandType::ToggleSolo: {
    const bool selectedMuted = player->IsChannelMuted(command.track);
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track)
      player->SetChannelMute(track,
                             track == command.track ? false : !selectedMuted);
    break;
  }
  case Ui2TrackerCommandType::UnmuteAll:
    for (std::uint8_t track = 0; track < SONG_CHANNEL_COUNT; ++track)
      player->SetChannelMute(track, false);
    break;
  case Ui2TrackerCommandType::NudgeTempo:
    session_.ProjectModel().NudgeTempo(command.value);
    break;
  case Ui2TrackerCommandType::StartAudition:
    player->OnStartButton(PM_AUDITION, command.track, false, command.row);
    break;
  default:
    break;
  }
}

void Ui2TrackerSessionModelPort::ResolveTargetPage(Ui2TrackerPage page,
                                                    std::uint8_t track,
                                                    std::uint8_t row) {
  TrackerSessionState &editor = session_.EditorState();
  Song &song = session_.ProjectModel().song_;
  editor.songX_ = std::min<std::uint8_t>(track, SONG_CHANNEL_COUNT - 1U);
  if (page == Ui2TrackerPage::Chain) {
    const std::uint8_t chain = song.data_[(editor.songOffset_ + editor.songY_) *
                                              SONG_CHANNEL_COUNT +
                                          editor.songX_];
    if (chain != 0xFFU)
      editor.currentChain_ = chain;
  } else if (page == Ui2TrackerPage::Phrase) {
    editor.chainRow_ = std::min<std::uint8_t>(row, 15U);
    const std::uint8_t phrase =
        song.chain_.data_[editor.currentChain_ * 16 + editor.chainRow_];
    if (phrase != 0xFFU)
      editor.currentPhrase_ = phrase;
  }
}

} // namespace ui2
