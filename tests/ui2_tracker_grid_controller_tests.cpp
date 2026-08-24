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

  controller.Handle(TrackerAction::Edit, true);
  controller.Handle(TrackerAction::Up, true);
  controller.Handle(TrackerAction::Up, false);
  controller.Handle(TrackerAction::Edit, false);
  CHECK(controller.VisibleRow() == 15U);
  CHECK(controller.RowOffset() == 96U);
}

TEST_CASE("UI2 Song LIVE mode and selection are explicit controller states") {
  Ui2SongController controller(2, 4, 8, false);
  controller.Handle(TrackerAction::Edit, true);
  const auto live = controller.Handle(TrackerAction::Right, true);
  REQUIRE(live.count == 1U);
  CHECK(live[0].type == Ui2TrackerCommandType::SetLiveMode);
  CHECK(live[0].flag);
  CHECK(controller.LiveMode());
  CHECK(controller.Track() == 2U);
  CHECK(controller.VisibleRow() == 4U);
  controller.Handle(TrackerAction::Right, false);
  controller.Handle(TrackerAction::Edit, false);

  Tap(controller, TrackerAction::Select);
  REQUIRE(controller.Selection().active);
  CHECK(controller.Selection().anchorColumn == 2U);
  CHECK(controller.Selection().anchorRow == 12U);
  Tap(controller, TrackerAction::Right);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.Selection().Right() == 3U);
  CHECK(controller.Selection().Bottom() == 13U);

  controller.Handle(TrackerAction::Enter, true);
  const auto edit = controller.Handle(TrackerAction::Up, true);
  REQUIRE(edit.count == 1U);
  CHECK(edit[0].type == Ui2TrackerCommandType::AdjustSelection);
  CHECK(edit[0].direction == Ui2TrackerEditDirection::Up);
  CHECK(edit[0].selection.Left() == 2U);
  CHECK(edit[0].selection.Right() == 3U);
  controller.Handle(TrackerAction::Up, false);
  const auto commit = controller.Handle(TrackerAction::Enter, false);
  REQUIRE(commit.count == 1U);
  CHECK(commit[0].type == Ui2TrackerCommandType::CommitValueEdits);

  Tap(controller, TrackerAction::Select);
  CHECK_FALSE(controller.Selection().active);
}

TEST_CASE("UI2 Song clone-pending and selection expansion stay in 8 by 16") {
  Ui2SongController cloneController(4, 5, 20);
  cloneController.Handle(TrackerAction::Edit, true);
  cloneController.Handle(TrackerAction::Alt, true);
  CHECK(cloneController.ClonePending());
  const auto clone = cloneController.Handle(TrackerAction::Enter, true);
  REQUIRE(clone.count == 1U);
  CHECK(clone[0].type == Ui2TrackerCommandType::CloneCell);
  CHECK_FALSE(cloneController.ClonePending());

  Ui2SongController selectionController(4, 5, 20);
  Tap(selectionController, TrackerAction::Select);
  selectionController.Handle(TrackerAction::Alt, true);
  selectionController.Handle(TrackerAction::Edit, true);
  CHECK(selectionController.Selection().Left() == 0U);
  CHECK(selectionController.Selection().Right() == 7U);
  selectionController.Handle(TrackerAction::Edit, false);
  selectionController.Handle(TrackerAction::Edit, true);
  CHECK(selectionController.Selection().Top() == 20U);
  CHECK(selectionController.Selection().Bottom() == 35U);
}

TEST_CASE("UI2 Chain separates its grid cursor from Edit-held dual focus") {
  Ui2ChainController controller(0x2A, 3, 7, 0);
  controller.Handle(TrackerAction::Edit, true);
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
  controller.Handle(TrackerAction::Edit, false);
  CHECK_FALSE(controller.NumberFocus());

  for (int repeat = 0; repeat < 30; ++repeat)
    controller.Handle(TrackerAction::Down, true);
  controller.Handle(TrackerAction::Down, false);
  CHECK(controller.Row() == 15U);
  CHECK(controller.Column() == 0U);
  Tap(controller, TrackerAction::Right);
  CHECK(controller.Column() == 1U);
}

TEST_CASE("UI2 Chain Enter-held edits emit typed deltas and commit on release") {
  Ui2ChainController phraseColumn(3, 0, 5, 0);
  phraseColumn.Handle(TrackerAction::Enter, true);
  const auto coarse = phraseColumn.Handle(TrackerAction::Up, true);
  REQUIRE(coarse.count == 1U);
  CHECK(coarse[0].type == Ui2TrackerCommandType::AdjustCell);
  CHECK(coarse[0].value == 16);
  CHECK(phraseColumn.Row() == 5U);
  phraseColumn.Handle(TrackerAction::Up, false);
  const auto commit = phraseColumn.Handle(TrackerAction::Enter, false);
  REQUIRE(commit.count == 1U);
  CHECK(commit[0].type == Ui2TrackerCommandType::CommitValueEdits);

  Ui2ChainController transposeColumn(3, 0, 5, 1);
  transposeColumn.Handle(TrackerAction::Enter, true);
  const auto octave = transposeColumn.Handle(TrackerAction::Down, true);
  REQUIRE(octave.count == 1U);
  CHECK(octave[0].value == -12);
  transposeColumn.Handle(TrackerAction::Down, false);
  transposeColumn.Handle(TrackerAction::Enter, false);
}

TEST_CASE("UI2 Phrase Edit dual focus does not move the cell cursor") {
  Ui2PhraseController controller(0x10, 2, 6, 3);
  controller.Handle(TrackerAction::Edit, true);
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
  controller.Handle(TrackerAction::Edit, false);
  CHECK_FALSE(controller.NumberFocus());
}

TEST_CASE("UI2 Phrase Enter focuses and moves the selected FX digit") {
  Ui2PhraseController controller(0, 0, 4, 3, 3);
  controller.Handle(TrackerAction::Enter, true);
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
  controller.Handle(TrackerAction::Enter, false);
  CHECK_FALSE(controller.EnterDigitFocus());
}

TEST_CASE("UI2 Table wraps its number while keeping a 16-row cursor") {
  Ui2TableController controller(Ui2TrackerPage::PhraseTable, 31, 5, 8, 1);
  controller.Handle(TrackerAction::Edit, true);
  CHECK(controller.NumberFocus());
  CHECK(controller.TrackFocus());
  const auto number = controller.Handle(TrackerAction::Right, true);
  REQUIRE(number.count == 1U);
  CHECK(number[0].type == Ui2TrackerCommandType::SelectNumber);
  CHECK(number[0].value == 0);
  CHECK(controller.Number() == 0U);
  CHECK(controller.SelectedTrack() == 5U);
  CHECK(controller.Row() == 8U);
  CHECK(controller.Column() == 1U);
  controller.Handle(TrackerAction::Right, false);
  controller.Handle(TrackerAction::Edit, false);

  for (int repeat = 0; repeat < 30; ++repeat)
    controller.Handle(TrackerAction::Down, true);
  controller.Handle(TrackerAction::Down, false);
  CHECK(controller.Row() == 15U);
}

TEST_CASE("UI2 Table Enter-held parameter focus owns a four-digit cursor") {
  Ui2TableController controller(Ui2TrackerPage::InstrumentTable, 1, 0, 2, 5,
                                3);
  controller.Handle(TrackerAction::Enter, true);
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
  controller.Handle(TrackerAction::Enter, false);
  CHECK_FALSE(controller.EnterDigitFocus());
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
