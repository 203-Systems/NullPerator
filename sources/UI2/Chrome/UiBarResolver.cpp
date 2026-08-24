/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Chrome/UiBarResolver.h"

namespace ui2 {

UiResolvedChrome UiBarResolver::Resolve(const UiBarInputs &inputs) {
  UiResolvedChrome resolved{inputs.pageTop, inputs.pageDefault};
  if (inputs.cursorContext != nullptr) resolved.bottom = *inputs.cursorContext;

  if (inputs.editHeldNumber && inputs.editHeldTracks != nullptr) {
    resolved.top.metaSelected = true;
    resolved.bottom.kind = UiBottomBarKind::TrackNotes;
    resolved.bottom.trackNotes = *inputs.editHeldTracks;
  }

  if (inputs.enterHeldAdjustment != nullptr) {
    resolved.bottom.kind = UiBottomBarKind::AdjustmentLegend;
    resolved.bottom.adjustment = *inputs.enterHeldAdjustment;
  }

  if (inputs.navHeld) {
    resolved.top.power = UiPowerState::Navigation;
    resolved.top.navTarget = inputs.navTarget;
  }

  if (inputs.criticalModal != nullptr) resolved.bottom = *inputs.criticalModal;
  return resolved;
}

} // namespace ui2
