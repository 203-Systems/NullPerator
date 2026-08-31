/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

namespace tracker_session_detail {

// A project may remain usable when platform audio initialization fails. Do not
// mark the process-global Player as initialized until that transaction has
// actually committed: the next project activation must retry Init(), while
// later activations can use the allocation-free BindProject() path.
template <typename Initialize, typename Bind>
[[nodiscard]] bool ActivatePlayer(bool &initialized, Initialize &&initialize,
                                  Bind &&bind) {
  if (!initialized) {
    initialized = initialize();
    return initialized;
  }

  bind();
  return true;
}

} // namespace tracker_session_detail
