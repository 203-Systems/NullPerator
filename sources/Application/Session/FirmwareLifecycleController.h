/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>
#include <type_traits>

// UI-agnostic battery sample used by the firmware lifecycle. A missing sample
// deliberately freezes (rather than clears) an active critical-battery timer,
// matching the legacy AppWindow behavior when BatteryState::error is set.
struct FirmwareBatterySample final {
  std::uint8_t percentage = 0U;
  bool available = false;
  bool charging = false;
};

enum class FirmwareShutdownReason : std::uint8_t {
  None,
  PowerButtonHold,
  CriticalBattery,
};

struct FirmwareLifecycleCommand final {
  FirmwareShutdownReason shutdownReason = FirmwareShutdownReason::None;

  [[nodiscard]] constexpr bool HasValue() const noexcept {
    return shutdownReason != FirmwareShutdownReason::None;
  }
};

// Typed state only: the presentation layer may later choose how (or whether)
// to visualize these values. No overlay, text, or glyph policy belongs here.
struct FirmwareLifecycleState final {
  std::uint16_t powerHoldRemainingMs = 0U;
  std::uint16_t criticalBatteryRemainingMs = 0U;
  bool powerButtonHeld = false;
  bool criticalBattery = false;
  bool shutdownLatched = false;
};

// Fixed-size, allocation-free policy shared by native UI2 hosts and firmware.
// Time arithmetic intentionally uses unsigned subtraction so clock wraparound
// has the same well-defined behavior as the rest of the tracker runtime.
class FirmwareLifecycleController final {
public:
  static constexpr std::uint32_t PowerHoldMs = 3'000U;
  static constexpr std::uint8_t CriticalBatteryPercentage = 2U;
  static constexpr std::uint32_t CriticalBatteryShutdownMs = 15'000U;

  constexpr FirmwareLifecycleController() noexcept = default;

  void Reset(std::uint32_t nowMs = 0U) noexcept;
  void SetPowerButton(bool pressed, std::uint32_t nowMs) noexcept;

  // Tick is cheap enough for every application frame. Battery sampling is a
  // separate 1 Hz service responsibility so this controller never performs
  // ADC/board I/O itself.
  [[nodiscard]] FirmwareLifecycleCommand Tick(std::uint32_t nowMs) noexcept;
  [[nodiscard]] FirmwareLifecycleCommand
  ObserveBattery(FirmwareBatterySample sample, std::uint32_t nowMs) noexcept;

  [[nodiscard]] FirmwareLifecycleState
  State(std::uint32_t nowMs) const noexcept;

private:
  [[nodiscard]] FirmwareLifecycleCommand
  Latch(FirmwareShutdownReason reason) noexcept;
  void ResetCriticalBattery() noexcept;

  std::uint32_t powerPressedMs_ = 0U;
  std::uint32_t batteryLastSampleMs_ = 0U;
  std::uint32_t criticalBatteryElapsedMs_ = 0U;
  bool powerButtonHeld_ = false;
  bool powerShutdownLatched_ = false;
  bool criticalBattery_ = false;
  bool previousBatterySampleCritical_ = false;
  bool batteryShutdownLatched_ = false;
};

static_assert(std::is_trivially_copyable_v<FirmwareBatterySample>);
static_assert(std::is_trivially_copyable_v<FirmwareLifecycleCommand>);
static_assert(std::is_trivially_copyable_v<FirmwareLifecycleState>);
static_assert(std::is_trivially_copyable_v<FirmwareLifecycleController>);
static_assert(sizeof(FirmwareLifecycleController) <= 24U,
              "firmware lifecycle state must stay embedded-friendly");
