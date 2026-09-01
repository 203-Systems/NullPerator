#include "Application/UI2/Controllers/Ui2DeviceLifecycleController.h"
#include "Application/UI2/Ui2DeviceLifecycleService.h"

#include "doctest/doctest.h"

#include <string_view>

namespace {

ui2::Ui2DeviceLifecycleCommand
Tap(ui2::Ui2DeviceLifecycleController &controller, TrackerAction action) {
  const auto command = controller.Handle(action, true);
  controller.Handle(action, false);
  return command;
}

std::string_view Text(const auto &text) { return text.data(); }

class DeviceLifecycleTestSystem final : public System {
public:
  unsigned long GetClock() override { return 0; }
  void GetBatteryState(BatteryState &) override {}
  void SetDisplayBrightness(unsigned char) override {}
  void PostQuitMessage() override {}
  unsigned int GetMemoryUsage() override { return 0; }
  void PowerDown() override {}
  void SystemPutChar(int) override {}
  void SystemBootloader() override { ++bootloaderRequests; }
  void SystemReboot() override {}
  std::uint32_t GetRandomNumber() override { return 0; }
  std::uint32_t Micros() override { return 0; }
  std::uint32_t Millis() override { return 0; }

  int bootloaderRequests = 0;
};

} // namespace

TEST_CASE("UI2 Device firmware update is blocked during playback") {
  using namespace ui2;
  Ui2DeviceLifecycleController controller;
  controller.RequestUpdateFirmware(true);

  REQUIRE(controller.Active());
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(Text(dialog.title) == "Not while playing");
  CHECK(Text(dialog.label).empty());
  REQUIRE(dialog.actionCount == 1U);
  CHECK(dialog.actions[0] == UiDialogAction::Ok);
  CHECK(dialog.selectedAction == 0U);
  CHECK_FALSE(Tap(controller, TrackerAction::Enter).HasValue());
  CHECK_FALSE(controller.Active());
}

TEST_CASE("UI2 Device firmware confirmation defaults to NO") {
  using namespace ui2;
  Ui2DeviceLifecycleController controller;
  controller.RequestUpdateFirmware(false);

  REQUIRE(controller.Active());
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(Text(dialog.title) == "Reboot and lose changes?");
  CHECK(Text(dialog.label).empty());
  REQUIRE(dialog.actionCount == 2U);
  CHECK(dialog.actions[0] == UiDialogAction::Yes);
  CHECK(dialog.actions[1] == UiDialogAction::No);
  CHECK(dialog.selectedAction == 1U);

  CHECK_FALSE(Tap(controller, TrackerAction::Enter).HasValue());
  CHECK_FALSE(controller.Active());
}

TEST_CASE("UI2 Device dialogs wait for the opening ENTER release") {
  using namespace ui2;
  Ui2DeviceLifecycleController controller;

  SUBCASE("firmware confirmation") {
    controller.RequestUpdateFirmware(false, TrackerAction::Enter);
    REQUIRE(controller.Active());
    CHECK(controller.Snapshot().selectedAction == 1U);

    CHECK_FALSE(controller.Handle(TrackerAction::Enter, true).HasValue());
    CHECK_FALSE(controller.Handle(TrackerAction::Left, true).HasValue());
    CHECK_FALSE(controller.Handle(TrackerAction::Left, false).HasValue());
    CHECK(controller.Active());
    CHECK(controller.Snapshot().selectedAction == 1U);
    CHECK_FALSE(controller.Handle(TrackerAction::Enter, false).HasValue());

    CHECK_FALSE(Tap(controller, TrackerAction::Left).HasValue());
    CHECK(Tap(controller, TrackerAction::Enter).type ==
          Ui2DeviceLifecycleCommandType::EnterBootloader);
  }

  SUBCASE("playback warning") {
    controller.RequestUpdateFirmware(true, TrackerAction::Enter);
    REQUIRE(controller.Active());

    CHECK_FALSE(controller.Handle(TrackerAction::Enter, true).HasValue());
    CHECK(controller.Active());
    CHECK_FALSE(controller.Handle(TrackerAction::Enter, false).HasValue());

    CHECK_FALSE(Tap(controller, TrackerAction::Enter).HasValue());
    CHECK_FALSE(controller.Active());
  }
}

TEST_CASE("UI2 Device firmware service runs only an explicit YES command") {
  using namespace ui2;
  DeviceLifecycleTestSystem system;
  const Ui2DeviceLifecycleService service =
      Ui2DeviceLifecycleService::FromSystem(&system);
  Ui2DeviceLifecycleController controller;

  CHECK(service.Execute({}) == Ui2DeviceLifecycleDispatch::Ignored);
  CHECK(system.bootloaderRequests == 0);

  controller.RequestUpdateFirmware(false);
  Tap(controller, TrackerAction::Left);
  const Ui2DeviceLifecycleCommand command =
      Tap(controller, TrackerAction::Enter);
  REQUIRE(command.type == Ui2DeviceLifecycleCommandType::EnterBootloader);
  CHECK(system.bootloaderRequests == 0);
  CHECK(service.Execute(command) ==
        Ui2DeviceLifecycleDispatch::RequestDispatched);
  CHECK(system.bootloaderRequests == 1);
}

TEST_CASE("UI2 Device firmware service safely reports missing System") {
  using namespace ui2;
  const Ui2DeviceLifecycleService service =
      Ui2DeviceLifecycleService::FromSystem(nullptr);
  CHECK(service.Execute(
            {.type = Ui2DeviceLifecycleCommandType::EnterBootloader}) ==
        Ui2DeviceLifecycleDispatch::SystemUnavailable);
}
