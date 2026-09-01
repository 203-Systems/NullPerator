#include "Application/UI2/Controllers/Ui2ProjectLifecycleController.h"

#include "doctest/doctest.h"

#include <string_view>

namespace {

ui2::Ui2ProjectLifecycleCommand
Tap(ui2::Ui2ProjectLifecycleController &controller, TrackerAction action) {
  const auto command = controller.Handle(action, true);
  controller.Handle(action, false);
  return command;
}

std::string_view Text(const auto &text) { return text.data(); }

void CheckHeldEnterIsReleasedBeforeDialogInput(
    ui2::Ui2ProjectLifecycleController &controller,
    ui2::Ui2ProjectLifecycleCommandType expected) {
  REQUIRE(controller.Active());
  REQUIRE(controller.Snapshot().actionCount == 2U);
  CHECK(controller.Snapshot().selectedAction == 1U);

  // This is the platform repeat pulse from the ENTER press that opened the
  // dialog. It must neither accept the conservative default nor close it.
  CHECK_FALSE(controller.Handle(TrackerAction::Enter, true).HasValue());
  CHECK(controller.Active());
  CHECK(controller.Snapshot().selectedAction == 1U);
  CHECK_FALSE(controller.Handle(TrackerAction::Left, true).HasValue());
  CHECK_FALSE(controller.Handle(TrackerAction::Left, false).HasValue());
  CHECK(controller.Snapshot().selectedAction == 1U);
  CHECK_FALSE(controller.Handle(TrackerAction::Enter, false).HasValue());

  CHECK_FALSE(Tap(controller, TrackerAction::Left).HasValue());
  CHECK(Tap(controller, TrackerAction::Enter).type == expected);
  CHECK_FALSE(controller.Active());
}

} // namespace

TEST_CASE("UI2 project lifecycle confirms dirty New with safe default") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;

  CHECK(controller.RequestNew(false, false).type ==
        Ui2ProjectLifecycleCommandType::NewProject);
  CHECK_FALSE(controller.Active());

  CHECK_FALSE(controller.RequestNew(true, false).HasValue());
  REQUIRE(controller.Active());
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(Text(dialog.title) == "Create a new project and");
  CHECK(Text(dialog.label) == "   lose all changes?");
  REQUIRE(dialog.actionCount == 2U);
  CHECK(dialog.actions[0] == UiDialogAction::Yes);
  CHECK(dialog.actions[1] == UiDialogAction::No);
  CHECK(dialog.selectedAction == 1U);

  CHECK_FALSE(Tap(controller, TrackerAction::Enter).HasValue());
  CHECK_FALSE(controller.Active());

  controller.RequestNew(true, false);
  Tap(controller, TrackerAction::Left);
  CHECK(Tap(controller, TrackerAction::Enter).type ==
        Ui2ProjectLifecycleCommandType::NewProject);
}

TEST_CASE("UI2 project lifecycle blocks destructive work while playing") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;
  CHECK_FALSE(controller.RequestLoad("DEMO", true, true).HasValue());
  REQUIRE(controller.Active());
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(Text(dialog.title) == "Not while running!");
  REQUIRE(dialog.actionCount == 1U);
  CHECK(dialog.actions[0] == UiDialogAction::Ok);
  CHECK_FALSE(Tap(controller, TrackerAction::Enter).HasValue());
}

TEST_CASE("UI2 project lifecycle preserves confirmed Load payload") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;
  CHECK_FALSE(controller.RequestLoad("RESTORE-ME", true, false).HasValue());
  CHECK(controller.Snapshot().selectedAction == 1U);
  Tap(controller, TrackerAction::Left);
  const Ui2ProjectLifecycleCommand load = Tap(controller, TrackerAction::Enter);
  CHECK(load.type == Ui2ProjectLifecycleCommandType::LoadProject);
  CHECK(std::string_view(load.project.data()) == "RESTORE-ME");
}

TEST_CASE("UI2 project lifecycle protects current project from Delete") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;
  CHECK_FALSE(controller.RequestDelete("CURRENT", "CURRENT", false).HasValue());
  REQUIRE(controller.Active());
  CHECK(Text(controller.Snapshot().title) == "Cannot delete the active");
  CHECK(Text(controller.Snapshot().label) == "project.");
  CHECK_FALSE(controller.Snapshot().labelUserText);
  Tap(controller, TrackerAction::Enter);

  controller.RequestDelete("OLD", "CURRENT", false);
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(Text(dialog.title) == "Delete selected project?");
  CHECK(Text(dialog.label) == "OLD");
  CHECK(dialog.labelUserText);
  CHECK(dialog.selectedAction == 1U);
  Tap(controller, TrackerAction::Left);
  const Ui2ProjectLifecycleCommand remove =
      Tap(controller, TrackerAction::Enter);
  CHECK(remove.type == Ui2ProjectLifecycleCommandType::DeleteProject);
  CHECK(std::string_view(remove.project.data()) == "OLD");
}

TEST_CASE("UI2 project lifecycle requires explicit overwrite selection") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;
  controller.RequestOverwrite("EXISTING");
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  REQUIRE(dialog.actionCount == 2U);
  CHECK(dialog.actions[0] == UiDialogAction::Ok);
  CHECK(dialog.actions[1] == UiDialogAction::Cancel);
  CHECK(dialog.selectedAction == 1U);
  Tap(controller, TrackerAction::Left);
  const Ui2ProjectLifecycleCommand overwrite =
      Tap(controller, TrackerAction::Enter);
  CHECK(overwrite.type == Ui2ProjectLifecycleCommandType::OverwriteProject);
  CHECK(std::string_view(overwrite.project.data()) == "EXISTING");
}

TEST_CASE("UI2 theme overwrite reuses conservative lifecycle confirmation") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;

  controller.RequestThemeOverwrite("DEFAULT");
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(Text(dialog.title) == "Theme already exists");
  CHECK(Text(dialog.label) == "Overwrite?");
  REQUIRE(dialog.actionCount == 2U);
  CHECK(dialog.actions[0] == UiDialogAction::Yes);
  CHECK(dialog.actions[1] == UiDialogAction::No);
  CHECK(dialog.selectedAction == 1U);

  CHECK_FALSE(Tap(controller, TrackerAction::Enter).HasValue());
  controller.RequestThemeOverwrite("DEFAULT");
  Tap(controller, TrackerAction::Left);
  const Ui2ProjectLifecycleCommand overwrite =
      Tap(controller, TrackerAction::Enter);
  CHECK(overwrite.type == Ui2ProjectLifecycleCommandType::OverwriteTheme);
  CHECK(std::string_view(overwrite.project.data()) == "DEFAULT");
}

TEST_CASE("UI2 project dialogs ignore their held ENTER opener until release") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;

  SUBCASE("dirty New") {
    CHECK_FALSE(
        controller.RequestNew(true, false, TrackerAction::Enter).HasValue());
    CheckHeldEnterIsReleasedBeforeDialogInput(
        controller, Ui2ProjectLifecycleCommandType::NewProject);
  }
  SUBCASE("dirty Load") {
    CHECK_FALSE(controller
                    .RequestLoad("RESTORE-ME", true, false,
                                 TrackerAction::Enter)
                    .HasValue());
    CheckHeldEnterIsReleasedBeforeDialogInput(
        controller, Ui2ProjectLifecycleCommandType::LoadProject);
  }
  SUBCASE("Delete") {
    CHECK_FALSE(controller
                    .RequestDelete("OLD", "CURRENT", false,
                                   TrackerAction::Enter)
                    .HasValue());
    CheckHeldEnterIsReleasedBeforeDialogInput(
        controller, Ui2ProjectLifecycleCommandType::DeleteProject);
  }
  SUBCASE("sample purge") {
    controller.RequestPurgeUnusedSamples(false, TrackerAction::Enter);
    CheckHeldEnterIsReleasedBeforeDialogInput(
        controller, Ui2ProjectLifecycleCommandType::PurgeUnusedSamples);
  }
  SUBCASE("instrument purge") {
    controller.RequestPurgeUnusedInstruments(false, TrackerAction::Enter);
    CheckHeldEnterIsReleasedBeforeDialogInput(
        controller, Ui2ProjectLifecycleCommandType::PurgeUnusedInstruments);
  }
  SUBCASE("Theme overwrite") {
    controller.RequestThemeOverwrite("DEFAULT", TrackerAction::Enter);
    CheckHeldEnterIsReleasedBeforeDialogInput(
        controller, Ui2ProjectLifecycleCommandType::OverwriteTheme);
  }
  SUBCASE("Project overwrite") {
    controller.RequestOverwrite("EXISTING", TrackerAction::Enter);
    CheckHeldEnterIsReleasedBeforeDialogInput(
        controller, Ui2ProjectLifecycleCommandType::OverwriteProject);
  }
  SUBCASE("running guard") {
    controller.RequestPurgeUnusedSamples(true, TrackerAction::Enter);
    REQUIRE(controller.Active());
    REQUIRE(controller.Snapshot().actionCount == 1U);
    CHECK_FALSE(controller.Handle(TrackerAction::Enter, true).HasValue());
    CHECK(controller.Active());
    CHECK_FALSE(controller.Handle(TrackerAction::Enter, false).HasValue());
    CHECK_FALSE(Tap(controller, TrackerAction::Enter).HasValue());
    CHECK_FALSE(controller.Active());
  }
}

TEST_CASE("UI2 project lifecycle confirms sample purge with safe default") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;

  controller.RequestPurgeUnusedSamples(false);
  REQUIRE(controller.Active());
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(Text(dialog.title) == "Remove unused samples?");
  CHECK(Text(dialog.label).empty());
  REQUIRE(dialog.actionCount == 2U);
  CHECK(dialog.actions[0] == UiDialogAction::Yes);
  CHECK(dialog.actions[1] == UiDialogAction::No);
  CHECK(dialog.selectedAction == 1U);

  CHECK_FALSE(Tap(controller, TrackerAction::Enter).HasValue());
  CHECK_FALSE(controller.Active());

  controller.RequestPurgeUnusedSamples(false);
  Tap(controller, TrackerAction::Left);
  CHECK(Tap(controller, TrackerAction::Enter).type ==
        Ui2ProjectLifecycleCommandType::PurgeUnusedSamples);
}

TEST_CASE("UI2 project lifecycle confirms instrument purge and blocks playback") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;

  controller.RequestPurgeUnusedInstruments(false);
  REQUIRE(controller.Active());
  CHECK(Text(controller.Snapshot().title) == "Remove unused instruments?");
  CHECK(controller.Snapshot().selectedAction == 1U);
  Tap(controller, TrackerAction::Left);
  CHECK(Tap(controller, TrackerAction::Enter).type ==
        Ui2ProjectLifecycleCommandType::PurgeUnusedInstruments);

  controller.RequestPurgeUnusedInstruments(true);
  REQUIRE(controller.Active());
  const Ui2DialogSnapshot blocked = controller.Snapshot();
  CHECK(Text(blocked.title) == "Not while running!");
  REQUIRE(blocked.actionCount == 1U);
  CHECK(blocked.actions[0] == UiDialogAction::Ok);
  CHECK_FALSE(Tap(controller, TrackerAction::Enter).HasValue());
}

TEST_CASE("UI2 project lifecycle exposes established persistence failures") {
  using namespace ui2;
  Ui2ProjectLifecycleController controller;
  controller.ReportFailure(Ui2ProjectLifecycleFailure::OpenProjectBrowser);
  CHECK(Text(controller.Snapshot().title) == "Project browser unavailable");
  Tap(controller, TrackerAction::Enter);
  controller.ReportFailure(Ui2ProjectLifecycleFailure::SaveProject);
  CHECK(Text(controller.Snapshot().title) == "Error saving Project");
  Tap(controller, TrackerAction::Enter);
  controller.ReportFailure(Ui2ProjectLifecycleFailure::LoadProject, "BROKEN");
  CHECK(Text(controller.Snapshot().title) == "Invalid Project:");
  CHECK(Text(controller.Snapshot().label) == "BROKEN");
  CHECK(controller.Snapshot().labelUserText);
  Tap(controller, TrackerAction::Enter);
  controller.ReportFailure(Ui2ProjectLifecycleFailure::DeleteProject);
  CHECK(Text(controller.Snapshot().title) == "Project could not be deleted");
  Tap(controller, TrackerAction::Enter);
  controller.ReportFailure(
      Ui2ProjectLifecycleFailure::RefreshBrowserAfterDelete);
  CHECK(Text(controller.Snapshot().title) == "Project deleted;");
  CHECK(Text(controller.Snapshot().label) == "browser refresh failed");
  CHECK_FALSE(controller.Snapshot().labelUserText);
  Tap(controller, TrackerAction::Enter);
  controller.ReportFailure(Ui2ProjectLifecycleFailure::SaveTheme);
  CHECK(Text(controller.Snapshot().title) == "Failed to export theme");
}
