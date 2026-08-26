/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

// Lightweight UI-facing entry point. Keeping PersistencyService and its XML
// adapter out of controller headers prevents stdio compatibility macros from
// leaking into unrelated renderer/test translation units.
[[nodiscard]] bool RecoverInstrumentExportJournals();
