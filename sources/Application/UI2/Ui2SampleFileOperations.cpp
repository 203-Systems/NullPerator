/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2SampleFileOperations.h"
#include "Application/UI2/Ui2SamplePathPolicy.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ui2 {
namespace {

constexpr const char *kDeleteStageName = ".ui2-sample-delete.tmp";

bool FormatPath(Ui2ProjectSamplePath &destination, const char *projectName,
                const char *sampleName) {
  destination.fill('\0');
  if (projectName == nullptr || projectName[0] == '\0' ||
      !Ui2IsFlatProjectSampleLeaf(sampleName))
    return false;
  const int written = std::snprintf(destination.data(), destination.size(),
                                    "%s/%s/%s/%s", PROJECTS_DIR, projectName,
                                    PROJECT_SAMPLES_DIR, sampleName);
  return written > 0 && static_cast<std::size_t>(written) < destination.size();
}

} // namespace

bool Ui2ResolveImportedSampleName(const char *sourceName,
                                  Ui2ProjectSampleName &destination) {
  destination.fill('\0');
  if (sourceName == nullptr || sourceName[0] == '\0')
    return false;
  const std::size_t length = std::strlen(sourceName);
  if (length <= MAX_INSTRUMENT_FILENAME_LENGTH) {
    std::memcpy(destination.data(), sourceName, length);
    return true;
  }
  constexpr std::size_t extensionLength = 4U;
  constexpr std::size_t stemLength =
      MAX_INSTRUMENT_FILENAME_LENGTH - extensionLength;
  std::memcpy(destination.data(), sourceName, stemLength);
  std::memcpy(destination.data() + stemLength, ".wav", extensionLength);
  return true;
}

bool Ui2BuildProjectSamplePath(const char *projectName, const char *sampleName,
                               Ui2ProjectSamplePath &destination) {
  return FormatPath(destination, projectName, sampleName);
}

Ui2DeleteProjectSampleResult Ui2DeleteProjectSampleSafely(
    FileSystem &fileSystem, SamplePool &pool, const char *projectName,
    const char *sampleName) {
  Ui2ProjectSamplePath source{};
  Ui2ProjectSamplePath staged{};
  if (!FormatPath(source, projectName, sampleName) ||
      !FormatPath(staged, projectName, kDeleteStageName))
    return Ui2DeleteProjectSampleResult::Invalid;
  const std::uint32_t index = pool.FindSampleIndexByName(
      etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>(sampleName));
  if (index >= static_cast<std::uint32_t>(pool.GetNameListSize()) ||
      !fileSystem.exists(source.data()) || fileSystem.exists(staged.data()))
    return Ui2DeleteProjectSampleResult::Invalid;

  if (!fileSystem.MoveFile(source.data(), staged.data()))
    return Ui2DeleteProjectSampleResult::StageFailed;
  if (!pool.unloadSample(index)) {
    // Best effort rollback. Even if the rename back fails, the bytes remain
    // under the hidden staged path and are not destroyed.
    fileSystem.MoveFile(staged.data(), source.data());
    return Ui2DeleteProjectSampleResult::UnloadFailed;
  }
  return fileSystem.DeleteFile(staged.data())
             ? Ui2DeleteProjectSampleResult::Deleted
             : Ui2DeleteProjectSampleResult::CleanupFailed;
}

} // namespace ui2
