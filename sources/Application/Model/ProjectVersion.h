/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

// File-format identity and schema compatibility are intentionally independent
// from the product version. CREATED_WITH is diagnostic metadata; SCHEMA alone
// decides whether a NullPerator document can be restored.
namespace nullperator_project {

inline constexpr char Format[] = "NP";
inline constexpr char Schema[] = "1";
inline constexpr int PicoCompatibilityVersionHundredths = 230;

} // namespace nullperator_project
