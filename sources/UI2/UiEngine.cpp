/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/UiEngine.h"

namespace ui2 {

PresentResult UiEngine::PresentDirty() {
  if (!surface_.DirtyTiles().Any()) return PresentResult::Deferred;
  if (!surface_.DirtyTiles().Collect(storage_.strips)) {
    return PresentResult::Failed;
  }
  const PresentResult result =
      presenter_.Present(surface_, palette_, storage_.strips.Strips());
  if (result == PresentResult::Presented) surface_.ClearDirty();
  return result;
}

} // namespace ui2
