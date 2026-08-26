/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

// Kept separate from Project.h so single-file instrument persistence does not
// pull the complete Project/InstrumentBank/SampleInstrument graph into host
// tools merely to serialize the compatibility version attribute.
#define PROJECT_NUMBER "2.3-Beta3"
#define PROJECT_RELEASE "r"
