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

constexpr char kDeleteStagePrefix[] = ".ui2-sample-delete.";
constexpr char kDeleteStageSuffix[] = ".tmp";
using DeleteStageName = std::array<char, sizeof(kDeleteStagePrefix) - 1U +
                                             MAX_INSTRUMENT_FILENAME_LENGTH +
                                             sizeof(kDeleteStageSuffix)>;

bool IsFlatProjectName(const char *projectName) {
  if (projectName == nullptr || projectName[0] == '\0' ||
      std::strlen(projectName) > MAX_PROJECT_NAME_LENGTH ||
      std::strcmp(projectName, ".") == 0 ||
      std::strcmp(projectName, "..") == 0) {
    return false;
  }
  for (const char *cursor = projectName; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\')
      return false;
  }
  return true;
}

bool FormatPath(Ui2ProjectSamplePath &destination, const char *projectName,
                const char *sampleName) {
  destination.fill('\0');
  if (!IsFlatProjectName(projectName) ||
      !Ui2IsFlatProjectSampleLeaf(sampleName))
    return false;
  const int written = std::snprintf(destination.data(), destination.size(),
                                    "%s/%s/%s/%s", PROJECTS_DIR, projectName,
                                    PROJECT_SAMPLES_DIR, sampleName);
  return written > 0 && static_cast<std::size_t>(written) < destination.size();
}

bool FormatDeleteStageName(DeleteStageName &destination,
                           const char *sampleName) {
  destination.fill('\0');
  if (!Ui2IsFlatProjectSampleLeaf(sampleName))
    return false;
  const std::size_t sampleLength = std::strlen(sampleName);
  if (sampleLength > MAX_INSTRUMENT_FILENAME_LENGTH)
    return false;
  const int written =
      std::snprintf(destination.data(), destination.size(), "%s%s%s",
                    kDeleteStagePrefix, sampleName, kDeleteStageSuffix);
  return written > 0 && static_cast<std::size_t>(written) < destination.size();
}

bool ParseDeleteStageName(const char *stageName,
                          Ui2ProjectSampleName &sampleName) {
  sampleName.fill('\0');
  if (stageName == nullptr)
    return false;
  constexpr std::size_t prefixLength = sizeof(kDeleteStagePrefix) - 1U;
  constexpr std::size_t suffixLength = sizeof(kDeleteStageSuffix) - 1U;
  const std::size_t stageLength = std::strlen(stageName);
  if (stageLength <= prefixLength + suffixLength ||
      std::strncmp(stageName, kDeleteStagePrefix, prefixLength) != 0 ||
      std::strcmp(stageName + stageLength - suffixLength, kDeleteStageSuffix) !=
          0) {
    return false;
  }
  const std::size_t sampleLength = stageLength - prefixLength - suffixLength;
  if (sampleLength > MAX_INSTRUMENT_FILENAME_LENGTH)
    return false;
  std::memcpy(sampleName.data(), stageName + prefixLength, sampleLength);
  return Ui2IsFlatProjectSampleLeaf(sampleName.data());
}

bool RollbackStage(FileSystem &fileSystem, const char *staged,
                   const char *source) {
  if (fileSystem.MoveFile(staged, source))
    return true;
  // Some media adapters can report a delayed error after the rename reached
  // durable storage. Treat the observed postcondition as the authoritative
  // rollback result instead of stranding a restored sample behind an error.
  return fileSystem.exists(source) && !fileSystem.exists(staged);
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

bool Ui2RecoverStagedProjectSampleDeletes(
    FileSystem &fileSystem, const char *projectName) {
  Ui2ProjectSamplePath probe{};
  if (!FormatPath(probe, projectName, "probe.wav") ||
      !fileSystem.chdir(PROJECTS_DIR) || !fileSystem.chdir(projectName)) {
    return false;
  }
  if (!fileSystem.chdir(PROJECT_SAMPLES_DIR))
    return !fileSystem.exists(PROJECT_SAMPLES_DIR);

  etl::vector<int, MAX_FILE_INDEX_SIZE> entries;
  if (!fileSystem.listChecked(&entries, kDeleteStagePrefix, false, true) ||
      entries.full()) {
    return false;
  }

  bool recovered = true;
  for (const int index : entries) {
    if (fileSystem.getFileType(index) != PFT_FILE)
      continue;
    char stageLeaf[PFILENAME_SIZE]{};
    fileSystem.getFileName(index, stageLeaf, sizeof(stageLeaf));
    Ui2ProjectSampleName sampleName{};
    if (!ParseDeleteStageName(stageLeaf, sampleName))
      continue;

    Ui2ProjectSamplePath source{};
    Ui2ProjectSamplePath staged{};
    if (!FormatPath(source, projectName, sampleName.data()) ||
        !FormatPath(staged, projectName, stageLeaf)) {
      recovered = false;
      continue;
    }
    // A visible source plus a stage is ambiguous: never overwrite either
    // copy. Leaving both in place is the only lossless recovery response.
    if (fileSystem.exists(source.data())) {
      recovered = false;
      continue;
    }
    if (fileSystem.exists(staged.data()) &&
        !RollbackStage(fileSystem, staged.data(), source.data())) {
      recovered = false;
    }
  }
  return recovered;
}

Ui2DeleteProjectSampleResult Ui2DeleteProjectSampleSafely(
    FileSystem &fileSystem, SamplePool &pool, const char *projectName,
    const char *sampleName) {
  Ui2ProjectSamplePath source{};
  Ui2ProjectSamplePath staged{};
  DeleteStageName stageName{};
  if (!FormatPath(source, projectName, sampleName) ||
      !FormatDeleteStageName(stageName, sampleName) ||
      !FormatPath(staged, projectName, stageName.data()))
    return Ui2DeleteProjectSampleResult::Invalid;
  const std::uint32_t index = pool.FindSampleIndexByName(
      etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>(sampleName));
  const bool loaded =
      index < static_cast<std::uint32_t>(pool.GetNameListSize());

  if (fileSystem.exists(staged.data())) {
    if (fileSystem.exists(source.data()))
      return Ui2DeleteProjectSampleResult::Invalid;
    if (!loaded) {
      return fileSystem.DeleteFile(staged.data()) ||
                     !fileSystem.exists(staged.data())
                 ? Ui2DeleteProjectSampleResult::Deleted
                 : Ui2DeleteProjectSampleResult::CleanupFailed;
    }
    if (!RollbackStage(fileSystem, staged.data(), source.data()))
      return Ui2DeleteProjectSampleResult::RollbackFailed;
  }

  if (!loaded || !fileSystem.exists(source.data()))
    return Ui2DeleteProjectSampleResult::Invalid;

  if (!fileSystem.MoveFile(source.data(), staged.data()))
    return Ui2DeleteProjectSampleResult::StageFailed;
  if (!pool.unloadSample(index)) {
    return RollbackStage(fileSystem, staged.data(), source.data())
               ? Ui2DeleteProjectSampleResult::UnloadFailed
               : Ui2DeleteProjectSampleResult::RollbackFailed;
  }
  return fileSystem.DeleteFile(staged.data()) ||
                 !fileSystem.exists(staged.data())
             ? Ui2DeleteProjectSampleResult::Deleted
             : Ui2DeleteProjectSampleResult::CleanupFailed;
}

} // namespace ui2
