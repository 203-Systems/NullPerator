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
  RollbackFailed,
  CleanupFailed,
};

// Restores any interrupted hidden delete stages before SamplePool::Load()
// enumerates the project. Stages carry the original bounded sample leaf, so
// recovery never has to guess which visible filename owns the retained bytes.
[[nodiscard]] bool Ui2RecoverStagedProjectSampleDeletes(
    FileSystem &fileSystem, const char *projectName);

// Deletion is staged under a per-sample hidden non-WAV name before the live
// pool is mutated. If unloading or final cleanup fails, the bytes remain in a
// recoverable stage; a repeated request may finish cleanup and a later project
// load restores an unfinished stage before rebuilding the pool.
[[nodiscard]] Ui2DeleteProjectSampleResult Ui2DeleteProjectSampleSafely(
    FileSystem &fileSystem, SamplePool &pool, const char *projectName,
    const char *sampleName);

} // namespace ui2
