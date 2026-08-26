/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

namespace tracker_session_detail {

// A missing .untitled project is a normal first-boot state. It must reach the
// session's create path instead of failing the non-destructive preflight used
// for existing projects.
[[nodiscard]] constexpr bool ShouldPreflightProjectLoad(
    bool createProject, bool stagingProject, bool stagingPayloadExists) {
  return !createProject && (!stagingProject || stagingPayloadExists);
}

} // namespace tracker_session_detail
