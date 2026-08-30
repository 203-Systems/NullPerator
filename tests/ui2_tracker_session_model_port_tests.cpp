#include "Application/Model/Table.h"
#include "Application/Player/Player.h"
#include "Application/UI2/Ui2GrooveCommandAdapter.h"
#include "Application/UI2/Ui2TrackerSessionModelPort.h"

#include "doctest/doctest.h"

#include <cstdint>

namespace {

using ui2::Ui2GridSelectionState;
using ui2::Ui2TrackerCommand;
using ui2::Ui2TrackerCommandType;
using ui2::Ui2TrackerEditDirection;
using ui2::Ui2TrackerPage;
using ui2::Ui2TrackerSessionModelPort;

Ui2TrackerCommand GridCommand(Ui2TrackerCommandType type, Ui2TrackerPage page,
                              std::uint8_t row, std::uint8_t column) {
  return ui2::Ui2MakeTrackerCommand(type, page, row, column, column);
}

Ui2TrackerCommand SelectionCommand(Ui2TrackerCommandType type,
                                   Ui2TrackerPage page, std::uint8_t left,
                                   std::uint8_t top, std::uint8_t right,
                                   std::uint8_t bottom) {
  Ui2TrackerCommand command = GridCommand(type, page, top, left);
  command.selection.Begin(left, top);
  command.selection.Follow(right, bottom);
  return command;
}

} // namespace

TEST_CASE("UI2 model port exposes one mutation generation for all workflows") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);

  CHECK(port.ProjectMutationGeneration() == 0U);
  port.MarkProjectMutated();
  CHECK(port.ProjectMutationGeneration() == 1U);
}

TEST_CASE("UI2 model port preserves raw Phrase clipboard data") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  session.EditorState().currentPhrase_ = 3;
  Phrase &phrase = session.ProjectModel().song_.phrase_;
  const int source = 3 * STEPS_PER_PHRASE + 1;
  phrase.note_[source] = 64U;
  phrase.instr_[source] = 7U;
  phrase.note_[source + 1] = 65U;
  phrase.instr_[source + 1] = 8U;

  port.ApplyGridCommand(SelectionCommand(Ui2TrackerCommandType::CopySelection,
                                         Ui2TrackerPage::Phrase, 0, 1, 1, 2));
  CHECK(port.ProjectMutationGeneration() == 0U);

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteSelection,
                                    Ui2TrackerPage::Phrase, 14, 0));
  const int destination = 3 * STEPS_PER_PHRASE + 14;
  CHECK(phrase.note_[destination] == 64U);
  CHECK(phrase.instr_[destination] == 7U);
  CHECK(phrase.note_[destination + 1] == 65U);
  CHECK(phrase.instr_[destination + 1] == 8U);
  CHECK(port.ProjectMutationGeneration() == 1U);

  port.ApplyGridCommand(SelectionCommand(Ui2TrackerCommandType::CutSelection,
                                         Ui2TrackerPage::Phrase, 0, 1, 1, 2));
  CHECK(phrase.note_[source] == NO_NOTE);
  CHECK(phrase.instr_[source] == 0xFFU);
  CHECK(phrase.note_[source + 1] == NO_NOTE);
  CHECK(phrase.instr_[source + 1] == 0xFFU);
  CHECK(port.ProjectMutationGeneration() == 2U);
}

TEST_CASE("UI2 model port rejects semantically incompatible Phrase paste") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Phrase &phrase = session.ProjectModel().song_.phrase_;
  phrase.note_[0] = 60U;
  phrase.instr_[0] = 7U;
  phrase.cmd1_[1] = FourCC::InstrumentCommandArpeggiator;
  phrase.param1_[1] = 0x1234U;

  port.ApplyGridCommand(SelectionCommand(Ui2TrackerCommandType::CopySelection,
                                         Ui2TrackerPage::Phrase, 0, 0, 1, 0));
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteSelection,
                                    Ui2TrackerPage::Phrase, 1, 1));
  CHECK(phrase.instr_[1] == 0xFFU);
  CHECK(phrase.cmd1_[1] == FourCC::InstrumentCommandArpeggiator);
  CHECK(port.ProjectMutationGeneration() == 0U);

  port.ApplyGridCommand(SelectionCommand(Ui2TrackerCommandType::CopySelection,
                                         Ui2TrackerPage::Phrase, 2, 1, 3, 1));
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteSelection,
                                    Ui2TrackerPage::Phrase, 2, 4));
  CHECK(phrase.cmd2_[2] == FourCC::InstrumentCommandArpeggiator);
  CHECK(phrase.param2_[2] == 0x1234U);
  CHECK(port.ProjectMutationGeneration() == 1U);
}

TEST_CASE("UI2 model port rejects empty and malformed selections") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Phrase &phrase = session.ProjectModel().song_.phrase_;
  phrase.note_[0] = 70U;

  port.ApplyGridCommand(SelectionCommand(Ui2TrackerCommandType::CopySelection,
                                         Ui2TrackerPage::Phrase, 0, 0, 0, 0));

  Ui2TrackerCommand empty = GridCommand(Ui2TrackerCommandType::CopySelection,
                                        Ui2TrackerPage::Phrase, 0, 0);
  port.ApplyGridCommand(empty);

  Ui2TrackerCommand malformed = SelectionCommand(
      Ui2TrackerCommandType::CopySelection, Ui2TrackerPage::Phrase, 0, 0, 0, 0);
  malformed.selection.activeColumn = 0xFFU;
  port.ApplyGridCommand(malformed);

  Ui2TrackerCommand oversized = SelectionCommand(
      Ui2TrackerCommandType::CopySelection, Ui2TrackerPage::Song, 0, 0, 7, 16);
  port.ApplyGridCommand(oversized);

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteSelection,
                                    Ui2TrackerPage::Phrase, 1, 0));
  CHECK(phrase.note_[1] == 70U);
  CHECK(port.ProjectMutationGeneration() == 1U);

  Ui2TrackerCommand invalidPaste = GridCommand(
      Ui2TrackerCommandType::PasteSelection, Ui2TrackerPage::Phrase, 1, 0);
  invalidPaste.column = 0xFFU;
  port.ApplyGridCommand(invalidPaste);
  CHECK(port.ProjectMutationGeneration() == 1U);
}

TEST_CASE("UI2 model port clips clipboard paste at Song boundaries") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  song.data_[0] = 0x12U;
  song.data_[1] = 0x13U;
  song.data_[SONG_CHANNEL_COUNT] = 0x22U;
  song.data_[SONG_CHANNEL_COUNT + 1] = 0x23U;
  port.ApplyGridCommand(SelectionCommand(Ui2TrackerCommandType::CopySelection,
                                         Ui2TrackerPage::Song, 0, 0, 1, 1));

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteSelection,
                                    Ui2TrackerPage::Song, 127, 7));
  CHECK(song.data_[127 * SONG_CHANNEL_COUNT + 7] == 0x12U);
  CHECK(port.ProjectMutationGeneration() == 1U);
}

TEST_CASE("UI2 Song controller jumps through the model port between sections") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  TrackerSessionState &editor = session.EditorState();
  editor.songX_ = 0;
  editor.songY_ = 1;
  editor.songOffset_ = 0;
  for (int row = 0; row <= 2; ++row)
    song.data_[row * SONG_CHANNEL_COUNT] = static_cast<std::uint8_t>(row);
  for (int row = 5; row <= 6; ++row)
    song.data_[row * SONG_CHANNEL_COUNT] = static_cast<std::uint8_t>(row);

  ui2::Ui2TrackerCommandExecutor executor(port);
  CHECK(executor.Handle(TrackerAction::Option, true).Empty());
  const auto next = executor.Handle(TrackerAction::Down, true);
  REQUIRE(next.count == 1U);
  CHECK(next[0].type == Ui2TrackerCommandType::JumpSection);
  CHECK(next[0].row == 1U);
  CHECK(next[0].track == 0U);
  CHECK(next[0].value == 1);
  CHECK(editor.songOffset_ + editor.songY_ == 5);
  CHECK(executor.ActiveState().rowOffset + executor.ActiveState().row == 5U);
  executor.Handle(TrackerAction::Down, false);
  executor.Handle(TrackerAction::Option, false);

  CHECK(executor.Handle(TrackerAction::Option, true).Empty());
  const auto previous = executor.Handle(TrackerAction::Up, true);
  REQUIRE(previous.count == 1U);
  CHECK(previous[0].type == Ui2TrackerCommandType::JumpSection);
  CHECK(previous[0].row == 5U);
  CHECK(previous[0].track == 0U);
  CHECK(previous[0].value == -1);
  CHECK(editor.songOffset_ + editor.songY_ == 0);
  CHECK(executor.ActiveState().rowOffset + executor.ActiveState().row == 0U);
}

TEST_CASE("UI2 Song JumpSection keeps the cursor when no target section exists") {
  struct NoTargetCase {
    bool populateEveryRow;
    TrackerAction direction;
    int commandValue;
  };
  constexpr NoTargetCase cases[] = {
      {false, TrackerAction::Up, -1},
      {false, TrackerAction::Down, 1},
      {true, TrackerAction::Up, -1},
      {true, TrackerAction::Down, 1},
  };

  for (const NoTargetCase &testCase : cases) {
    CAPTURE(testCase.populateEveryRow);
    CAPTURE(testCase.commandValue);
    TrackerApplicationSession session;
    Ui2TrackerSessionModelPort port(session);
    TrackerSessionState &editor = session.EditorState();
    Song &song = session.ProjectModel().song_;
    editor.songX_ = 3;
    editor.songY_ = 10;
    editor.songOffset_ = 32;
    if (testCase.populateEveryRow) {
      for (int row = 0; row < SONG_ROW_COUNT; ++row)
        song.data_[row * SONG_CHANNEL_COUNT + 3] = 0U;
    }

    ui2::Ui2TrackerCommandExecutor executor(port);
    CHECK(executor.Handle(TrackerAction::Option, true).Empty());
    const auto jump = executor.Handle(testCase.direction, true);
    REQUIRE(jump.count == 1U);
    CHECK(jump[0].type == Ui2TrackerCommandType::JumpSection);
    CHECK(jump[0].row == 42U);
    CHECK(jump[0].track == 3U);
    CHECK(jump[0].value == testCase.commandValue);

    CHECK(editor.songOffset_ == 32);
    CHECK(editor.songY_ == 10);
    CHECK(executor.ActiveState().rowOffset == 32U);
    CHECK(executor.ActiveState().row == 10U);
  }
}

TEST_CASE("UI2 model port cuts and pastes all Table cell kinds") {
  TableHolder::GetInstance()->Reset();
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Table &table = TableHolder::GetInstance()->GetTable(0);
  table.cmd1_[2] = FourCC::InstrumentCommandVolume;
  table.param1_[2] = 0x1234U;

  port.ApplyGridCommand(SelectionCommand(Ui2TrackerCommandType::CutSelection,
                                         Ui2TrackerPage::PhraseTable, 0, 2, 1,
                                         2));
  CHECK(table.cmd1_[2] == FourCC::InstrumentCommandNone);
  CHECK(table.param1_[2] == 0U);
  CHECK(port.ProjectMutationGeneration() == 1U);

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteSelection,
                                    Ui2TrackerPage::PhraseTable, 3, 2));
  CHECK(table.cmd2_[3] == FourCC::InstrumentCommandVolume);
  CHECK(table.param2_[3] == 0x1234U);
  CHECK(port.ProjectMutationGeneration() == 2U);
}

TEST_CASE("UI2 model port clones loaded Song chain references safely") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  constexpr int sourceChain = 0;
  constexpr int clonedChain = 1;
  // Simulate a persisted project whose raw slot reference was restored before
  // any allocator bookkeeping was reconstructed by this adapter.
  song.data_[4 * SONG_CHANNEL_COUNT + 3] = sourceChain;
  for (int row = 0; row < PHRASES_PER_CHAIN; ++row) {
    song.chain_.data_[sourceChain * PHRASES_PER_CHAIN + row] =
        static_cast<std::uint8_t>(20 + row);
    song.chain_.transpose_[sourceChain * PHRASES_PER_CHAIN + row] =
        static_cast<std::uint8_t>(row - 8);
  }

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CloneCell,
                                    Ui2TrackerPage::Song, 4, 3));
  CHECK(song.data_[4 * SONG_CHANNEL_COUNT + 3] == clonedChain);
  CHECK(song.chain_.IsUsed(sourceChain));
  CHECK(song.chain_.IsUsed(clonedChain));
  for (int row = 0; row < PHRASES_PER_CHAIN; ++row) {
    CHECK(song.chain_.data_[clonedChain * PHRASES_PER_CHAIN + row] ==
          song.chain_.data_[sourceChain * PHRASES_PER_CHAIN + row]);
    CHECK(song.chain_.transpose_[clonedChain * PHRASES_PER_CHAIN + row] ==
          song.chain_.transpose_[sourceChain * PHRASES_PER_CHAIN + row]);
  }
  CHECK(port.ProjectMutationGeneration() == 1U);
}

TEST_CASE("UI2 model port clones loaded Chain phrase references safely") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  session.EditorState().currentChain_ = 4;
  Song &song = session.ProjectModel().song_;
  constexpr int sourcePhrase = 0;
  constexpr int clonedPhrase = 1;
  song.chain_.data_[4 * PHRASES_PER_CHAIN + 6] = sourcePhrase;
  for (int row = 0; row < STEPS_PER_PHRASE; ++row) {
    const int source = sourcePhrase * STEPS_PER_PHRASE + row;
    song.phrase_.note_[source] = static_cast<std::uint8_t>(40 + row);
    song.phrase_.instr_[source] = static_cast<std::uint8_t>(row);
    song.phrase_.cmd1_[source] = FourCC::InstrumentCommandVolume;
    song.phrase_.param1_[source] = static_cast<std::uint16_t>(0x100 + row);
    song.phrase_.cmd2_[source] = FourCC::InstrumentCommandDelay;
    song.phrase_.param2_[source] = static_cast<std::uint16_t>(0x200 + row);
  }

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CloneCell,
                                    Ui2TrackerPage::Chain, 6, 0));
  CHECK(song.chain_.data_[4 * PHRASES_PER_CHAIN + 6] == clonedPhrase);
  CHECK(song.phrase_.IsUsed(sourcePhrase));
  CHECK(song.phrase_.IsUsed(clonedPhrase));
  for (int row = 0; row < STEPS_PER_PHRASE; ++row) {
    const int source = sourcePhrase * STEPS_PER_PHRASE + row;
    const int destination = clonedPhrase * STEPS_PER_PHRASE + row;
    CHECK(song.phrase_.note_[destination] == song.phrase_.note_[source]);
    CHECK(song.phrase_.instr_[destination] == song.phrase_.instr_[source]);
    CHECK(song.phrase_.cmd1_[destination] == song.phrase_.cmd1_[source]);
    CHECK(song.phrase_.param1_[destination] == song.phrase_.param1_[source]);
    CHECK(song.phrase_.cmd2_[destination] == song.phrase_.cmd2_[source]);
    CHECK(song.phrase_.param2_[destination] == song.phrase_.param2_[source]);
  }
  CHECK(port.ProjectMutationGeneration() == 1U);
}

TEST_CASE("UI2 model port clones the Phrase instrument into a free slot") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  InstrumentBank *bank = session.ProjectModel().GetInstrumentBank();
  REQUIRE(bank != nullptr);
  constexpr std::uint8_t sourceInstrument = 4U;
  REQUIRE(bank->GetNextAndAssignID(IT_SAMPLE, sourceInstrument) ==
          sourceInstrument);
  bank->SetInstrumentTable(sourceInstrument, 9);

  session.EditorState().currentPhrase_ = 2;
  constexpr std::uint8_t row = 5U;
  const int index = 2 * STEPS_PER_PHRASE + row;
  session.ProjectModel().song_.phrase_.instr_[index] = sourceInstrument;

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CloneCell,
                                    Ui2TrackerPage::Phrase, row, 1));

  constexpr std::uint8_t clonedInstrument = 0U;
  CHECK(session.ProjectModel().song_.phrase_.instr_[index] == clonedInstrument);
  I_Instrument *clone = bank->GetInstrument(clonedInstrument);
  REQUIRE(clone != nullptr);
  CHECK(clone->GetType() == IT_SAMPLE);
  CHECK(clone->GetTable() == 9);
  CHECK(port.ProjectMutationGeneration() == 1U);
}

TEST_CASE("UI2 Phrase instrument clone is a no-op when the bank is full") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  InstrumentBank *bank = session.ProjectModel().GetInstrumentBank();
  REQUIRE(bank != nullptr);
  for (std::uint8_t id = 0U; id < MAX_INSTRUMENT_COUNT; ++id) {
    REQUIRE(bank->GetNextAndAssignID(IT_SAMPLE, id) == id);
  }

  constexpr std::uint8_t sourceInstrument = 4U;
  session.ProjectModel().song_.phrase_.instr_[0] = sourceInstrument;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CloneCell,
                                    Ui2TrackerPage::Phrase, 0, 1));

  CHECK(session.ProjectModel().song_.phrase_.instr_[0] == sourceInstrument);
  CHECK(port.ProjectMutationGeneration() == 0U);
}

TEST_CASE("UI2 model port clones TBL references from Phrase and Table cells") {
  TableHolder *tables = TableHolder::GetInstance();
  tables->Reset();
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  constexpr std::uint8_t sourceTable = 4U;
  tables->GetTable(sourceTable).cmd1_[2] = FourCC::InstrumentCommandVolume;
  tables->GetTable(sourceTable).param1_[2] = 0x1234U;

  session.EditorState().currentPhrase_ = 2;
  constexpr std::uint8_t phraseRow = 5U;
  const int phraseIndex = 2 * STEPS_PER_PHRASE + phraseRow;
  session.ProjectModel().song_.phrase_.cmd1_[phraseIndex] =
      FourCC::InstrumentCommandTable;
  session.ProjectModel().song_.phrase_.param1_[phraseIndex] = sourceTable;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CloneCell,
                                    Ui2TrackerPage::Phrase, phraseRow, 3));

  constexpr std::uint8_t firstClone = 0U;
  CHECK(session.ProjectModel().song_.phrase_.param1_[phraseIndex] ==
        firstClone);
  CHECK(tables->GetTable(firstClone).cmd1_[2] ==
        FourCC::InstrumentCommandVolume);
  CHECK(tables->GetTable(firstClone).param1_[2] == 0x1234U);

  constexpr std::uint8_t tableRow = 7U;
  Table &visible = tables->GetTable(firstClone);
  visible.cmd2_[tableRow] = FourCC::InstrumentCommandTable;
  visible.param2_[tableRow] = sourceTable;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CloneCell,
                                    Ui2TrackerPage::PhraseTable, tableRow, 3));

  constexpr std::uint8_t secondClone = 1U;
  CHECK(visible.param2_[tableRow] == secondClone);
  CHECK(tables->GetTable(secondClone).cmd1_[2] ==
        FourCC::InstrumentCommandVolume);
  CHECK(tables->GetTable(secondClone).param1_[2] == 0x1234U);
  CHECK(port.ProjectMutationGeneration() == 2U);
}

TEST_CASE("UI2 model port reports clone allocation exhaustion as a no-op") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  song.data_[0] = 0U;
  for (unsigned index = 0; index < CHAIN_COUNT; ++index)
    song.chain_.SetUsed(static_cast<std::uint8_t>(index));

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CloneCell,
                                    Ui2TrackerPage::Song, 0, 0));
  CHECK(song.data_[0] == 0U);
  CHECK(port.ProjectMutationGeneration() == 0U);

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::AllocateNext,
                                    Ui2TrackerPage::Song, 1, 0));
  CHECK(song.data_[SONG_CHANNEL_COUNT] == 0xFFU);
  CHECK(port.ProjectMutationGeneration() == 0U);

  song.chain_.data_[0] = 0U;
  for (unsigned index = 0; index < PHRASE_COUNT; ++index)
    song.phrase_.SetUsed(static_cast<std::uint8_t>(index));
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CloneCell,
                                    Ui2TrackerPage::Chain, 0, 0));
  CHECK(song.chain_.data_[0] == 0U);
  CHECK(port.ProjectMutationGeneration() == 0U);

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::AllocateNext,
                                    Ui2TrackerPage::Chain, 1, 0));
  CHECK(song.chain_.data_[1] == 0xFFU);
  CHECK(port.ProjectMutationGeneration() == 0U);
}

TEST_CASE("UI2 model port allocates phrase FE before reporting exhaustion") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  for (unsigned index = 0; index < PHRASE_COUNT - 1U; ++index)
    song.phrase_.SetUsed(static_cast<std::uint8_t>(index));

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::AllocateNext,
                                    Ui2TrackerPage::Chain, 0, 0));
  CHECK(song.chain_.data_[0] == 0xFEU);
  CHECK(song.phrase_.IsUsed(0xFEU));

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::AllocateNext,
                                    Ui2TrackerPage::Chain, 1, 0));
  CHECK(song.chain_.data_[1] == 0xFFU);
}

TEST_CASE("UI2 model port registers pasted Chains and allocates Phrase entries") {
  TableHolder::GetInstance()->Reset();
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::Song, 0, 0));
  CHECK(song.data_[0] == 0U);
  CHECK(song.chain_.IsUsed(0U));

  session.EditorState().currentPhrase_ = 2;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::AllocateNext,
                                    Ui2TrackerPage::Phrase, 4, 1));
  const int phraseIndex = 2 * STEPS_PER_PHRASE + 4;
  CHECK(song.phrase_.instr_[phraseIndex] == 0U);
  CHECK(session.ProjectModel().GetInstrumentBank()->GetInstrument(0) != nullptr);

  song.phrase_.cmd1_[phraseIndex] = FourCC::InstrumentCommandTable;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::AllocateNext,
                                    Ui2TrackerPage::Phrase, 4, 3));
  CHECK(song.phrase_.param1_[phraseIndex] == 0U);
  CHECK(port.ProjectMutationGeneration() == 3U);
}

TEST_CASE("UI2 model port registers a pasted Phrase before allocating another") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::Chain, 0, 0));
  CHECK(song.chain_.data_[0] == 0U);
  CHECK(song.phrase_.IsUsed(0U));

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::AllocateNext,
                                    Ui2TrackerPage::Chain, 1, 0));
  CHECK(song.chain_.data_[1] == 1U);
  CHECK(song.phrase_.IsUsed(1U));
}

TEST_CASE("UI2 inherited Phrase instruments do not poison new Note entry") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Phrase &phrase = session.ProjectModel().song_.phrase_;
  session.EditorState().currentPhrase_ = 0;

  phrase.note_[0] = 60U;
  phrase.instr_[0] = 7U;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::Phrase, 0, 0));

  phrase.note_[1] = 64U;
  phrase.instr_[1] = 0xFFU;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::Phrase, 1, 0));

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::Phrase, 2, 0));
  CHECK(phrase.note_[2] == 64U);
  CHECK(phrase.instr_[2] == 7U);
}

TEST_CASE("UI2 model port pastes the last Phrase and Table FX values") {
  TableHolder::GetInstance()->Reset();
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  session.EditorState().currentPhrase_ = 2;

  const int phraseBase = 2 * STEPS_PER_PHRASE;
  song.phrase_.cmd1_[phraseBase] = FourCC::InstrumentCommandVolume;
  song.phrase_.param1_[phraseBase] = 0x1234U;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::Phrase, 0, 3));
  song.phrase_.cmd1_[phraseBase + 1] = FourCC::InstrumentCommandVolume;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::Phrase, 1, 3));
  CHECK(song.phrase_.param1_[phraseBase + 1] == 0x1234U);

  Table &table = TableHolder::GetInstance()->GetTable(0);
  table.cmd2_[3] = FourCC::InstrumentCommandKill;
  table.param2_[3] = 0x00BBU;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::PhraseTable, 3, 2));
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::PhraseTable, 3, 3));
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::PhraseTable, 4, 2));
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::PhraseTable, 4, 3));
  CHECK(table.cmd2_[4] == FourCC::InstrumentCommandKill);
  CHECK(table.param2_[4] == 0x00BBU);
}

TEST_CASE("UI2 model port synchronizes Phrase audition row and adjacent phrases") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  session.EditorState().currentChain_ = 3;
  session.EditorState().chainRow_ = 5;
  song.chain_.data_[3 * PHRASES_PER_CHAIN + 4] = 0x22U;
  song.chain_.data_[3 * PHRASES_PER_CHAIN + 6] = 0x23U;

  auto navigation = port.LoadGridSession();
  ui2::Ui2TrackerGridNavigationState state{
      .activePage = Ui2TrackerPage::Phrase,
      .track = navigation.track,
      .songVisibleRow = navigation.songVisibleRow,
      .songRowOffset = navigation.songRowOffset,
      .chainNumber = navigation.chainNumber,
      .chainRow = navigation.chainRow,
      .chainColumn = navigation.chainColumn,
      .phraseNumber = navigation.phraseNumber,
      .phraseRow = 0U,
      .phraseColumn = navigation.phraseColumn,
      .phraseDigit = navigation.phraseDigit,
      .tablePage = Ui2TrackerPage::PhraseTable,
      .tableNumber = navigation.phraseTableNumber,
      .tableRow = navigation.phraseTableRow,
      .tableColumn = navigation.phraseTableColumn,
      .tableDigit = navigation.phraseTableDigit,
      .liveMode = navigation.liveMode,
  };
  port.StoreGridNavigation(state);
  CHECK(session.EditorState().phraseCurPos_ == 0);

  Ui2TrackerCommand previous = GridCommand(
      Ui2TrackerCommandType::WarpVertical, Ui2TrackerPage::Phrase, 0, 0);
  previous.track = 0U;
  previous.value = -1;
  port.ApplyGridCommand(previous);
  CHECK(session.EditorState().chainRow_ == 4);
  CHECK(session.EditorState().currentPhrase_ == 0x22);
  CHECK(port.LoadGridSession().phraseRow == 15U);

  state.chainRow = 5U;
  state.phraseRow = 15U;
  port.StoreGridNavigation(state);
  Ui2TrackerCommand next = previous;
  next.row = 15U;
  next.value = 1;
  port.ApplyGridCommand(next);
  CHECK(session.EditorState().chainRow_ == 6);
  CHECK(session.EditorState().currentPhrase_ == 0x23);
  CHECK(port.LoadGridSession().phraseRow == 0U);

  state.chainRow = 5U;
  state.phraseRow = 0U;
  port.StoreGridNavigation(state);
  Ui2TrackerCommand quickPrevious = previous;
  quickPrevious.flag = true;
  port.ApplyGridCommand(quickPrevious);
  CHECK(session.EditorState().chainRow_ == 4);
  CHECK(session.EditorState().currentPhrase_ == 0x22);
  CHECK(port.LoadGridSession().phraseRow == 0U);
}

TEST_CASE("UI2 model port resolves Chain quick-select and vertical song position") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  TrackerSessionState &editor = session.EditorState();
  editor.songOffset_ = 8;
  editor.songY_ = 4; // Absolute song row 12.
  editor.songX_ = 2;
  editor.currentChain_ = 3;
  song.data_[12 * SONG_CHANNEL_COUNT + 3] = 9U;
  song.data_[13 * SONG_CHANNEL_COUNT + 3] = 10U;

  Ui2TrackerCommand track = GridCommand(
      Ui2TrackerCommandType::SelectTrack, Ui2TrackerPage::Chain, 6, 0);
  track.value = 3;
  port.ApplyGridCommand(track);
  CHECK(editor.songX_ == 3);
  CHECK(editor.currentChain_ == 9);

  Ui2TrackerCommand down = GridCommand(
      Ui2TrackerCommandType::WarpVertical, Ui2TrackerPage::Chain, 6, 0);
  down.track = 3U;
  down.value = 1;
  port.ApplyGridCommand(down);
  CHECK(editor.songOffset_ + editor.songY_ == 13);
  CHECK(editor.currentChain_ == 10);

  song.data_[12 * SONG_CHANNEL_COUNT + 3] = 0xFFU;
  Ui2TrackerCommand up = down;
  up.value = -1;
  port.ApplyGridCommand(up);
  CHECK(editor.songOffset_ + editor.songY_ == 13);
  CHECK(editor.currentChain_ == 10);
}

TEST_CASE("UI2 model port resolves Phrase Table context on track quick-select") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  TrackerSessionState &editor = session.EditorState();
  editor.songX_ = 0;
  editor.songY_ = 2;
  editor.songOffset_ = 0;
  editor.currentChain_ = 1;
  editor.chainRow_ = 4;
  editor.currentPhrase_ = 3;
  song.data_[2 * SONG_CHANNEL_COUNT] = 1U;
  song.data_[2 * SONG_CHANNEL_COUNT + 1] = 2U;
  song.chain_.data_[2 * PHRASES_PER_CHAIN + 4] = 5U;
  constexpr std::uint8_t phraseRow = 6U;
  const int phraseIndex = 5 * STEPS_PER_PHRASE + phraseRow;
  song.phrase_.cmd2_[phraseIndex] = FourCC::InstrumentCommandTable;
  song.phrase_.param2_[phraseIndex] = 0x0007U;

  const auto loaded = port.LoadGridSession();
  port.StoreGridNavigation({
      .activePage = Ui2TrackerPage::PhraseTable,
      .track = loaded.track,
      .songVisibleRow = loaded.songVisibleRow,
      .songRowOffset = loaded.songRowOffset,
      .chainNumber = loaded.chainNumber,
      .chainRow = loaded.chainRow,
      .chainColumn = loaded.chainColumn,
      .phraseNumber = loaded.phraseNumber,
      .phraseRow = phraseRow,
      .phraseColumn = loaded.phraseColumn,
      .phraseDigit = loaded.phraseDigit,
      .tablePage = Ui2TrackerPage::PhraseTable,
      .tableNumber = loaded.phraseTableNumber,
      .tableRow = loaded.phraseTableRow,
      .tableColumn = loaded.phraseTableColumn,
      .tableDigit = loaded.phraseTableDigit,
      .liveMode = loaded.liveMode,
  });

  Ui2TrackerCommand select = GridCommand(
      Ui2TrackerCommandType::SelectTrack, Ui2TrackerPage::PhraseTable, 0, 0);
  select.value = 1;
  port.ApplyGridCommand(select);
  const auto resolved = port.LoadGridSession();
  CHECK(resolved.track == 1U);
  CHECK(resolved.chainNumber == 2U);
  CHECK(resolved.phraseNumber == 5U);
  CHECK(resolved.phraseTableNumber == 7U);
}

TEST_CASE("UI2 model port resolves Phrase and Instrument navigation references") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  session.EditorState().currentPhrase_ = 4;
  constexpr int row = 7;
  const int index = 4 * STEPS_PER_PHRASE + row;

  song.phrase_.instr_[index] = 6U;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteLast,
                                    Ui2TrackerPage::Phrase, row, 1));
  song.phrase_.instr_[index] = 0xFFU;
  CHECK(port.PreparePageNavigation(Ui2TrackerPage::Phrase,
                                   Ui2TrackerPage::Instrument, 0, row));
  CHECK(session.EditorState().currentInstrumentID_ == 6);

  song.phrase_.cmd2_[index] = FourCC::InstrumentCommandTable;
  song.phrase_.param2_[index] = 0x23U;
  CHECK(port.PreparePageNavigation(Ui2TrackerPage::Phrase,
                                   Ui2TrackerPage::PhraseTable, 0, row));
  CHECK(port.LoadGridSession().phraseTableNumber == 3U);
  CHECK(session.EditorState().currentTable_ == 3);

  InstrumentBank *bank = session.ProjectModel().GetInstrumentBank();
  REQUIRE(bank->GetNextAndAssignID(IT_NONE, 6U) == 6U);
  bank->SetInstrumentTable(6U, 9);
  session.EditorState().currentInstrumentID_ = 6;
  CHECK(port.PreparePageNavigation(Ui2TrackerPage::Instrument,
                                   Ui2TrackerPage::InstrumentTable, 0, 0));
  CHECK(port.LoadGridSession().instrumentTableNumber == 9U);
  CHECK(session.EditorState().currentTable_ == 9);
}

TEST_CASE("UI2 model port adjusts mixed Chain selections by cell domain") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Song &song = session.ProjectModel().song_;
  song.chain_.data_[0] = 10U;
  song.chain_.transpose_[0] = 0U;
  Ui2TrackerCommand adjust =
      SelectionCommand(Ui2TrackerCommandType::AdjustSelection,
                       Ui2TrackerPage::Chain, 0, 0, 1, 0);
  adjust.direction = Ui2TrackerEditDirection::Up;
  adjust.value = 16;
  port.ApplyGridCommand(adjust);

  CHECK(song.chain_.data_[0] == 26U);
  CHECK(song.chain_.transpose_[0] == 12U);
  CHECK(port.ProjectMutationGeneration() == 1U);
}

TEST_CASE("UI2 Phrase notes follow project scale and Sample slice range") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Project &project = session.ProjectModel();
  Phrase &phrase = project.song_.phrase_;
  session.EditorState().currentPhrase_ = 1;
  constexpr int base = STEPS_PER_PHRASE;

  project.SetScale(21, 0U); // Ionian major, C root.
  phrase.note_[base + 2] = 60U;
  Ui2TrackerCommand right = GridCommand(Ui2TrackerCommandType::AdjustCell,
                                        Ui2TrackerPage::Phrase, 2, 0);
  right.direction = Ui2TrackerEditDirection::Right;
  port.ApplyGridCommand(right);
  CHECK(phrase.note_[base + 2] == 62U); // C# is skipped.

  phrase.note_[base + 3] = NOTE_OFF;
  right.row = 3U;
  port.ApplyGridCommand(right);
  CHECK(phrase.note_[base + 3] == NOTE_C3);

  phrase.note_[base + 4] = NO_NOTE;
  right.row = 4U;
  port.ApplyGridCommand(right);
  CHECK(phrase.note_[base + 4] == NO_NOTE);

  InstrumentBank *bank = project.GetInstrumentBank();
  bank->SetSampleSliceRange(5U, 36U, 39U);
  phrase.instr_[base] = 5U; // Effective for all following rows.
  phrase.note_[base + 5] = 38U;
  Ui2TrackerCommand octaveUp = right;
  octaveUp.row = 5U;
  octaveUp.direction = Ui2TrackerEditDirection::Up;
  port.ApplyGridCommand(octaveUp);
  CHECK(phrase.note_[base + 5] == 39U);
  octaveUp.direction = Ui2TrackerEditDirection::Down;
  port.ApplyGridCommand(octaveUp);
  CHECK(phrase.note_[base + 5] == 36U);
}

TEST_CASE("UI2 Phrase note adjustment retriggers an active audition") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Player *player = Player::GetInstance();
  player->Reset();
  session.EditorState().currentPhrase_ = 0;
  session.EditorState().songX_ = 4;
  session.EditorState().chainRow_ = 7;
  session.ProjectModel().song_.phrase_.note_[6] = 60U;

  Ui2TrackerCommand audition = GridCommand(
      Ui2TrackerCommandType::StartAudition, Ui2TrackerPage::Phrase, 6, 0);
  audition.track = 4U;
  port.ApplyGridCommand(audition);
  REQUIRE(player->startCalls == 1);

  Ui2TrackerCommand adjust = GridCommand(Ui2TrackerCommandType::AdjustCell,
                                         Ui2TrackerPage::Phrase, 6, 0);
  adjust.direction = Ui2TrackerEditDirection::Right;
  port.ApplyGridCommand(adjust);
  CHECK(player->stopCalls == 1);
  CHECK(player->startCalls == 2);
  CHECK(player->lastOrigin == PM_AUDITION);
  CHECK(player->lastFrom == 4U);
  CHECK(player->lastChainPosition == 7U);
}

TEST_CASE("UI2 Phrase audition never commandeers ordinary transport") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Player *player = Player::GetInstance();
  player->Reset();
  session.EditorState().playMode_ = PM_SONG;
  player->OnStartButton(PM_SONG, 2U, false, 0U);

  Ui2TrackerCommand start = GridCommand(
      Ui2TrackerCommandType::StartAudition, Ui2TrackerPage::Phrase, 4, 0);
  start.track = 2U;
  port.ApplyGridCommand(start);
  CHECK(player->startCalls == 1);
  CHECK(player->stopCalls == 0);
  CHECK(player->IsRunning());

  Ui2TrackerCommand stop = start;
  stop.type = Ui2TrackerCommandType::StopAudition;
  port.ApplyGridCommand(stop);
  CHECK(player->stopCalls == 0);
  CHECK(player->IsRunning());

  player->Reset();
  port.ApplyGridCommand(start);
  CHECK(player->startCalls == 1);
  CHECK(player->lastOrigin == PM_AUDITION);
  port.ApplyGridCommand(stop);
  CHECK(player->stopCalls == 1);
  CHECK_FALSE(player->IsRunning());
}

TEST_CASE("UI2 Phrase single Note cut clears its paired instrument") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Phrase &phrase = session.ProjectModel().song_.phrase_;
  session.EditorState().currentPhrase_ = 2;
  constexpr int index = 2 * STEPS_PER_PHRASE + 5;
  phrase.note_[index] = 64U;
  phrase.instr_[index] = 7U;

  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CutCell,
                                    Ui2TrackerPage::Phrase, 5, 0));
  CHECK(phrase.note_[index] == NO_NOTE);
  CHECK(phrase.instr_[index] == 0xFFU);

  phrase.instr_[index] = 8U;
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::CutCell,
                                    Ui2TrackerPage::Phrase, 5, 0));
  CHECK(phrase.note_[index] == NOTE_OFF);
  CHECK(phrase.instr_[index] == 8U);
}

TEST_CASE("UI2 empty cuts and identical pastes do not mark storage dirty") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  port.ApplyGridCommand(SelectionCommand(Ui2TrackerCommandType::CutSelection,
                                         Ui2TrackerPage::Phrase, 0, 0, 0, 0));
  CHECK(port.ProjectMutationGeneration() == 0U);

  Phrase &phrase = session.ProjectModel().song_.phrase_;
  phrase.note_[0] = 60U;
  port.ApplyGridCommand(SelectionCommand(Ui2TrackerCommandType::CopySelection,
                                         Ui2TrackerPage::Phrase, 0, 0, 0, 0));
  port.ApplyGridCommand(GridCommand(Ui2TrackerCommandType::PasteSelection,
                                    Ui2TrackerPage::Phrase, 0, 0));
  CHECK(port.ProjectMutationGeneration() == 0U);
}

TEST_CASE("UI2 context grids distinguish local PLAY from global SHIFT PLAY") {
  struct PlaybackCase {
    Ui2TrackerPage page;
    std::uint8_t row;
    std::uint8_t chainRow;
    PlayMode localOrigin;
    std::uint8_t localChainPosition;
  };
  constexpr PlaybackCase cases[] = {
      {Ui2TrackerPage::Chain, 7U, 11U, PM_CHAIN, 7U},
      {Ui2TrackerPage::Phrase, 2U, 11U, PM_PHRASE, 11U},
      {Ui2TrackerPage::PhraseTable, 15U, 4U, PM_PHRASE, 4U},
      {Ui2TrackerPage::InstrumentTable, 9U, 6U, PM_PHRASE, 6U},
  };

  for (const PlaybackCase &playbackCase : cases) {
    CAPTURE(static_cast<int>(playbackCase.page));
    TrackerApplicationSession session;
    Ui2TrackerSessionModelPort port(session);
    Player *player = Player::GetInstance();
    session.EditorState().songX_ = 6;
    session.EditorState().songY_ = 9;
    session.EditorState().songOffset_ = 32;
    session.EditorState().chainRow_ = playbackCase.chainRow;

    Ui2TrackerCommand command = GridCommand(
        Ui2TrackerCommandType::StartPlayback, playbackCase.page,
        playbackCase.row, 0U);
    command.track = 2U;

    player->Reset();
    port.ApplyGridCommand(command);
    CHECK(player->startCalls == 1);
    CHECK(player->lastOrigin == playbackCase.localOrigin);
    CHECK(player->lastFrom == 2U);
    CHECK_FALSE(player->lastStartFromPrevious);
    CHECK(player->lastChainPosition == playbackCase.localChainPosition);

    player->Reset();
    command.flag = true;
    port.ApplyGridCommand(command);
    CHECK(player->startCalls == 1);
    CHECK(player->lastOrigin == PM_SONG);
    CHECK(player->lastFrom == 6U);
    // Player::Start reads songY_ + songOffset_ only when this is false.
    CHECK_FALSE(player->lastStartFromPrevious);
    CHECK(player->lastChainPosition == 6U);
    CHECK(session.EditorState().songY_ == 9);
    CHECK(session.EditorState().songOffset_ == 32);
  }
}

TEST_CASE("UI2 Groove playback routes from controller through the model port") {
  struct PlaybackCase {
    bool shift;
    PlayMode origin;
    std::uint8_t chainPosition;
  };
  constexpr PlaybackCase cases[] = {
      {false, PM_PHRASE, 11U},
      {true, PM_SONG, 5U},
  };

  for (const PlaybackCase &playbackCase : cases) {
    CAPTURE(playbackCase.shift);
    TrackerApplicationSession session;
    Ui2TrackerSessionModelPort port(session);
    Player *player = Player::GetInstance();
    player->Reset();
    session.EditorState().songX_ = 5;
    session.EditorState().songY_ = 9;
    session.EditorState().songOffset_ = 32;
    session.EditorState().chainRow_ = 11;

    ui2::Ui2GrooveController controller;
    if (playbackCase.shift)
      controller.Handle(TrackerAction::Shift, true);
    const ui2::Ui2GrooveCommand grooveCommand =
        controller.Handle(TrackerAction::Play, true);
    const Ui2TrackerCommand trackerCommand =
        ui2::Ui2GrooveTrackerCommand(grooveCommand,
                                     session.EditorState().songX_);
    port.ApplyGridCommand(trackerCommand);

    CHECK(player->startCalls == 1);
    CHECK(player->lastOrigin == playbackCase.origin);
    CHECK(player->lastFrom == 5U);
    CHECK_FALSE(player->lastStartFromPrevious);
    CHECK(player->lastChainPosition == playbackCase.chainPosition);
    CHECK(session.EditorState().songY_ == 9);
    CHECK(session.EditorState().songOffset_ == 32);
  }
}

TEST_CASE("UI2 Groove performance chords route through the model port") {
  struct PerformanceCase {
    bool shift;
    Ui2TrackerCommandType type;
  };
  constexpr PerformanceCase cases[] = {
      {false, Ui2TrackerCommandType::ToggleSolo},
      {true, Ui2TrackerCommandType::UnmuteAll},
  };

  for (const PerformanceCase &performanceCase : cases) {
    CAPTURE(performanceCase.shift);
    TrackerApplicationSession session;
    Ui2TrackerSessionModelPort port(session);
    Player *player = Player::GetInstance();
    player->Reset();
    session.EditorState().songX_ = 5;
    for (int track = 0; track < SONG_CHANNEL_COUNT; ++track)
      player->SetChannelMute(track, (track & 1) == 0);

    ui2::Ui2GrooveController controller;
    if (performanceCase.shift)
      controller.Handle(TrackerAction::Shift, true);
    controller.Handle(TrackerAction::Option, true);
    const ui2::Ui2GrooveCommand grooveCommand =
        controller.Handle(TrackerAction::Play, true);
    const Ui2TrackerCommand trackerCommand =
        ui2::Ui2GrooveTrackerCommand(grooveCommand,
                                     session.EditorState().songX_);
    CHECK(trackerCommand.type == performanceCase.type);
    port.ApplyGridCommand(trackerCommand);

    for (int track = 0; track < SONG_CHANNEL_COUNT; ++track) {
      const bool expectedMuted =
          performanceCase.shift ? false : track != 5;
      CHECK(player->IsChannelMuted(track) == expectedMuted);
    }
    CHECK(player->startCalls == 0);
    CHECK(player->songStartCalls == 0);
    CHECK(player->stopCalls == 0);
    CHECK_FALSE(player->IsRunning());
  }
}

TEST_CASE("UI2 model port preserves playback and solo command semantics") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Player *player = Player::GetInstance();
  player->Reset();

  Ui2TrackerCommand song = GridCommand(Ui2TrackerCommandType::StartPlayback,
                                       Ui2TrackerPage::Song, 12, 3);
  song.selection.Begin(2, 12);
  song.selection.Follow(5, 14);
  port.ApplyGridCommand(song);
  CHECK(player->songStartCalls == 1);
  CHECK(player->lastFrom == 2U);
  CHECK(player->lastTo == 5U);
  CHECK_FALSE(player->lastForceImmediate);

  Ui2TrackerCommand chain = GridCommand(Ui2TrackerCommandType::StartPlayback,
                                        Ui2TrackerPage::Chain, 7, 0);
  chain.track = 4U;
  port.ApplyGridCommand(chain);
  CHECK(player->startCalls == 1);
  CHECK(player->lastOrigin == PM_CHAIN);
  CHECK(player->lastFrom == 4U);
  CHECK_FALSE(player->lastStartFromPrevious);
  CHECK(player->lastChainPosition == 7U);

  session.EditorState().chainRow_ = 11;
  Ui2TrackerCommand phrase = GridCommand(Ui2TrackerCommandType::StartPlayback,
                                         Ui2TrackerPage::Phrase, 2, 0);
  phrase.track = 6U;
  port.ApplyGridCommand(phrase);
  CHECK(player->startCalls == 2);
  CHECK(player->lastOrigin == PM_PHRASE);
  CHECK(player->lastFrom == 6U);
  CHECK(player->lastChainPosition == 11U);

  session.EditorState().chainRow_ = 4;
  Ui2TrackerCommand table = GridCommand(Ui2TrackerCommandType::StartPlayback,
                                        Ui2TrackerPage::PhraseTable, 15, 0);
  table.track = 1U;
  port.ApplyGridCommand(table);
  CHECK(player->startCalls == 3);
  CHECK(player->lastOrigin == PM_PHRASE);
  CHECK(player->lastFrom == 1U);
  CHECK_FALSE(player->lastStartFromPrevious);
  CHECK(player->lastChainPosition == 4U);

  Ui2TrackerCommand immediate = GridCommand(
      Ui2TrackerCommandType::StartImmediate, Ui2TrackerPage::Song, 12, 3);
  port.ApplyGridCommand(immediate);
  CHECK(player->songStartCalls == 2);
  CHECK(player->lastFrom == 3U);
  CHECK(player->lastTo == 3U);
  CHECK(player->lastForceImmediate);

  player->SetChannelMute(0, true);
  player->SetChannelMute(6, true);
  Ui2TrackerCommand solo = GridCommand(Ui2TrackerCommandType::ToggleSolo,
                                       Ui2TrackerPage::Song, 12, 3);
  solo.selection.Begin(2, 12);
  solo.selection.Follow(5, 14);
  port.ApplyGridCommand(solo);
  for (int track = 0; track < SONG_CHANNEL_COUNT; ++track)
    CHECK(player->IsChannelMuted(track) == (track < 2 || track > 5));
  port.ApplyGridCommand(solo);
  CHECK(player->IsChannelMuted(0));
  CHECK_FALSE(player->IsChannelMuted(1));
  CHECK_FALSE(player->IsChannelMuted(2));
  CHECK_FALSE(player->IsChannelMuted(3));
  CHECK_FALSE(player->IsChannelMuted(4));
  CHECK_FALSE(player->IsChannelMuted(5));
  CHECK(player->IsChannelMuted(6));
  CHECK_FALSE(player->IsChannelMuted(7));

  // The same model-port state is used by Mixer: it can turn off a solo that
  // was started in a grid and must restore the exact pre-solo mute mask.
  solo.selection.Clear();
  solo.track = 2U;
  port.ApplyGridCommand(solo);
  for (int track = 0; track < SONG_CHANNEL_COUNT; ++track)
    CHECK(player->IsChannelMuted(track) == (track != 2));
  Ui2TrackerCommand mixerSolo = solo;
  mixerSolo.sourcePage = Ui2TrackerPage::Mixer;
  mixerSolo.track = 5U;
  port.ApplyGridCommand(mixerSolo);
  CHECK(player->IsChannelMuted(0));
  CHECK_FALSE(player->IsChannelMuted(1));
  CHECK_FALSE(player->IsChannelMuted(2));
  CHECK_FALSE(player->IsChannelMuted(3));
  CHECK_FALSE(player->IsChannelMuted(4));
  CHECK_FALSE(player->IsChannelMuted(5));
  CHECK(player->IsChannelMuted(6));
  CHECK_FALSE(player->IsChannelMuted(7));
}

TEST_CASE("UI2 Shift Play requests a selected-track stop in Live mode") {
  TrackerApplicationSession session;
  Ui2TrackerSessionModelPort port(session);
  Player *player = Player::GetInstance();
  player->Reset();
  player->SetSequencerMode(SM_LIVE);

  Ui2TrackerCommand stop = GridCommand(Ui2TrackerCommandType::StartPlayback,
                                       Ui2TrackerPage::Song, 23, 4);
  stop.flag = true;
  port.ApplyGridCommand(stop);

  CHECK(player->songStartCalls == 1);
  CHECK(player->lastFrom == 4U);
  CHECK(player->lastTo == 4U);
  CHECK(player->lastRequestStop);
  CHECK_FALSE(player->lastForceImmediate);
}
