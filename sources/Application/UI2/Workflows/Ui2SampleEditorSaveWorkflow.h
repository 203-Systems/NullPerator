/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Ui2SampleEditorTransaction.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace ui2 {

enum class Ui2SampleEditorSaveFailureResolution : std::uint8_t {
  KeepEditorForRetry,
  ReloadDestination,
};

enum class Ui2SampleEditorSaveFollowUp : std::uint8_t {
  None,
  ReturnToCaller,
  SaveAndLoad,
};

// Keeps application policy around the transaction boundary explicit and
// directly testable. A successful promotion replaces a directory entry, so an
// active return browser must be refreshed before any follow-up can import,
// navigate, render metadata, or preview that entry.
class Ui2SampleEditorSaveWorkflow final {
public:
  [[nodiscard]] static constexpr Ui2SampleEditorSaveFailureResolution
  ResolveFailure(Ui2SampleEditorTransactionResult result) {
    return result == Ui2SampleEditorTransactionResult::SaveFailedRetryable
               ? Ui2SampleEditorSaveFailureResolution::KeepEditorForRetry
               : Ui2SampleEditorSaveFailureResolution::ReloadDestination;
  }

  template <typename RefreshBrowser>
  [[nodiscard]] static Ui2SampleEditorSaveFollowUp
  PrepareFollowUp(Ui2SampleEditorTransactionResult result,
                  bool saveAndLoadRequested, bool browserRefreshAvailable,
                  RefreshBrowser &&refreshBrowser) {
    if (result != Ui2SampleEditorTransactionResult::Saved &&
        result != Ui2SampleEditorTransactionResult::NoChanges)
      return Ui2SampleEditorSaveFollowUp::None;
    if (result == Ui2SampleEditorTransactionResult::Saved &&
        browserRefreshAvailable)
      std::forward<RefreshBrowser>(refreshBrowser)();
    return saveAndLoadRequested ? Ui2SampleEditorSaveFollowUp::SaveAndLoad
                                : Ui2SampleEditorSaveFollowUp::ReturnToCaller;
  }
};

static_assert(std::is_empty_v<Ui2SampleEditorSaveWorkflow>);
static_assert(sizeof(Ui2SampleEditorSaveFailureResolution) == 1U);
static_assert(sizeof(Ui2SampleEditorSaveFollowUp) == 1U);

} // namespace ui2
