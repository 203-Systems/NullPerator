/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

// Shared transport mode without pulling Project and instrument storage into
// small controller and host-test seams. The explicit underlying type preserves
// the enum's existing ABI while allowing a standalone definition.
enum PlayMode : int { PM_SONG, PM_CHAIN, PM_PHRASE, PM_LIVE, PM_AUDITION };
