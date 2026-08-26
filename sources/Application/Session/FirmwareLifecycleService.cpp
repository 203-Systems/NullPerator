/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "FirmwareLifecycleService.h"

FirmwareMidiLifecycleResult
FirmwareLifecycleService::InitializeMidi() noexcept {
  if (midiInitialized_)
    return FirmwareMidiLifecycleResult::AlreadyStarted;
  if (platform_ == nullptr || !platform_->InitializeMidi())
    return FirmwareMidiLifecycleResult::Failed;
  midiInitialized_ = true;
  return FirmwareMidiLifecycleResult::Started;
}

FirmwareMidiLifecycleResult FirmwareLifecycleService::CloseMidi() noexcept {
  if (!midiInitialized_)
    return FirmwareMidiLifecycleResult::AlreadyStopped;
  if (platform_ != nullptr)
    platform_->CloseMidi();
  midiInitialized_ = false;
  return FirmwareMidiLifecycleResult::Stopped;
}

FirmwareBootPreparation
FirmwareLifecycleService::PrepareProjectBoot(bool forceUntitled) noexcept {
  FirmwareBootPreparation result{.requested = forceUntitled};
  if (!forceUntitled || platform_ == nullptr ||
      !platform_->CanPrepareForcedUntitled()) {
    return result;
  }
  result.platformAvailable = true;
  // Both calls are intentional even if the marker was already absent. The
  // boot override means "fresh untitled", not merely "forget current".
  result.currentMarkerDeleted = platform_->DeleteCurrentProjectMarker();
  result.untitledPurged = platform_->PurgeUntitledProject();
  return result;
}

FirmwareLifecycleCommand FirmwareLifecycleService::Tick(
    FirmwareLifecycleController &controller, std::uint32_t nowMs) noexcept {
  if (shutdownDispatched_)
    return {};

  FirmwareLifecycleCommand command = controller.Tick(nowMs);
  if (command.HasValue())
    return command;
  if (platform_ == nullptr)
    return {};

  const bool due = !batterySampleInitialized_ ||
                   static_cast<std::uint32_t>(nowMs - lastBatterySampleMs_) >=
                       BatterySampleIntervalMs;
  if (!due)
    return {};
  batterySampleInitialized_ = true;
  lastBatterySampleMs_ = nowMs;
  lastBatterySample_ = platform_->ReadBattery();
  return controller.ObserveBattery(lastBatterySample_, nowMs);
}

FirmwareShutdownDispatch
FirmwareLifecycleService::Execute(FirmwareLifecycleCommand command) noexcept {
  if (!command.HasValue())
    return FirmwareShutdownDispatch::Ignored;
  if (shutdownDispatched_)
    return FirmwareShutdownDispatch::AlreadyDispatched;
  if (platform_ == nullptr)
    return FirmwareShutdownDispatch::PlatformUnavailable;
  shutdownDispatched_ = true;
  platform_->PowerDown();
  return FirmwareShutdownDispatch::RequestDispatched;
}
