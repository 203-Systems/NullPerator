#include "Application/UI2/Controllers/Ui2ChainController.h"
#include "Application/UI2/Controllers/Ui2PhraseController.h"
#include "Application/UI2/Controllers/Ui2SongController.h"
#include "Application/UI2/Controllers/Ui2TableController.h"

#include "doctest/doctest.h"

#include <type_traits>

namespace {

using ui2::Ui2ChainController;
using ui2::Ui2PhraseController;
using ui2::Ui2SongController;
using ui2::Ui2TableController;
using ui2::Ui2TrackerCommandType;
using ui2::Ui2TrackerEditDirection;
using ui2::Ui2TrackerPage;

template <typename Controller>
void Tap(Controller &controller, TrackerAction action) {
  controller.Handle(action, true);
  controller.Handle(action, false);
}

template <typename Controller> void BeginSelection(Controller &controller) {
  controller.Handle(TrackerAction::Shift, true);
  Tap(controller, TrackerAction::Option);
  controller.Handle(TrackerAction::Shift, false);
}

template <typename Controller>
void CheckSelectionClipboardLifecycle(Controller controller) {
  BeginSelection(controller);
  REQUIRE(controller.Selection().active);
  CHECK(controller.Handle(TrackerAction::Option, true).Empty());
  const auto copy = controller.Handle(TrackerAction::Option, false);
  REQUIRE(copy.count == 1U);
  CHECK(copy[0].type == Ui2TrackerCommandType::CopySelection);
  CHECK(copy[0].selection.active);
  CHECK_FALSE(controller.Selection().active);
  controller.Handle(TrackerAction::Shift, true);
  const auto paste = controller.Handle(TrackerAction::Edit, true);
  REQUIRE(paste.count == 1U);
  CHECK(paste[0].type == Ui2TrackerCommandType::PasteSelection);
  controller.Handle(TrackerAction::Edit, false);
  controller.Handle(TrackerAction::Shift, false);

  BeginSelection(controller);
  controller.Handle(TrackerAction::Edit, true);
  const auto cut = controller.Handle(TrackerAction::Option, true);
  REQUIRE(cut.count == 1U);
  CHECK(cut[0].type == Ui2TrackerCommandType::CutSelection);
  CHECK(cut[0].selection.active);
  CHECK_FALSE(controller.Selection().active);
  controller.Handle(TrackerAction::Option, false);
  CHECK(controller.Handle(TrackerAction::Edit, false).Empty());
}
template <typename Controller>
void CheckSelectionCutAcceptsEitherModifierOrder(Controller controller) {
  BeginSelection(controller);
  REQUIRE(controller.Selection().active);
  CHECK(controller.Handle(TrackerAction::Option, true).Empty());
  const auto cut = controller.Handle(TrackerAction::Edit, true);
  REQUIRE(cut.count == 1U);
  CHECK(cut[0].type == Ui2TrackerCommandType::CutSelection);
  CHECK(cut[0].selection.active);
  CHECK_FALSE(controller.Selection().active);
  controller.Handle(TrackerAction::Edit, false);
  CHECK(controller.Handle(TrackerAction::Option, false).Empty());
}

template <typename Controller> void CheckCommonPlaybackChords() {
  Controller plain;
  const auto play = plain.Handle(TrackerAction::Play, true);
  REQUIRE(play.count == 1U);
  CHECK(play[0].type == Ui2TrackerCommandType::StartPlayback);
  CHECK_FALSE(play[0].flag);

  Controller continued;
  continued.Handle(TrackerAction::Shift, true);
  const auto shiftPlay = continued.Handle(TrackerAction::Play, true);
  REQUIRE(shiftPlay.count == 1U);
  CHECK(shiftPlay[0].type == Ui2TrackerCommandType::StartPlayback);
  CHECK(shiftPlay[0].flag);

  Controller solo;
  solo.Handle(TrackerAction::Option, true);
  const auto optionPlay = solo.Handle(TrackerAction::Play, true);
  REQUIRE(optionPlay.count == 1U);
  CHECK(optionPlay[0].type == Ui2TrackerCommandType::ToggleSolo);

  Controller clearSolo;
  clearSolo.Handle(TrackerAction::Shift, true);
  clearSolo.Handle(TrackerAction::Option, true);
  const auto optionShiftPlay = clearSolo.Handle(TrackerAction::Play, true);
  REQUIRE(optionShiftPlay.count == 1U);
  CHECK(optionShiftPlay[0].type == Ui2TrackerCommandType::UnmuteAll);

  Controller editPlay;
  editPlay.Handle(TrackerAction::Edit, true);
  CHECK(editPlay.Handle(TrackerAction::Play, true).Empty());

  Controller optionEditPlay;
  optionEditPlay.Handle(TrackerAction::Option, true);
  optionEditPlay.Handle(TrackerAction::Edit, true);
  CHECK(optionEditPlay.Handle(TrackerAction::Play, true).Empty());

  Controller selectedEditPlay;
  BeginSelection(selectedEditPlay);
  selectedEditPlay.Handle(TrackerAction::Edit, true);
  CHECK(selectedEditPlay.Handle(TrackerAction::Play, true).Empty());
}

template <typename Controller>
void CheckSelectionSoloDoesNotCopy(Controller controller) {
  BeginSelection(controller);
  REQUIRE(controller.Selection().active);
  controller.Handle(TrackerAction::Option, true);
  const auto solo = controller.Handle(TrackerAction::Play, true);
  REQUIRE(solo.count == 1U);
  CHECK(solo[0].type == Ui2TrackerCommandType::ToggleSolo);
  controller.Handle(TrackerAction::Play, false);
  CHECK(controller.Handle(TrackerAction::Option, false).Empty());
  CHECK(controller.Selection().active);
}

} // namespace

TEST_CASE("UI2 Song owns a 16-row cursor and independent viewport") {
  Ui2SongController controller(0, 15, 0);
  CHECK(controller.VisibleRow() == 15U);
  CHECK(controller.RowOffset() == 0U);

  controller.Handle(TrackerAction::Down, true);
  CHECK(controller.VisibleRow() == 15U);
  CHECK(controller.RowOffset() == 1U);
  for (int repeat = 0; repeat < 200; ++repeat)
    controller.Handle(TrackerAction::Down, true);
  controller.Handle(TrackerAction::Down, false);
  CHECK(controller.VisibleRow() == 15U);
  CHECK(controller.RowOffset() == Ui2SongController::MaximumRowOffset);
  CHECK(controller.AbsoluteRow() == 127U);

  controller.Handle(TrackerAction::Option, true);
  const auto jump = controller.Handle(TrackerAction::Up, true);
  REQUIRE(jump.count == 1U);
  CHECK(jump[0].type == Ui2TrackerCommandType::JumpSection);
  CHECK(jump[0].value == -1);
  controller.Handle(TrackerAction::Up, false);
  controller.Handle(TrackerAction::Option, false);
  CHECK(controller.VisibleRow() == 15U);
  CHECK(controller.RowOffset() == Ui2SongController::MaximumRowOffset);
}

TEST_CASE("UI2 Song LIVE mode and selection are explicit controller states") {
  Ui2SongController controller(2, 4, 8, false);
  controller.Handle(TrackerAction::Option, true);
  const auto live = controller.Handle(TrackerAction::Right, true);
  REQUIRE(live.count == 1U);
  CHECK(live[0].type == Ui2TrackerCommandType::SetLiveMode);
  CHECK(live[0].flag);
  CHECK(controller.LiveMode());
  CHECK(controller.Track() == 2U);
  CHECK(controller.VisibleRow() == 4U);
  controller.Handle(TrackerAction::Right, false);
  controller.Handle(TrackerAction::Option, false);

  BeginSelection(controller);
  REQUIRE(controller.Selection().active);
  CHECK(controller.Selection().anchorColumn == 2U);
  CHECK(controller.Selection().anchorRow == 12U);
  Tap(controller, TrackerAction::Right);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.Selection().Right() == 3U);
  CHECK(controller.Selection().Bottom() == 13U);

  controller.Handle(TrackerAction::Edit, true);
  const auto edit = controller.Handle(TrackerAction::Up, true);
  REQUIRE(edit.count == 1U);
  CHECK(edit[0].type == Ui2TrackerCommandType::AdjustSelection);
  CHECK(edit[0].direction == Ui2TrackerEditDirection::Up);
  CHECK(edit[0].selection.Left() == 2U);
  CHECK(edit[0].selection.Right() == 3U);
  controller.Handle(TrackerAction::Up, false);
  const auto commit = controller.Handle(TrackerAction::Edit, false);
  REQUIRE(commit.count == 1U);
  CHECK(commit[0].type == Ui2TrackerCommandType::CommitValueEdits);

  Tap(controller, TrackerAction::Option);
  CHECK_FALSE(controller.Selection().active);
}

TEST_CASE("UI2 Song M8 selection expansion stays in 8 by 16") {
  Ui2SongController selectionController(4, 5, 20);
  BeginSelection(selectionController);
  selectionController.Handle(TrackerAction::Shift, true);
  selectionController.Handle(TrackerAction::Option, true);
  CHECK(selectionController.Selection().Left() == 0U);
  CHECK(selectionController.Selection().Right() == 7U);
  selectionController.Handle(TrackerAction::Option, false);
  selectionController.Handle(TrackerAction::Shift, false);
  selectionController.Handle(TrackerAction::Shift, true);
  selectionController.Handle(TrackerAction::Option, true);
  CHECK(selectionController.Selection().Top() == 20U);
  CHECK(selectionController.Selection().Bottom() == 35U);
}

TEST_CASE("UI2 grid selection copy cut and paste close the lifecycle") {
  CheckSelectionClipboardLifecycle(Ui2SongController(1, 2, 3));
  CheckSelectionClipboardLifecycle(Ui2ChainController(2, 1, 3, 0));
  CheckSelectionClipboardLifecycle(Ui2PhraseController(2, 1, 3, 0));
  CheckSelectionClipboardLifecycle(
      Ui2TableController(Ui2TrackerPage::PhraseTable, 2, 1, 3, 0));
}
TEST_CASE("UI2 grid selection cut is independent of modifier press order") {
  CheckSelectionCutAcceptsEitherModifierOrder(Ui2SongController(1, 2, 3));
  CheckSelectionCutAcceptsEitherModifierOrder(Ui2ChainController(2, 1, 3, 0));
  CheckSelectionCutAcceptsEitherModifierOrder(Ui2PhraseController(2, 1, 3, 0));
  CheckSelectionCutAcceptsEitherModifierOrder(
      Ui2TableController(Ui2TrackerPage::PhraseTable, 2, 1, 3, 0));
}

TEST_CASE("UI2 Song and Chain clone only after SHIFT OPTION release EDIT") {
  Ui2SongController song(2, 3, 4);
  song.Handle(TrackerAction::Shift, true);
  song.Handle(TrackerAction::Option, true);
  REQUIRE(song.Selection().active);
  CHECK(song.ClonePending());
  song.Handle(TrackerAction::Option, false);
  const auto songClone = song.Handle(TrackerAction::Edit, true);
  REQUIRE(songClone.count == 1U);
  CHECK(songClone[0].type == Ui2TrackerCommandType::CloneCell);
  CHECK_FALSE(song.ClonePending());
  CHECK_FALSE(song.Selection().active);
  song.Handle(TrackerAction::Edit, false);
  song.Handle(TrackerAction::Shift, false);

  Ui2ChainController chain(2, 1, 3, 0);
  chain.Handle(TrackerAction::Shift, true);
  chain.Handle(TrackerAction::Option, true);
  REQUIRE(chain.Selection().active);
  CHECK(chain.ClonePending());
  chain.Handle(TrackerAction::Option, false);
  const auto chainClone = chain.Handle(TrackerAction::Edit, true);
  REQUIRE(chainClone.count == 1U);
  CHECK(chainClone[0].type == Ui2TrackerCommandType::CloneCell);
  CHECK_FALSE(chain.ClonePending());
  CHECK_FALSE(chain.Selection().active);

  Ui2ChainController transpose(2, 1, 3, 1);
  transpose.Handle(TrackerAction::Shift, true);
  Tap(transpose, TrackerAction::Option);
  CHECK_FALSE(transpose.ClonePending());
  CHECK(transpose.Handle(TrackerAction::Edit, true).Empty());
}

TEST_CASE("UI2 Phrase clones an instrument after SHIFT OPTION release EDIT") {
  Ui2PhraseController instrument(2, 1, 3, 1);
  instrument.Handle(TrackerAction::Shift, true);
  instrument.Handle(TrackerAction::Option, true);
  REQUIRE(instrument.Selection().active);
  CHECK(instrument.ClonePending());
  instrument.Handle(TrackerAction::Option, false);
  const auto clone = instrument.Handle(TrackerAction::Edit, true);
  REQUIRE(clone.count == 1U);
  CHECK(clone[0].type == Ui2TrackerCommandType::CloneCell);
  CHECK_FALSE(instrument.ClonePending());
  CHECK_FALSE(instrument.Selection().active);

  Ui2PhraseController note(2, 1, 3, 0);
  note.Handle(TrackerAction::Shift, true);
  note.Handle(TrackerAction::Option, true);
  CHECK(note.Selection().active);
  CHECK_FALSE(note.ClonePending());
  note.Handle(TrackerAction::Option, false);
  CHECK(note.Handle(TrackerAction::Edit, true).Empty());
  CHECK(note.Selection().active);
}

TEST_CASE("UI2 Phrase and Table clone referenced data from parameter cells") {
  Ui2PhraseController phrase(2, 1, 3, 3);
  phrase.Handle(TrackerAction::Shift, true);
  phrase.Handle(TrackerAction::Option, true);
  phrase.Handle(TrackerAction::Option, false);
  const auto phraseClone = phrase.Handle(TrackerAction::Edit, true);
  REQUIRE(phraseClone.count == 1U);
  CHECK(phraseClone[0].type == Ui2TrackerCommandType::CloneCell);
  CHECK_FALSE(phrase.Selection().active);

  Ui2TableController table(Ui2TrackerPage::PhraseTable, 2, 1, 3, 1);
  table.Handle(TrackerAction::Shift, true);
  table.Handle(TrackerAction::Option, true);
  table.Handle(TrackerAction::Option, false);
  const auto tableClone = table.Handle(TrackerAction::Edit, true);
  REQUIRE(tableClone.count == 1U);
  CHECK(tableClone[0].type == Ui2TrackerCommandType::CloneCell);
  CHECK_FALSE(table.Selection().active);
}

TEST_CASE("UI2 clone gesture cancels when SHIFT is released") {
  Ui2SongController controller(2, 3, 4);
  controller.Handle(TrackerAction::Shift, true);
  Tap(controller, TrackerAction::Option);
  REQUIRE(controller.ClonePending());
  controller.Handle(TrackerAction::Shift, false);
  CHECK_FALSE(controller.ClonePending());
  CHECK(controller.Selection().active);
  CHECK(controller.Handle(TrackerAction::Edit, true).Empty());

  Ui2SongController interrupted(2, 3, 4);
  interrupted.Handle(TrackerAction::Shift, true);
  Tap(interrupted, TrackerAction::Option);
  REQUIRE(interrupted.ClonePending());
  const auto playback = interrupted.Handle(TrackerAction::Play, true);
  REQUIRE(playback.count == 1U);
  CHECK(playback[0].type == Ui2TrackerCommandType::StartPlayback);
  CHECK_FALSE(interrupted.ClonePending());

  Ui2PhraseController movedPhrase(2, 1, 3, 3);
  movedPhrase.Handle(TrackerAction::Shift, true);
  Tap(movedPhrase, TrackerAction::Option);
  REQUIRE(movedPhrase.ClonePending());
  Tap(movedPhrase, TrackerAction::Right);
  CHECK_FALSE(movedPhrase.ClonePending());
  CHECK(movedPhrase.Handle(TrackerAction::Edit, true).Empty());
  CHECK(movedPhrase.Selection().active);

  Ui2TableController releasedTable(Ui2TrackerPage::PhraseTable, 2, 1, 3, 1);
  releasedTable.Handle(TrackerAction::Shift, true);
  Tap(releasedTable, TrackerAction::Option);
  releasedTable.Handle(TrackerAction::Shift, false);
  CHECK(releasedTable.Handle(TrackerAction::Edit, true).Empty());
  CHECK(releasedTable.Selection().active);
}

TEST_CASE("UI2 Song follows M8 modifier order and transport chords") {
  Ui2SongController controller(3, 0, 0);

  controller.Handle(TrackerAction::Option, true);
  const auto mute = controller.Handle(TrackerAction::Shift, true);
  REQUIRE(mute.count == 1U);
  CHECK(mute[0].type == Ui2TrackerCommandType::ToggleMute);
  controller.Handle(TrackerAction::Shift, false);

  const auto solo = controller.Handle(TrackerAction::Play, true);
  REQUIRE(solo.count == 1U);
  CHECK(solo[0].type == Ui2TrackerCommandType::ToggleSolo);
  controller.Handle(TrackerAction::Play, false);

  controller.Handle(TrackerAction::Shift, true);
  const auto clear = controller.Handle(TrackerAction::Play, true);
  REQUIRE(clear.count == 1U);
  CHECK(clear[0].type == Ui2TrackerCommandType::UnmuteAll);
  controller.Handle(TrackerAction::Play, false);
  controller.Handle(TrackerAction::Shift, false);
  controller.Handle(TrackerAction::Option, false);

  controller.Handle(TrackerAction::Shift, true);
  controller.Handle(TrackerAction::Option, true);
  CHECK(controller.Selection().active);
}

TEST_CASE("UI2 grid selections retain Option then Shift mute") {
  Ui2SongController song(3, 4, 8);
  BeginSelection(song);
  song.Handle(TrackerAction::Option, true);
  const auto songMute = song.Handle(TrackerAction::Shift, true);
  REQUIRE(songMute.count == 1U);
  CHECK(songMute[0].type == Ui2TrackerCommandType::ToggleMute);
  CHECK(songMute[0].selection.active);
  CHECK(songMute[0].selection.Left() == 3U);
  CHECK(songMute[0].selection.Right() == 3U);

  Ui2ChainController chain(2, 4, 3, 0);
  BeginSelection(chain);
  chain.Handle(TrackerAction::Option, true);
  const auto chainMute = chain.Handle(TrackerAction::Shift, true);
  REQUIRE(chainMute.count == 1U);
  CHECK(chainMute[0].type == Ui2TrackerCommandType::ToggleMute);
  CHECK(chainMute[0].track == 4U);

  Ui2PhraseController phrase(2, 5, 3, 0);
  BeginSelection(phrase);
  phrase.Handle(TrackerAction::Option, true);
  const auto phraseMute = phrase.Handle(TrackerAction::Shift, true);
  REQUIRE(phraseMute.count == 1U);
  CHECK(phraseMute[0].type == Ui2TrackerCommandType::ToggleMute);
  CHECK(phraseMute[0].track == 5U);

  Ui2TableController table(Ui2TrackerPage::PhraseTable, 2, 6, 3, 0);
  BeginSelection(table);
  table.Handle(TrackerAction::Option, true);
  const auto tableMute = table.Handle(TrackerAction::Shift, true);
  REQUIRE(tableMute.count == 1U);
  CHECK(tableMute[0].type == Ui2TrackerCommandType::ToggleMute);
  CHECK(tableMute[0].track == 6U);
}

TEST_CASE("UI2 grid selection solo does not also copy on Option release") {
  CheckSelectionSoloDoesNotCopy(Ui2SongController(1, 2, 3));
  CheckSelectionSoloDoesNotCopy(Ui2ChainController(2, 1, 3, 0));
  CheckSelectionSoloDoesNotCopy(Ui2PhraseController(2, 1, 3, 0));
  CheckSelectionSoloDoesNotCopy(
      Ui2TableController(Ui2TrackerPage::PhraseTable, 2, 1, 3, 0));
}

TEST_CASE("UI2 Song EDIT PLAY launches immediately only in LIVE mode") {
  Ui2SongController liveController(3, 4, 8, true);

  const auto editDown = liveController.Handle(TrackerAction::Edit, true);
  CHECK(editDown.count == 0U);
  const auto immediate = liveController.Handle(TrackerAction::Play, true);
  REQUIRE(immediate.count == 1U);
  CHECK(immediate[0].type == Ui2TrackerCommandType::StartImmediate);
  CHECK(immediate[0].track == 3U);
  liveController.Handle(TrackerAction::Play, false);
  const auto editUp = liveController.Handle(TrackerAction::Edit, false);
  CHECK(editUp.count == 0U);

  Ui2SongController liveEditController(3, 4, 8, true);
  CHECK(liveEditController.Handle(TrackerAction::Edit, true).count == 0U);
  const auto liveEdit = liveEditController.Handle(TrackerAction::Edit, false);
  REQUIRE(liveEdit.count == 1U);
  CHECK(liveEdit[0].type == Ui2TrackerCommandType::PasteLast);

  Ui2SongController liveCutController(3, 4, 8, true);
  CHECK(liveCutController.Handle(TrackerAction::Edit, true).count == 0U);
  const auto cut = liveCutController.Handle(TrackerAction::Option, true);
  REQUIRE(cut.count == 1U);
  CHECK(cut[0].type == Ui2TrackerCommandType::CutCell);
  liveCutController.Handle(TrackerAction::Option, false);
  CHECK(liveCutController.Handle(TrackerAction::Edit, false).count == 0U);

  Ui2SongController songController(3, 4, 8, false);
  const auto songEdit = songController.Handle(TrackerAction::Edit, true);
  REQUIRE(songEdit.count == 1U);
  CHECK(songEdit[0].type == Ui2TrackerCommandType::PasteLast);
  const auto songPlay = songController.Handle(TrackerAction::Play, true);
  CHECK(songPlay.count == 0U);

  Ui2SongController selectedLiveController(3, 4, 8, true);
  BeginSelection(selectedLiveController);
  CHECK(selectedLiveController.Handle(TrackerAction::Edit, true).Empty());
  CHECK(selectedLiveController.Handle(TrackerAction::Play, true).Empty());
  CHECK(selectedLiveController.Handle(TrackerAction::Play, false).Empty());
  CHECK(selectedLiveController.Handle(TrackerAction::Edit, false).Empty());
  CHECK(selectedLiveController.Selection().active);
}

TEST_CASE("UI2 Song LIVE ignores Edit releases without an active press") {
  Ui2SongController controller(3, 4, 8, true);

  CHECK(controller.Handle(TrackerAction::Edit, false).Empty());

  CHECK(controller.Handle(TrackerAction::Edit, true).Empty());
  const auto edit = controller.Handle(TrackerAction::Edit, false);
  REQUIRE(edit.count == 1U);
  CHECK(edit[0].type == Ui2TrackerCommandType::PasteLast);

  CHECK(controller.Handle(TrackerAction::Edit, false).Empty());
}

TEST_CASE("UI2 Song LIVE Left Play cues the current row across all tracks") {
  Ui2SongController liveController(4, 3, 8, true);

  const auto left = liveController.Handle(TrackerAction::Left, true);
  CHECK(left.Empty());
  CHECK(liveController.Track() == 3U);

  const auto rowCue = liveController.Handle(TrackerAction::Play, true);
  REQUIRE(rowCue.count == 1U);
  CHECK(rowCue[0].type == Ui2TrackerCommandType::StartPlayback);
  CHECK(rowCue[0].track == 3U);
  CHECK(rowCue[0].row == 11U);
  REQUIRE(rowCue[0].selection.active);
  CHECK(rowCue[0].selection.Left() == 0U);
  CHECK(rowCue[0].selection.Right() == ui2::kUi2TrackerTrackCount - 1U);
  CHECK(rowCue[0].selection.Top() == 11U);
  CHECK(rowCue[0].selection.Bottom() == 11U);

  Ui2SongController selectedLiveController(4, 3, 8, true);
  BeginSelection(selectedLiveController);
  CHECK(selectedLiveController.Handle(TrackerAction::Left, true).Empty());
  const auto selectedRowCue =
      selectedLiveController.Handle(TrackerAction::Play, true);
  REQUIRE(selectedRowCue.count == 1U);
  CHECK(selectedRowCue[0].selection.Left() == 0U);
  CHECK(selectedRowCue[0].selection.Right() == ui2::kUi2TrackerTrackCount - 1U);
  CHECK(selectedRowCue[0].selection.Top() == 11U);
  CHECK(selectedRowCue[0].selection.Bottom() == 11U);

  Ui2SongController songController(4, 3, 8, false);
  CHECK(songController.Handle(TrackerAction::Left, true).Empty());
  const auto ordinaryPlay = songController.Handle(TrackerAction::Play, true);
  REQUIRE(ordinaryPlay.count == 1U);
  CHECK_FALSE(ordinaryPlay[0].selection.active);
}

TEST_CASE("UI2 grid playback chords match the M8 command matrix") {
  CheckCommonPlaybackChords<Ui2SongController>();
  CheckCommonPlaybackChords<Ui2ChainController>();
  CheckCommonPlaybackChords<Ui2PhraseController>();
  CheckCommonPlaybackChords<Ui2TableController>();

  Ui2SongController live(0, 0, 0, true);
  live.Handle(TrackerAction::Edit, true);
  const auto immediate = live.Handle(TrackerAction::Play, true);
  REQUIRE(immediate.count == 1U);
  CHECK(immediate[0].type == Ui2TrackerCommandType::StartImmediate);
}

TEST_CASE("UI2 Song mode selector wraps across exactly two options") {
  Ui2SongController controller(0, 0, 0, false);
  controller.Handle(TrackerAction::Option, true);
  const auto left = controller.Handle(TrackerAction::Left, true);
  REQUIRE(left.count == 1U);
  CHECK(left[0].type == Ui2TrackerCommandType::SetLiveMode);
  CHECK(left[0].flag);
  CHECK(controller.LiveMode());
  controller.Handle(TrackerAction::Left, false);

  const auto right = controller.Handle(TrackerAction::Right, true);
  REQUIRE(right.count == 1U);
  CHECK(right[0].type == Ui2TrackerCommandType::SetLiveMode);
  CHECK_FALSE(right[0].flag);
  CHECK_FALSE(controller.LiveMode());
}

TEST_CASE("UI2 Chain separates its grid cursor from Edit-held dual focus") {
  Ui2ChainController controller(0x2A, 3, 7, 0);
  controller.Handle(TrackerAction::Option, true);
  CHECK(controller.NumberFocus());
  CHECK(controller.TrackFocus());
  const auto track = controller.Handle(TrackerAction::Right, true);
  REQUIRE(track.count == 1U);
  CHECK(track[0].type == Ui2TrackerCommandType::SelectTrack);
  CHECK(track[0].value == 4);
  CHECK(controller.SelectedTrack() == 4U);
  CHECK(controller.Number() == 0x2AU);
  CHECK(controller.Row() == 7U);
  CHECK(controller.Column() == 0U);
  controller.Handle(TrackerAction::Right, false);
  controller.Handle(TrackerAction::Option, false);
  CHECK_FALSE(controller.NumberFocus());

  for (int repeat = 0; repeat < 30; ++repeat)
    controller.Handle(TrackerAction::Down, true);
  controller.Handle(TrackerAction::Down, false);
  CHECK(controller.Row() == 15U);
  CHECK(controller.Column() == 0U);
  Tap(controller, TrackerAction::Right);
  CHECK(controller.Column() == 1U);
}

TEST_CASE("UI2 Phrase accepts the full persisted phrase ID range") {
  CHECK(Ui2PhraseController(0x80U).Number() == 0x80U);
  CHECK(Ui2PhraseController(0xFEU).Number() == 0xFEU);
  CHECK(Ui2PhraseController(0xFFU).Number() == 0xFEU);
}

TEST_CASE(
    "UI2 Chain Enter-held edits emit typed deltas and commit on release") {
  Ui2ChainController phraseColumn(3, 0, 5, 0);
  phraseColumn.Handle(TrackerAction::Edit, true);
  const auto coarse = phraseColumn.Handle(TrackerAction::Up, true);
  REQUIRE(coarse.count == 1U);
  CHECK(coarse[0].type == Ui2TrackerCommandType::AdjustCell);
  CHECK(coarse[0].value == 16);
  CHECK(phraseColumn.Row() == 5U);
  phraseColumn.Handle(TrackerAction::Up, false);
  const auto commit = phraseColumn.Handle(TrackerAction::Edit, false);
  REQUIRE(commit.count == 1U);
  CHECK(commit[0].type == Ui2TrackerCommandType::CommitValueEdits);

  Ui2ChainController transposeColumn(3, 0, 5, 1);
  transposeColumn.Handle(TrackerAction::Edit, true);
  const auto octave = transposeColumn.Handle(TrackerAction::Down, true);
  REQUIRE(octave.count == 1U);
  CHECK(octave[0].value == -12);
  transposeColumn.Handle(TrackerAction::Down, false);
  transposeColumn.Handle(TrackerAction::Edit, false);
}

TEST_CASE("UI2 Phrase Edit dual focus does not move the cell cursor") {
  Ui2PhraseController controller(0x10, 2, 6, 3);
  controller.Handle(TrackerAction::Option, true);
  CHECK(controller.NumberFocus());
  CHECK(controller.TrackFocus());
  const auto track = controller.Handle(TrackerAction::Right, true);
  REQUIRE(track.count == 1U);
  CHECK(track[0].type == Ui2TrackerCommandType::SelectTrack);
  CHECK(controller.SelectedTrack() == 3U);
  CHECK(controller.Number() == 0x10U);
  CHECK(controller.Row() == 6U);
  CHECK(controller.Column() == 3U);
  controller.Handle(TrackerAction::Right, false);
  controller.Handle(TrackerAction::Option, false);
  CHECK_FALSE(controller.NumberFocus());
}

TEST_CASE("UI2 Phrase Enter focuses and moves the selected FX digit") {
  Ui2PhraseController controller(0, 0, 4, 3, 3);
  controller.Handle(TrackerAction::Edit, true);
  CHECK(controller.EnterDigitFocus());
  const auto moveDigit = controller.Handle(TrackerAction::Left, true);
  CHECK(moveDigit.Empty());
  CHECK(controller.ParameterDigit() == 2U);
  controller.Handle(TrackerAction::Left, false);

  const auto adjust = controller.Handle(TrackerAction::Up, true);
  REQUIRE(adjust.count == 1U);
  CHECK(adjust[0].type == Ui2TrackerCommandType::AdjustCell);
  CHECK(adjust[0].digit == 2U);
  CHECK(adjust[0].value == 16);
  CHECK(controller.Row() == 4U);
  CHECK(controller.Column() == 3U);
  controller.Handle(TrackerAction::Up, false);
  const auto commit = controller.Handle(TrackerAction::Edit, false);
  REQUIRE(commit.count == 1U);
  CHECK(commit[0].type == Ui2TrackerCommandType::CommitValueEdits);
  CHECK_FALSE(controller.EnterDigitFocus());
}

TEST_CASE("UI2 Phrase horizontal focus clamps at cell and FX digit edges") {
  Ui2PhraseController noteEdge(0, 0, 0, 0);
  Tap(noteEdge, TrackerAction::Left);
  CHECK(noteEdge.Column() == 0U);
  CHECK(noteEdge.Row() == 0U);

  Ui2PhraseController valueEdge(0, 0, 0, 5, 3);
  Tap(valueEdge, TrackerAction::Right);
  CHECK(valueEdge.Column() == 5U);
  valueEdge.Handle(TrackerAction::Edit, true);
  const auto digitEdge = valueEdge.Handle(TrackerAction::Right, true);
  CHECK(digitEdge.Empty());
  CHECK(valueEdge.ParameterDigit() == 3U);
  CHECK(valueEdge.Column() == 5U);
  valueEdge.Handle(TrackerAction::Right, false);
  CHECK(valueEdge.Handle(TrackerAction::Edit, false).Empty());
}

TEST_CASE("UI2 Phrase selection cannot move beyond fixed grid edges") {
  Ui2PhraseController controller(0, 0, 15, 5);
  BeginSelection(controller);
  REQUIRE(controller.Selection().active);
  const auto right = controller.Handle(TrackerAction::Right, true);
  CHECK(right.Empty());
  CHECK(controller.Column() == 5U);
  CHECK(controller.Selection().activeColumn == 5U);
  controller.Handle(TrackerAction::Right, false);
  const auto down = controller.Handle(TrackerAction::Down, true);
  CHECK(down.Empty());
  CHECK(controller.Row() == 15U);
  CHECK(controller.Selection().activeRow == 15U);
}

TEST_CASE("UI2 selections do not drift at any grid boundary") {
  Ui2SongController song(7, 15, Ui2SongController::MaximumRowOffset);
  BeginSelection(song);
  REQUIRE(song.Selection().SingleCell());
  CHECK(song.Handle(TrackerAction::Right, true).Empty());
  CHECK(song.Selection().activeColumn == 7U);
  song.Handle(TrackerAction::Right, false);
  CHECK(song.Handle(TrackerAction::Down, true).Empty());
  CHECK(song.Selection().activeRow == 127U);

  Ui2ChainController chain(0, 0, 15, 1);
  BeginSelection(chain);
  CHECK(chain.Handle(TrackerAction::Right, true).Empty());
  CHECK(chain.Selection().activeColumn == 1U);
  chain.Handle(TrackerAction::Right, false);
  CHECK(chain.Handle(TrackerAction::Down, true).Empty());
  CHECK(chain.Selection().activeRow == 15U);

  Ui2TableController table(Ui2TrackerPage::InstrumentTable, 0, 0, 15, 5);
  BeginSelection(table);
  CHECK(table.Handle(TrackerAction::Right, true).Empty());
  CHECK(table.Selection().activeColumn == 5U);
  table.Handle(TrackerAction::Right, false);
  CHECK(table.Handle(TrackerAction::Down, true).Empty());
  CHECK(table.Selection().activeRow == 15U);
}

TEST_CASE("UI2 Song selection stays within the fixed 8 by 16 clipboard") {
  Ui2SongController controller(0, 0, 0);
  BeginSelection(controller);
  for (int repeat = 0; repeat < 30; ++repeat)
    Tap(controller, TrackerAction::Down);
  CHECK(controller.AbsoluteRow() == 15U);
  CHECK(controller.Selection().Top() == 0U);
  CHECK(controller.Selection().Bottom() == 15U);

  CHECK(controller.Handle(TrackerAction::Option, true).Empty());
  const auto copy = controller.Handle(TrackerAction::Option, false);
  REQUIRE(copy.count == 1U);
  CHECK(copy[0].type == Ui2TrackerCommandType::CopySelection);
  CHECK(copy[0].selection.Bottom() - copy[0].selection.Top() + 1U == 16U);
}

TEST_CASE("UI2 Table switches track horizontally and table number vertically") {
  Ui2TableController controller(Ui2TrackerPage::PhraseTable, 31, 5, 8, 1);
  controller.Handle(TrackerAction::Option, true);
  CHECK(controller.NumberFocus());
  CHECK(controller.TrackFocus());
  const auto track = controller.Handle(TrackerAction::Right, true);
  REQUIRE(track.count == 1U);
  CHECK(track[0].type == Ui2TrackerCommandType::SelectTrack);
  CHECK(track[0].value == 6);
  CHECK(controller.Number() == 31U);
  CHECK(controller.SelectedTrack() == 6U);
  CHECK(controller.Row() == 8U);
  CHECK(controller.Column() == 1U);
  controller.Handle(TrackerAction::Right, false);

  const auto number = controller.Handle(TrackerAction::Up, true);
  REQUIRE(number.count == 1U);
  CHECK(number[0].type == Ui2TrackerCommandType::SelectNumber);
  CHECK(number[0].value == 15);
  CHECK(controller.Number() == 15U);
  controller.Handle(TrackerAction::Up, false);
  controller.Handle(TrackerAction::Option, false);

  for (int repeat = 0; repeat < 30; ++repeat)
    controller.Handle(TrackerAction::Down, true);
  controller.Handle(TrackerAction::Down, false);
  CHECK(controller.Row() == 15U);
}

TEST_CASE("UI2 Table Enter-held parameter focus owns a four-digit cursor") {
  Ui2TableController controller(Ui2TrackerPage::InstrumentTable, 1, 0, 2, 5, 3);
  controller.Handle(TrackerAction::Edit, true);
  CHECK(controller.EnterDigitFocus());
  Tap(controller, TrackerAction::Left);
  Tap(controller, TrackerAction::Left);
  CHECK(controller.ParameterDigit() == 1U);

  const auto adjust = controller.Handle(TrackerAction::Down, true);
  REQUIRE(adjust.count == 1U);
  CHECK(adjust[0].type == Ui2TrackerCommandType::AdjustCell);
  CHECK(adjust[0].digit == 1U);
  CHECK(adjust[0].value == -256);
  controller.Handle(TrackerAction::Down, false);
  const auto commit = controller.Handle(TrackerAction::Edit, false);
  REQUIRE(commit.count == 1U);
  CHECK(commit[0].type == Ui2TrackerCommandType::CommitValueEdits);
  CHECK_FALSE(controller.EnterDigitFocus());
}

TEST_CASE("UI2 Table command and value cells share fixed horizontal edges") {
  Ui2TableController left(Ui2TrackerPage::PhraseTable, 0, 0, 0, 0);
  Tap(left, TrackerAction::Left);
  CHECK(left.Column() == 0U);

  Ui2TableController right(Ui2TrackerPage::PhraseTable, 0, 0, 0, 5, 0);
  Tap(right, TrackerAction::Right);
  CHECK(right.Column() == 5U);
  right.Handle(TrackerAction::Edit, true);
  const auto digitEdge = right.Handle(TrackerAction::Left, true);
  CHECK(digitEdge.Empty());
  CHECK(right.ParameterDigit() == 0U);
  CHECK(right.Column() == 5U);
  right.Handle(TrackerAction::Left, false);
  CHECK(right.Handle(TrackerAction::Edit, false).Empty());
}

TEST_CASE("UI2 Chain Phrase and Table selections commit once on Edit release") {
  Ui2ChainController chain(0, 0, 2, 0);
  BeginSelection(chain);
  chain.Handle(TrackerAction::Edit, true);
  const auto chainAdjust = chain.Handle(TrackerAction::Up, true);
  REQUIRE(chainAdjust.count == 1U);
  CHECK(chainAdjust[0].type == Ui2TrackerCommandType::AdjustSelection);
  chain.Handle(TrackerAction::Up, false);
  const auto chainCommit = chain.Handle(TrackerAction::Edit, false);
  REQUIRE(chainCommit.count == 1U);
  CHECK(chainCommit[0].type == Ui2TrackerCommandType::CommitValueEdits);

  Ui2PhraseController phrase(0, 0, 2, 2);
  BeginSelection(phrase);
  phrase.Handle(TrackerAction::Edit, true);
  const auto phraseAdjust = phrase.Handle(TrackerAction::Up, true);
  REQUIRE(phraseAdjust.count == 1U);
  CHECK(phraseAdjust[0].type == Ui2TrackerCommandType::AdjustSelection);
  phrase.Handle(TrackerAction::Up, false);
  const auto phraseCommit = phrase.Handle(TrackerAction::Edit, false);
  REQUIRE(phraseCommit.count == 1U);
  CHECK(phraseCommit[0].type == Ui2TrackerCommandType::CommitValueEdits);
  CHECK(phrase.Handle(TrackerAction::Edit, false).Empty());

  Ui2TableController table(Ui2TrackerPage::InstrumentTable, 0, 0, 2, 2);
  BeginSelection(table);
  table.Handle(TrackerAction::Edit, true);
  const auto tableAdjust = table.Handle(TrackerAction::Down, true);
  REQUIRE(tableAdjust.count == 1U);
  CHECK(tableAdjust[0].type == Ui2TrackerCommandType::AdjustSelection);
  table.Handle(TrackerAction::Down, false);
  const auto tableCommit = table.Handle(TrackerAction::Edit, false);
  REQUIRE(tableCommit.count == 1U);
  CHECK(tableCommit[0].type == Ui2TrackerCommandType::CommitValueEdits);
  CHECK(table.Handle(TrackerAction::Edit, false).Empty());
}

TEST_CASE("UI2 Phrase release commits value edit and stops audition together") {
  Ui2PhraseController phrase(0, 0, 2, 0);
  const auto begin = phrase.Handle(TrackerAction::Edit, true);
  REQUIRE(begin.count == 2U);
  CHECK(begin[0].type == Ui2TrackerCommandType::PasteLast);
  CHECK(begin[1].type == Ui2TrackerCommandType::StartAudition);

  const auto adjust = phrase.Handle(TrackerAction::Up, true);
  REQUIRE(adjust.count == 1U);
  CHECK(adjust[0].type == Ui2TrackerCommandType::AdjustCell);
  phrase.Handle(TrackerAction::Up, false);

  const auto release = phrase.Handle(TrackerAction::Edit, false);
  REQUIRE(release.count == 2U);
  CHECK(release[0].type == Ui2TrackerCommandType::CommitValueEdits);
  CHECK(release[1].type == Ui2TrackerCommandType::StopAudition);
}

TEST_CASE("UI2 Phrase row edges request the adjacent Chain phrase") {
  Ui2PhraseController previous(0x12, 3, 0, 2);
  const auto up = previous.Handle(TrackerAction::Up, true);
  REQUIRE(up.count == 1U);
  CHECK(up[0].type == Ui2TrackerCommandType::WarpVertical);
  CHECK(up[0].direction == Ui2TrackerEditDirection::Up);
  CHECK(up[0].value == -1);
  CHECK(previous.Row() == 0U);

  Ui2PhraseController next(0x12, 3, 15, 2);
  const auto down = next.Handle(TrackerAction::Down, true);
  REQUIRE(down.count == 1U);
  CHECK(down[0].type == Ui2TrackerCommandType::WarpVertical);
  CHECK(down[0].direction == Ui2TrackerEditDirection::Down);
  CHECK(down[0].value == 1);
  CHECK(next.Row() == 15U);

  Ui2PhraseController middle(0x12, 3, 7, 2);
  CHECK(middle.Handle(TrackerAction::Down, true).Empty());
  CHECK(middle.Row() == 8U);

  Ui2PhraseController quickSelect(0x12, 3, 0, 2);
  quickSelect.Handle(TrackerAction::Option, true);
  const auto preserve = quickSelect.Handle(TrackerAction::Up, true);
  REQUIRE(preserve.count == 1U);
  CHECK(preserve[0].type == Ui2TrackerCommandType::WarpVertical);
  CHECK(preserve[0].flag);
}

TEST_CASE("UI2 grid controllers own fixed-capacity trivial state") {
  CHECK(std::is_trivially_copyable_v<Ui2SongController>);
  CHECK(std::is_trivially_copyable_v<Ui2ChainController>);
  CHECK(std::is_trivially_copyable_v<Ui2PhraseController>);
  CHECK(std::is_trivially_copyable_v<Ui2TableController>);
  CHECK(sizeof(Ui2SongController) <= 16U);
  CHECK(sizeof(Ui2ChainController) <= 16U);
  CHECK(sizeof(Ui2PhraseController) <= 16U);
  CHECK(sizeof(Ui2TableController) <= 16U);
}
