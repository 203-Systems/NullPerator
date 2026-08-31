/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Persistency/PersistencyService.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace ui2 {

// The storage-only untitled identity must not leak into user-facing state.
// Keep header and rename presentation decisions together so persistence can
// continue using its stable reserved name without UI callers knowing it.
class Ui2ProjectNamePresentation final {
public:
  explicit Ui2ProjectNamePresentation(const char *storageName)
      : storageName_(storageName == nullptr ? "" : storageName) {}

  [[nodiscard]] const char *Header() const {
    return IsInternalUntitled() ? "UNTITLED" : storageName_;
  }

  [[nodiscard]] const char *RenameDraft() const {
    return IsInternalUntitled() ? "" : storageName_;
  }

  [[nodiscard]] bool NeedsNameBeforeSave() const {
    return IsInternalUntitled();
  }

  template <std::size_t Size>
  void CopyHeaderTo(std::array<char, Size> &destination) const {
    destination.fill('\0');
    if constexpr (Size > 0U) {
      const char *source = Header();
      std::copy_n(source, std::min(std::strlen(source), Size - 1U),
                  destination.begin());
    }
  }

private:
  [[nodiscard]] bool IsInternalUntitled() const {
    return std::strcmp(storageName_, UNNAMED_PROJECT_NAME) == 0;
  }

  const char *storageName_;
};

} // namespace ui2
