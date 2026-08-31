/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "SampleEditorFileJournal.h"

#include "Application/Instruments/WavHeader.h"
#include "System/FileSystem/FileSystem.h"

#include <array>

bool SampleEditorFileJournal::ValidateWav(FileSystem &fileSystem,
                                          const char *path) {
  if (path == nullptr || path[0] == '\0' || !fileSystem.exists(path))
    return false;
  FileHandle file = fileSystem.Open(path, "r");
  if (!file)
    return false;
  const auto header = WavHeaderWriter::ReadHeader(file.get());
  if (!header.has_value() || header->numChannels == 0U ||
      header->numChannels > 2U || header->blockAlign == 0U ||
      header->dataChunkSize / header->blockAlign == 0U)
    return false;
  const bool supportedPcm =
      header->audioFormat == 1U && header->bytesPerSample >= 1U &&
      header->bytesPerSample <= 4U;
  const bool supportedFloat =
      header->audioFormat == 3U &&
      (header->bytesPerSample == 4U || header->bytesPerSample == 8U);
  return supportedPcm || supportedFloat;
}

bool SampleEditorFileJournal::RecoverDestination(FileSystem &fileSystem,
                                                 const char *destination) {
  std::array<char, PFILENAME_SIZE> working{};
  std::array<char, PFILENAME_SIZE> backup{};
  if (!BuildPath(destination, Generation::Working, working.data(),
                 working.size()) ||
      !BuildPath(destination, Generation::Backup, backup.data(),
                 backup.size()))
    return false;
  if (!fileSystem.exists(backup.data()))
    return true;

  if (ValidateWav(fileSystem, destination)) {
    if (!fileSystem.DeleteFile(backup.data()))
      return false;
  } else {
    if (!ValidateWav(fileSystem, backup.data()))
      return false;
    if (fileSystem.exists(destination) &&
        !fileSystem.DeleteFile(destination))
      return false;
    if (!fileSystem.MoveFile(backup.data(), destination) ||
        !ValidateWav(fileSystem, destination))
      return false;
  }

  return !fileSystem.exists(working.data()) ||
         fileSystem.DeleteFile(working.data());
}

bool SampleEditorFileJournal::RecoverCurrentDirectory(
    FileSystem &fileSystem) {
  etl::vector<int, MAX_FILE_INDEX_SIZE> entries;
  if (!fileSystem.listChecked(&entries, ".b", false, true) || entries.full())
    return false;

  for (const int entry : entries) {
    if (fileSystem.getFileType(entry) != PFT_FILE)
      continue;
    std::array<char, PFILENAME_SIZE> backup{};
    std::array<char, PFILENAME_SIZE> destination{};
    fileSystem.getFileName(entry, backup.data(),
                           static_cast<int>(backup.size()));
    backup.back() = '\0';
    if (!DecodeBackupPath(backup.data(), destination.data(),
                          destination.size()))
      continue;
    // Journal leaf names are reserved. A backup can be the only surviving
    // generation after a failed rollback, so destination/working existence
    // cannot be used as a prerequisite for recovery.
    if (!RecoverDestination(fileSystem, destination.data()))
      return false;
  }

  // Recovery mutates directory entries. Refresh the adapter's index cache
  // before inspecting working journals; FAT directory indexes are not stable
  // across rename/delete operations.
  entries.clear();
  if (!fileSystem.listChecked(&entries, ".w", false, true) || entries.full())
    return false;
  for (const int entry : entries) {
    if (fileSystem.getFileType(entry) != PFT_FILE)
      continue;
    std::array<char, PFILENAME_SIZE> working{};
    std::array<char, PFILENAME_SIZE> destination{};
    fileSystem.getFileName(entry, working.data(),
                           static_cast<int>(working.size()));
    working.back() = '\0';
    if (!DecodeWorkingPath(working.data(), destination.data(),
                           destination.size()))
      continue;
    // Copy creates the working generation while the destination is still
    // authoritative. A working-only journal is therefore disposable only
    // when that destination remains a valid WAV.
    if (ValidateWav(fileSystem, destination.data()) &&
        !fileSystem.DeleteFile(working.data()))
      return false;
  }
  return true;
}
