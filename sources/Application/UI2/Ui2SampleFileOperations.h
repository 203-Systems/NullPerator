/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "System/FileSystem/FileSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ui2 {

using Ui2ProjectSamplePath =
    std::array<char, MAX_PROJECT_SAMPLE_PATH_LENGTH>;
using Ui2ProjectSampleName =
    std::array<char, MAX_INSTRUMENT_FILENAME_LENGTH + 1U>;

[[nodiscard]] bool Ui2ResolveImportedSampleName(
    const char *sourceName, Ui2ProjectSampleName &destination);

[[nodiscard]] bool Ui2BuildProjectSamplePath(
    const char *projectName, const char *sampleName,
    Ui2ProjectSamplePath &destination);

enum class Ui2DeleteProjectSampleResult : std::uint8_t {
  Deleted,
  Invalid,
  StageFailed,
  UnloadFailed,
  CleanupFailed,
};

// Deletion is staged under a hidden non-WAV name before the live pool is
// mutated. If the adapter cannot unload the sample, the original filename is
// restored. This makes Node/ESP32 and WASM deletion failure-safe while the
// older RP2040 adapter (whose unloadSample() is still unsupported) degrades to
// a no-op rather than deleting a file behind a live sample pointer.
[[nodiscard]] Ui2DeleteProjectSampleResult Ui2DeleteProjectSampleSafely(
    FileSystem &fileSystem, SamplePool &pool, const char *projectName,
    const char *sampleName);

} // namespace ui2
