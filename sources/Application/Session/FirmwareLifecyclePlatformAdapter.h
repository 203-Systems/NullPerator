/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "FirmwareLifecycleService.h"

// Production boundary for the already-installed PicoTracker services. It is
// stateless so an application can embed it without adding heap ownership.
class FirmwareLifecyclePlatformAdapter final
    : public IFirmwareLifecyclePlatform {
public:
  bool InitializeMidi() override;
  void CloseMidi() override;
  FirmwareBatterySample ReadBattery() override;
  void PowerDown() override;
  [[nodiscard]] bool CanPrepareForcedUntitled() const override;
  bool DeleteCurrentProjectMarker() override;
  bool PurgeUntitledProject() override;
};
