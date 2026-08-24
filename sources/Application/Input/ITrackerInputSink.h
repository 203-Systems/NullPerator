/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "TrackerInput.h"

class ITrackerInputSink {
public:
  virtual ~ITrackerInputSink() = default;
  virtual void DispatchTrackerAction(TrackerAction action, bool pressed) = 0;
};
