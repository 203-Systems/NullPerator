/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Application/UI2/Controllers/Ui2TrackerControllerHub.h"

#include <doctest/doctest.h>

namespace {

class FakeGridPort final : public ui2::IUi2TrackerModelPort {
public:
  ui2::Ui2TrackerGridSessionState LoadGridSession() const override {
    return loaded;
  }

  void StoreGridNavigation(
      const ui2::Ui2TrackerGridNavigationState &state) override {
    navigation = state;
    ++storeCount;
  }

  void ApplyGridCommand(const ui2::Ui2TrackerCommand &command) override {
    if (appliedCount < applied.size())
      applied[appliedCount++] = command;
    if (command.type == ui2::Ui2TrackerCommandType::SwitchPage)
      loaded.activePage = command.targetPage;
    if (command.type == ui2::Ui2TrackerCommandType::SelectTrack)
      loaded.track = static_cast<std::uint8_t>(command.value);
  }

  ui2::Ui2TrackerGridSessionState loaded{};
  ui2::Ui2TrackerGridNavigationState navigation{};
  std::array<ui2::Ui2TrackerCommand, 8> applied{};
  std::size_t appliedCount = 0;
  std::size_t storeCount = 0;
};

} // namespace

TEST_CASE("UI2 tracker hub restores independent grid page state") {
  ui2::Ui2TrackerGridSessionState state{};
  state.activePage = ui2::Ui2TrackerPage::Phrase;
  state.track = 5;
  state.songVisibleRow = 9;
  state.songRowOffset = 32;
  state.chainNumber = 0x34;
  state.chainRow = 7;
  state.chainColumn = 1;
  state.phraseNumber = 0x21;
  state.phraseRow = 11;
  state.phraseColumn = 5;
  state.phraseDigit = 2;
  state.phraseTableNumber = 4;
  state.instrumentTableNumber = 9;
  state.liveMode = true;

  ui2::Ui2TrackerControllerHub hub(state);
  CHECK(hub.ActivePage() == ui2::Ui2TrackerPage::Phrase);
  CHECK(hub.ActiveApplicationPage() == ui2::UiApplicationPage::Phrase);
  CHECK(hub.Song().Track() == 5);
  CHECK(hub.Song().VisibleRow() == 9);
  CHECK(hub.Song().RowOffset() == 32);
  CHECK(hub.Song().LiveMode());
  CHECK(hub.Chain().Number() == 0x34);
  CHECK(hub.Chain().Row() == 7);
  CHECK(hub.Phrase().Number() == 0x21);
  CHECK(hub.Phrase().Column() == 5);
  CHECK(hub.PhraseTable().Number() == 4);
  CHECK(hub.InstrumentTable().Number() == 9);
}

TEST_CASE("UI2 tracker hub keeps key release with the press owner") {
  ui2::Ui2TrackerControllerHub hub;
  (void)hub.Handle(TrackerAction::Edit, true);
  hub.Activate(ui2::Ui2TrackerPage::Chain);
  (void)hub.Handle(TrackerAction::Edit, false);

  CHECK((hub.Song().HeldMask() & TrackerActionBit(TrackerAction::Edit)) == 0);
  CHECK((hub.Chain().HeldMask() & TrackerActionBit(TrackerAction::Edit)) == 0);
}

TEST_CASE("UI2 tracker executor applies typed command then stores navigation") {
  FakeGridPort port;
  port.loaded.activePage = ui2::Ui2TrackerPage::Song;
  ui2::Ui2TrackerCommandExecutor executor(port);

  executor.Handle(TrackerAction::Edit, true);
  const auto batch = executor.Handle(TrackerAction::Up, true);

  REQUIRE(batch.count == 1);
  CHECK(batch[0].type == ui2::Ui2TrackerCommandType::AdjustCell);
  CHECK(port.appliedCount == 2);
  CHECK(port.applied[1].type == ui2::Ui2TrackerCommandType::AdjustCell);
  CHECK(port.storeCount == 2);
  CHECK(port.navigation.activePage == ui2::Ui2TrackerPage::Song);
}

TEST_CASE("UI2 tracker executor activates navigation target before storing") {
  FakeGridPort port;
  ui2::Ui2TrackerCommandExecutor executor(port);

  executor.Handle(TrackerAction::Shift, true);
  const auto batch = executor.Handle(TrackerAction::Right, true);

  REQUIRE(batch.count == 1);
  CHECK(batch[0].type == ui2::Ui2TrackerCommandType::SwitchPage);
  CHECK(executor.ActivePage() == ui2::Ui2TrackerPage::Chain);
  CHECK(port.navigation.activePage == ui2::Ui2TrackerPage::Chain);
  CHECK(port.applied[0].targetPage == ui2::Ui2TrackerPage::Chain);
}

TEST_CASE("UI2 tracker executor preserves held navigation across page switches") {
  FakeGridPort port;
  ui2::Ui2TrackerCommandExecutor executor(port);

  executor.Handle(TrackerAction::Shift, true);
  executor.Hub().SetNavigationHeld(true);
  executor.Handle(TrackerAction::Right, true);
  executor.Handle(TrackerAction::Right, false);

  CHECK(executor.ActivePage() == ui2::Ui2TrackerPage::Chain);
  CHECK((executor.ActiveState().heldMask &
         TrackerActionBit(TrackerAction::Shift)) != 0U);

  executor.Handle(TrackerAction::Right, true);
  executor.Handle(TrackerAction::Right, false);
  CHECK(executor.ActivePage() == ui2::Ui2TrackerPage::Phrase);
  CHECK((executor.ActiveState().heldMask &
         TrackerActionBit(TrackerAction::Shift)) != 0U);

  executor.Handle(TrackerAction::Left, true);
  executor.Handle(TrackerAction::Left, false);
  CHECK(executor.ActivePage() == ui2::Ui2TrackerPage::Chain);
  CHECK((executor.ActiveState().heldMask &
         TrackerActionBit(TrackerAction::Shift)) != 0U);

  executor.Handle(TrackerAction::Shift, false);
  executor.Hub().SetNavigationHeld(false);
  CHECK((executor.ActiveState().heldMask &
         TrackerActionBit(TrackerAction::Shift)) == 0U);
}

TEST_CASE("UI2 tracker hub restores held navigation after direct activation") {
  ui2::Ui2TrackerControllerHub hub;
  hub.SetNavigationHeld(true);

  REQUIRE(hub.Activate(ui2::Ui2TrackerPage::Chain));
  CHECK((hub.ActiveState().heldMask &
         TrackerActionBit(TrackerAction::Shift)) != 0U);

  const auto playback = hub.Handle(TrackerAction::Play, true);
  REQUIRE(playback.count == 1U);
  CHECK(playback[0].type == ui2::Ui2TrackerCommandType::StartPlayback);
  CHECK(playback[0].flag);
}

TEST_CASE("UI2 tracker executor preserves OPTION across quick-select reloads") {
  FakeGridPort port;
  port.loaded.activePage = ui2::Ui2TrackerPage::Chain;
  port.loaded.track = 2U;
  port.loaded.chainNumber = 7U;
  port.loaded.chainRow = 4U;
  ui2::Ui2TrackerCommandExecutor executor(port);

  executor.Handle(TrackerAction::Option, true);
  const auto track = executor.Handle(TrackerAction::Right, true);
  REQUIRE(track.count == 1U);
  CHECK(track[0].type == ui2::Ui2TrackerCommandType::SelectTrack);
  CHECK(executor.ActiveState().track == 3U);
  CHECK((executor.ActiveState().heldMask &
         TrackerActionBit(TrackerAction::Option)) != 0U);

  executor.Handle(TrackerAction::Right, false);
  const auto vertical = executor.Handle(TrackerAction::Down, true);
  REQUIRE(vertical.count == 1U);
  CHECK(vertical[0].type == ui2::Ui2TrackerCommandType::WarpVertical);
  CHECK((executor.ActiveState().heldMask &
         TrackerActionBit(TrackerAction::Option)) != 0U);
  executor.Handle(TrackerAction::Down, false);
  executor.Handle(TrackerAction::Option, false);
  CHECK((executor.ActiveState().heldMask &
         TrackerActionBit(TrackerAction::Option)) == 0U);
}
