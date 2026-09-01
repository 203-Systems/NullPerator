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
  const UiTrackNotesModel *enterHeldTracks = nullptr;
  const UiAdjustmentLegendModel *enterHeldAdjustment = nullptr;
  bool selectionActive = false;
  bool selectionNextExpansionAll = false;
  bool clipboardReady = false;
  std::uint8_t clipboardWidth = 0;
  std::uint8_t clipboardHeight = 0;
  bool enterHeldNumber = false;
};

class UiBarResolver {
public:
  [[nodiscard]] static UiBottomBarModel
  SelectionMode(bool nextExpansionAll);
  [[nodiscard]] static UiBottomBarModel ClipboardReady(std::uint8_t width,
                                                       std::uint8_t height);
  [[nodiscard]] static UiResolvedChrome Resolve(const UiBarInputs &inputs);
};

} // namespace ui2
