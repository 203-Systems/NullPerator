/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

// Kept separate from Project.h so single-file instrument persistence does not
// pull the complete Project/InstrumentBank/SampleInstrument graph into host
// tools merely to serialize the format marker. NullPerator uses its own marker
// while retaining the PicoTracker 2.3 data layout for legacy restore rules.
namespace nullperator_project {

inline constexpr char FileVersion[] = "NP0.1";
inline constexpr int PicoCompatibilityVersionHundredths = 230;

} // namespace nullperator_project
