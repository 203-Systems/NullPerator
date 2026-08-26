/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <type_traits>

namespace ui2 {

// Application-owned retry state for Config persistence. Config variables are
// updated eagerly so hardware and the preview respond immediately, but a
// failed filesystem sync must keep a pending write until a later explicit
// action or page transition can retry it.
class Ui2ConfigSaveState final {
public:
  constexpr void MarkDirty() { dirty_ = true; }
  constexpr void MarkSaved() { dirty_ = false; }
  [[nodiscard]] constexpr bool Dirty() const { return dirty_; }

  template <typename SaveOperation> bool Flush(SaveOperation &&save) {
    if (!dirty_)
      return true;
    if (!save())
      return false;
    dirty_ = false;
    return true;
  }

private:
  bool dirty_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2ConfigSaveState>);
static_assert(sizeof(Ui2ConfigSaveState) == 1U);

} // namespace ui2
