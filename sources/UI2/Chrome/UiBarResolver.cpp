/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Chrome/UiBarResolver.h"

namespace ui2 {

UiBottomBarModel UiBarResolver::SelectionMode(bool nextExpansionAll) {
  UiBottomBarModel bottom{};
  bottom.kind = UiBottomBarKind::Context;
  bottom.context.firstLineCount = 1U;
  bottom.context.firstLine[0] = {
      .text = "SELECTION MODE", .color = UiColorToken::TextColored, .x = 79};
  bottom.context.secondLineCount = 1U;
  bottom.context.secondLine[0] = {
      .text = nextExpansionAll
                  ? "OPTION: COPY  SHIFT+OPTION: ALL"
                  : "OPTION: COPY  SHIFT+OPTION: ROW",
      .color = UiColorToken::TextNormal,
      .x = 28};
  return bottom;
}

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

  if (inputs.selectionActive) {
    resolved.bottom = SelectionMode(inputs.selectionNextExpansionAll);
  }

  if (inputs.criticalModal != nullptr) resolved.bottom = *inputs.criticalModal;
  return resolved;
}

} // namespace ui2
