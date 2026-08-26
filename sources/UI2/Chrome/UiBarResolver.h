/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiChromeModel.h"

namespace ui2 {

struct UiBarInputs {
  UiTopBarModel pageTop;
  UiBottomBarModel pageDefault;
  const UiBottomBarModel *cursorContext = nullptr;
  const UiBottomBarModel *criticalModal = nullptr;
  const UiTrackNotesModel *editHeldTracks = nullptr;
  const UiAdjustmentLegendModel *enterHeldAdjustment = nullptr;
  bool editHeldNumber = false;
  bool navHeld = false;
  UiNavTarget navTarget = UiNavTarget::None;
};

class UiBarResolver {
public:
  [[nodiscard]] static UiResolvedChrome Resolve(const UiBarInputs &inputs);
};

} // namespace ui2
