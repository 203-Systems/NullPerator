/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "FirmwareLifecycleController.h"

#include <algorithm>
#include <limits>

namespace {

std::uint16_t Remaining(std::uint32_t duration,
                        std::uint32_t elapsed) noexcept {
  if (elapsed >= duration)
    return 0U;
  return static_cast<std::uint16_t>(std::min<std::uint32_t>(
      duration - elapsed, std::numeric_limits<std::uint16_t>::max()));
}

} // namespace

void FirmwareLifecycleController::Reset(std::uint32_t nowMs) noexcept {
  powerPressedMs_ = nowMs;
  batteryLastSampleMs_ = nowMs;
  criticalBatteryElapsedMs_ = 0U;
  powerButtonHeld_ = false;
  powerShutdownLatched_ = false;
  criticalBattery_ = false;
  previousBatterySampleCritical_ = false;
  batteryShutdownLatched_ = false;
}

void FirmwareLifecycleController::SetPowerButton(bool pressed,
                                                 std::uint32_t nowMs) noexcept {
  if (pressed == powerButtonHeld_)
    return;
  powerButtonHeld_ = pressed;
  if (pressed) {
    powerPressedMs_ = nowMs;
    powerShutdownLatched_ = false;
  } else {
    powerShutdownLatched_ = false;
  }
}

FirmwareLifecycleCommand
FirmwareLifecycleController::Tick(std::uint32_t nowMs) noexcept {
  if (powerButtonHeld_ && !powerShutdownLatched_ &&
      static_cast<std::uint32_t>(nowMs - powerPressedMs_) >= PowerHoldMs) {
    powerShutdownLatched_ = true;
    return Latch(FirmwareShutdownReason::PowerButtonHold);
  }
  return {};
}

FirmwareLifecycleCommand FirmwareLifecycleController::ObserveBattery(
    FirmwareBatterySample sample, std::uint32_t nowMs) noexcept {
  if (!sample.available) {
    // Legacy behavior skips an errored sample without dismissing the warning.
    // Advance only the sample clock: with the service's regular 1 Hz calls,
    // every unavailable interval is frozen while the first restored critical
    // sample resumes the countdown by one normal interval.
    batteryLastSampleMs_ = nowMs;
    return {};
  }

  const bool critical = !sample.charging &&
                        sample.percentage < CriticalBatteryPercentage;
  if (!critical) {
    ResetCriticalBattery();
    batteryLastSampleMs_ = nowMs;
    return {};
  }

  criticalBattery_ = true;
  if (previousBatterySampleCritical_) {
    const std::uint32_t interval = nowMs - batteryLastSampleMs_;
    criticalBatteryElapsedMs_ =
        std::min<std::uint32_t>(CriticalBatteryShutdownMs,
                                criticalBatteryElapsedMs_ + interval);
  }
  previousBatterySampleCritical_ = true;
  batteryLastSampleMs_ = nowMs;

  if (!batteryShutdownLatched_ &&
      criticalBatteryElapsedMs_ >= CriticalBatteryShutdownMs) {
    batteryShutdownLatched_ = true;
    return Latch(FirmwareShutdownReason::CriticalBattery);
  }
  return {};
}

FirmwareLifecycleState
FirmwareLifecycleController::State(std::uint32_t nowMs) const noexcept {
  FirmwareLifecycleState state;
  state.powerButtonHeld = powerButtonHeld_;
  state.criticalBattery = criticalBattery_;
  state.shutdownLatched = powerShutdownLatched_ || batteryShutdownLatched_;
  if (powerButtonHeld_) {
    state.powerHoldRemainingMs = Remaining(
        PowerHoldMs, static_cast<std::uint32_t>(nowMs - powerPressedMs_));
  }
  if (criticalBattery_) {
    state.criticalBatteryRemainingMs =
        Remaining(CriticalBatteryShutdownMs, criticalBatteryElapsedMs_);
  }
  return state;
}

FirmwareLifecycleCommand FirmwareLifecycleController::Latch(
    FirmwareShutdownReason reason) noexcept {
  return {.shutdownReason = reason};
}

void FirmwareLifecycleController::ResetCriticalBattery() noexcept {
  criticalBatteryElapsedMs_ = 0U;
  criticalBattery_ = false;
  previousBatterySampleCritical_ = false;
  batteryShutdownLatched_ = false;
}
