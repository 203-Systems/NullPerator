#include "Application/Session/FirmwareLifecycleController.h"
#include "Application/Session/FirmwareLifecycleService.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <limits>

namespace {

class FirmwareLifecycleTestPlatform final
    : public IFirmwareLifecyclePlatform {
public:
  bool InitializeMidi() override {
    ++midiInitCalls;
    return midiInitSucceeds;
  }
  void CloseMidi() override { ++midiCloseCalls; }
  FirmwareBatterySample ReadBattery() override {
    ++batteryReads;
    return battery;
  }
  void PowerDown() override { ++powerDownCalls; }
  [[nodiscard]] bool CanPrepareForcedUntitled() const override {
    return bootStorageAvailable;
  }
  bool DeleteCurrentProjectMarker() override {
    ++deleteCurrentCalls;
    return deleteCurrentResult;
  }
  bool PurgeUntitledProject() override {
    ++purgeUntitledCalls;
    return purgeUntitledResult;
  }

  FirmwareBatterySample battery{};
  bool midiInitSucceeds = true;
  bool bootStorageAvailable = true;
  bool deleteCurrentResult = true;
  bool purgeUntitledResult = true;
  int midiInitCalls = 0;
  int midiCloseCalls = 0;
  int batteryReads = 0;
  int powerDownCalls = 0;
  int deleteCurrentCalls = 0;
  int purgeUntitledCalls = 0;
};

} // namespace

TEST_CASE("firmware power hold emits one shutdown after three seconds") {
  FirmwareLifecycleController controller;
  controller.SetPowerButton(true, 10U);

  CHECK(controller.State(10U).powerHoldRemainingMs == 3'000U);
  CHECK_FALSE(controller.Tick(3'009U).HasValue());
  const FirmwareLifecycleCommand command = controller.Tick(3'010U);
  CHECK(command.shutdownReason == FirmwareShutdownReason::PowerButtonHold);
  CHECK(controller.State(3'010U).shutdownLatched);
  CHECK(controller.State(3'010U).powerHoldRemainingMs == 0U);
  CHECK_FALSE(controller.Tick(30'000U).HasValue());

  controller.SetPowerButton(false, 30'001U);
  CHECK_FALSE(controller.State(30'001U).powerButtonHeld);
  controller.SetPowerButton(true, 30'010U);
  CHECK_FALSE(controller.Tick(33'009U).HasValue());
  CHECK(controller.Tick(33'010U).shutdownReason ==
        FirmwareShutdownReason::PowerButtonHold);
}

TEST_CASE("firmware power hold remains correct across millisecond wrap") {
  FirmwareLifecycleController controller;
  constexpr std::uint32_t pressed =
      std::numeric_limits<std::uint32_t>::max() - 1'000U;
  controller.SetPowerButton(true, pressed);
  CHECK_FALSE(controller.Tick(pressed + 2'999U).HasValue());
  CHECK(controller.Tick(pressed + 3'000U).shutdownReason ==
        FirmwareShutdownReason::PowerButtonHold);
}

TEST_CASE("critical battery uses strict two-percent and fifteen-second policy") {
  FirmwareLifecycleController controller;

  CHECK_FALSE(controller
                  .ObserveBattery(
                      {.percentage = 2U, .available = true, .charging = false},
                      0U)
                  .HasValue());
  CHECK_FALSE(controller.State(0U).criticalBattery);

  CHECK_FALSE(controller
                  .ObserveBattery(
                      {.percentage = 1U, .available = true, .charging = false},
                      1'000U)
                  .HasValue());
  CHECK(controller.State(1'000U).criticalBattery);
  CHECK(controller.State(1'000U).criticalBatteryRemainingMs == 15'000U);

  for (std::uint32_t second = 2U; second <= 15U; ++second) {
    CHECK_FALSE(controller
                    .ObserveBattery({.percentage = 1U,
                                     .available = true,
                                     .charging = false},
                                    second * 1'000U)
                    .HasValue());
  }
  CHECK(controller
            .ObserveBattery(
                {.percentage = 1U, .available = true, .charging = false},
                16'000U)
            .shutdownReason == FirmwareShutdownReason::CriticalBattery);
  CHECK_FALSE(controller
                  .ObserveBattery(
                      {.percentage = 1U, .available = true, .charging = false},
                      17'000U)
                  .HasValue());
}

TEST_CASE("charging resets and missing battery samples freeze countdown") {
  FirmwareLifecycleController controller;
  FirmwareBatterySample critical{
      .percentage = 0U, .available = true, .charging = false};

  CHECK_FALSE(controller.ObserveBattery(critical, 0U).HasValue());
  for (std::uint32_t second = 1U; second <= 5U; ++second)
    CHECK_FALSE(
        controller.ObserveBattery(critical, second * 1'000U).HasValue());
  CHECK(controller.State(5'000U).criticalBatteryRemainingMs == 10'000U);

  for (std::uint32_t second = 6U; second <= 10U; ++second)
    CHECK_FALSE(controller.ObserveBattery({}, second * 1'000U).HasValue());
  CHECK(controller.State(10'000U).criticalBattery);
  CHECK(controller.State(10'000U).criticalBatteryRemainingMs == 10'000U);
  CHECK_FALSE(controller.ObserveBattery(critical, 11'000U).HasValue());
  CHECK(controller.State(11'000U).criticalBatteryRemainingMs == 9'000U);

  CHECK_FALSE(controller
                  .ObserveBattery(
                      {.percentage = 0U, .available = true, .charging = true},
                      12'000U)
                  .HasValue());
  CHECK_FALSE(controller.State(12'000U).criticalBattery);
  CHECK(controller.State(12'000U).criticalBatteryRemainingMs == 0U);
}

TEST_CASE("firmware lifecycle service samples battery at one hertz") {
  FirmwareLifecycleTestPlatform platform;
  platform.battery = {
      .percentage = 1U, .available = true, .charging = false};
  FirmwareLifecycleService service(&platform);
  FirmwareLifecycleController controller;

  CHECK_FALSE(service.Tick(controller, 10U).HasValue());
  CHECK(platform.batteryReads == 1);
  CHECK(service.BatterySampled());
  CHECK(service.LastBatterySample().available);
  CHECK(service.LastBatterySample().percentage == 1U);
  CHECK_FALSE(service.Tick(controller, 999U).HasValue());
  CHECK(platform.batteryReads == 1);
  CHECK_FALSE(service.Tick(controller, 1'009U).HasValue());
  CHECK(platform.batteryReads == 1);
  CHECK_FALSE(service.Tick(controller, 1'010U).HasValue());
  CHECK(platform.batteryReads == 2);

  FirmwareLifecycleCommand shutdown;
  for (std::uint32_t second = 2U; second <= 15U; ++second)
    shutdown = service.Tick(controller, 10U + second * 1'000U);
  REQUIRE(shutdown.shutdownReason == FirmwareShutdownReason::CriticalBattery);
  CHECK(service.Execute(shutdown) ==
        FirmwareShutdownDispatch::RequestDispatched);
  CHECK(platform.powerDownCalls == 1);
  CHECK(service.Execute(shutdown) ==
        FirmwareShutdownDispatch::AlreadyDispatched);
  CHECK(platform.powerDownCalls == 1);
  CHECK_FALSE(service.Tick(controller, 50'000U).HasValue());
  CHECK(platform.batteryReads == 16);
}

TEST_CASE("firmware lifecycle service owns MIDI exactly once") {
  FirmwareLifecycleTestPlatform platform;
  FirmwareLifecycleService service(&platform);

  CHECK(service.InitializeMidi() == FirmwareMidiLifecycleResult::Started);
  CHECK(service.InitializeMidi() ==
        FirmwareMidiLifecycleResult::AlreadyStarted);
  CHECK(platform.midiInitCalls == 1);
  CHECK(service.CloseMidi() == FirmwareMidiLifecycleResult::Stopped);
  CHECK(service.CloseMidi() == FirmwareMidiLifecycleResult::AlreadyStopped);
  CHECK(platform.midiCloseCalls == 1);

  platform.midiInitSucceeds = false;
  CHECK(service.InitializeMidi() == FirmwareMidiLifecycleResult::Failed);
  CHECK_FALSE(service.MidiInitialized());
  platform.midiInitSucceeds = true;
  CHECK(service.InitializeMidi() == FirmwareMidiLifecycleResult::Started);
  CHECK(platform.midiInitCalls == 3);
}

TEST_CASE("forced untitled boot clears both legacy recovery locations") {
  FirmwareLifecycleTestPlatform platform;
  FirmwareLifecycleService service(&platform);

  FirmwareBootPreparation preparation = service.PrepareProjectBoot(false);
  CHECK_FALSE(preparation.requested);
  CHECK(platform.deleteCurrentCalls == 0);
  CHECK(platform.purgeUntitledCalls == 0);

  preparation = service.PrepareProjectBoot(true);
  CHECK(preparation.requested);
  CHECK(preparation.platformAvailable);
  CHECK(preparation.currentMarkerDeleted);
  CHECK(preparation.untitledPurged);
  CHECK(platform.deleteCurrentCalls == 1);
  CHECK(platform.purgeUntitledCalls == 1);

  platform.bootStorageAvailable = false;
  preparation = service.PrepareProjectBoot(true);
  CHECK(preparation.requested);
  CHECK_FALSE(preparation.platformAvailable);
  CHECK(platform.deleteCurrentCalls == 1);
  CHECK(platform.purgeUntitledCalls == 1);
}
