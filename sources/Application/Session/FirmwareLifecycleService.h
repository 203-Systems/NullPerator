/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "FirmwareLifecycleController.h"

#include <cstdint>
#include <type_traits>

// Narrow, UI-free platform boundary. Tests use an in-memory implementation;
// firmware and WASM use FirmwareLifecyclePlatformAdapter.
class IFirmwareLifecyclePlatform {
public:
  virtual ~IFirmwareLifecyclePlatform() = default;

  virtual bool InitializeMidi() = 0;
  virtual void CloseMidi() = 0;
  virtual FirmwareBatterySample ReadBattery() = 0;
  virtual void PowerDown() = 0;
  [[nodiscard]] virtual bool CanPrepareForcedUntitled() const = 0;
  virtual bool DeleteCurrentProjectMarker() = 0;
  virtual bool PurgeUntitledProject() = 0;
};

enum class FirmwareMidiLifecycleResult : std::uint8_t {
  Started,
  AlreadyStarted,
  Failed,
  Stopped,
  AlreadyStopped,
};

enum class FirmwareShutdownDispatch : std::uint8_t {
  Ignored,
  PlatformUnavailable,
  RequestDispatched,
  AlreadyDispatched,
};

struct FirmwareBootPreparation final {
  bool requested = false;
  bool platformAvailable = false;
  bool currentMarkerDeleted = false;
  bool untitledPurged = false;
};

// Owns lifecycle I/O cadence and idempotence, but no presentation state. The
// application explicitly initializes/closes MIDI so service teardown ordering
// remains deterministic on embedded targets.
class FirmwareLifecycleService final {
public:
  static constexpr std::uint32_t BatterySampleIntervalMs = 1'000U;

  explicit constexpr FirmwareLifecycleService(
      IFirmwareLifecyclePlatform *platform = nullptr) noexcept
      : platform_(platform) {}

  [[nodiscard]] FirmwareMidiLifecycleResult InitializeMidi() noexcept;
  [[nodiscard]] FirmwareMidiLifecycleResult CloseMidi() noexcept;
  [[nodiscard]] FirmwareBootPreparation
  PrepareProjectBoot(bool forceUntitled) noexcept;

  // Call every application tick. Power hold timing is checked every call;
  // battery I/O is independently capped at 1 Hz, including during playback.
  [[nodiscard]] FirmwareLifecycleCommand
  Tick(FirmwareLifecycleController &controller, std::uint32_t nowMs) noexcept;
  [[nodiscard]] FirmwareShutdownDispatch
  Execute(FirmwareLifecycleCommand command) noexcept;

  [[nodiscard]] constexpr bool MidiInitialized() const noexcept {
    return midiInitialized_;
  }
  [[nodiscard]] constexpr bool ShutdownDispatched() const noexcept {
    return shutdownDispatched_;
  }
  [[nodiscard]] constexpr bool BatterySampled() const noexcept {
    return batterySampleInitialized_;
  }
  [[nodiscard]] constexpr FirmwareBatterySample
  LastBatterySample() const noexcept {
    return lastBatterySample_;
  }

private:
  IFirmwareLifecyclePlatform *platform_ = nullptr;
  std::uint32_t lastBatterySampleMs_ = 0U;
  FirmwareBatterySample lastBatterySample_{};
  bool midiInitialized_ = false;
  bool batterySampleInitialized_ = false;
  bool shutdownDispatched_ = false;
};

static_assert(std::is_trivially_copyable_v<FirmwareBootPreparation>);
static_assert(std::is_trivially_copyable_v<FirmwareLifecycleService>);
static_assert(sizeof(FirmwareLifecycleService) <= 24U,
              "firmware lifecycle service must stay embedded-friendly");
