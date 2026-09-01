/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Chrome/UiBarResolver.h"

namespace ui2 {

UiBottomBarModel UiBarResolver::SelectionMode(bool nextExpansionAll,
                                              bool supportsInterpolation) {
  UiBottomBarModel bottom{};
  bottom.kind = UiBottomBarKind::Context;
  bottom.context.firstLineCount = 1U;
  bottom.context.firstLine[0] = {
      .text = "SELECTION MODE", .color = UiColorToken::TextColored, .x = 79};
  bottom.context.secondLineCount = 1U;
  bottom.context.secondLine[0] = {
      .text = supportsInterpolation
                  ? "OPTION: COPY  SHIFT+ENTER: INTERPOLATE"
                  : nextExpansionAll
                        ? "OPTION: COPY  SHIFT+OPTION: ALL"
                        : "OPTION: COPY  SHIFT+OPTION: ROW",
      .color = UiColorToken::TextNormal,
      .x = static_cast<std::int16_t>(supportsInterpolation ? 6 : 28)};
  return bottom;
}

UiBottomBarModel UiBarResolver::ClipboardNotice(std::uint8_t width,
                                                std::uint8_t height,
                                                UiClipboardBarModel::Notice notice) {
  UiBottomBarModel bottom{};
  bottom.kind = UiBottomBarKind::Clipboard;
  bottom.clipboard = {
      .width = width,
      .height = height,
      .notice = notice,
  };
  return bottom;
}

UiResolvedChrome UiBarResolver::Resolve(const UiBarInputs &inputs) {
  UiResolvedChrome resolved{inputs.pageTop, inputs.pageDefault};
  if (inputs.cursorContext != nullptr) resolved.bottom = *inputs.cursorContext;

  if (inputs.enterHeldNumber && inputs.enterHeldTracks != nullptr) {
    resolved.top.metaSelected = true;
    resolved.bottom.kind = UiBottomBarKind::TrackNotes;
    resolved.bottom.trackNotes = *inputs.enterHeldTracks;
  }

  if (inputs.enterHeldAdjustment != nullptr) {
    resolved.bottom.kind = UiBottomBarKind::AdjustmentLegend;
    resolved.bottom.adjustment = *inputs.enterHeldAdjustment;
  }

  if (inputs.selectionActive) {
    resolved.bottom = SelectionMode(inputs.selectionNextExpansionAll,
                                    inputs.selectionSupportsInterpolation);
  }

  // Operation acknowledgements temporarily replace selection help. This is
  // required for interpolation, which deliberately keeps the selection active.
  if (inputs.clipboardReady) {
    resolved.bottom = ClipboardNotice(
        inputs.clipboardWidth, inputs.clipboardHeight,
        inputs.clipboardPasted ? UiClipboardBarModel::Notice::Pasted
                               : UiClipboardBarModel::Notice::Copied);
  }

  if (inputs.criticalModal != nullptr) resolved.bottom = *inputs.criticalModal;
  return resolved;
}

} // namespace ui2
