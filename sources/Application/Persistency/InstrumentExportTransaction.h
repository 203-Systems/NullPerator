/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

enum class InstrumentExportTransactionResult {
  Saved,
  Exists,
  Error,
};

template <typename FileSystemType, typename Validator>
bool RecoverInstrumentExportFile(FileSystemType &fileSystem,
                                 const char *destination,
                                 const char *temporary, const char *backup,
                                 Validator &&validate) {
  if (destination == nullptr || temporary == nullptr || backup == nullptr ||
      destination[0] == '\0' || temporary[0] == '\0' || backup[0] == '\0' ||
      std::strcmp(destination, temporary) == 0 ||
      std::strcmp(destination, backup) == 0 ||
      std::strcmp(temporary, backup) == 0) {
    return false;
  }

  // A temporary payload is only prepared state. If power was lost before its
  // install, roll back to the last committed file instead of guessing whether
  // the user intended the replacement to complete.
  if (fileSystem.exists(temporary) &&
      !fileSystem.DeleteFile(temporary)) {
    return false;
  }
  if (!fileSystem.exists(backup))
    return true;

  // A retained backup means an overwrite was interrupted. Prefer a validated
  // installed file; otherwise restore a validated backup. Never discard the
  // only valid copy.
  if (fileSystem.exists(destination)) {
    if (validate(destination))
      return fileSystem.DeleteFile(backup);
    if (!validate(backup) || !fileSystem.DeleteFile(destination))
      return false;
  } else if (!validate(backup)) {
    return false;
  }
  return fileSystem.MoveFile(backup, destination);
}

// Instruments are small, single-file documents, but they still need the same
// media-failure guarantees as projects. The callbacks keep XML writing and
// validation outside this allocation-free journal so it can be fault-tested
// without constructing the complete instrument graph.
template <typename FileSystemType, typename Writer, typename Validator>
InstrumentExportTransactionResult ExportInstrumentFileAtomically(
    FileSystemType &fileSystem, const char *destination, const char *temporary,
    const char *backup, bool overwrite, Writer &&writeTemporary,
    Validator &&validate) {
  if (!RecoverInstrumentExportFile(fileSystem, destination, temporary, backup,
                                   validate)) {
    return InstrumentExportTransactionResult::Error;
  }

  const bool destinationExists = fileSystem.exists(destination);
  if (destinationExists && !overwrite)
    return InstrumentExportTransactionResult::Exists;

  if (!writeTemporary(temporary) || !validate(temporary)) {
    if (fileSystem.exists(temporary))
      (void)fileSystem.DeleteFile(temporary);
    return InstrumentExportTransactionResult::Error;
  }

  // POSIX/WASM may replace the destination in one rename. SdFat refuses that
  // operation, so fall back to a sibling backup journal on that platform.
  if (fileSystem.MoveFile(temporary, destination))
    return InstrumentExportTransactionResult::Saved;
  if (!destinationExists || !fileSystem.exists(destination)) {
    (void)fileSystem.DeleteFile(temporary);
    return InstrumentExportTransactionResult::Error;
  }
  if (fileSystem.exists(backup) && !fileSystem.DeleteFile(backup)) {
    (void)fileSystem.DeleteFile(temporary);
    return InstrumentExportTransactionResult::Error;
  }
  if (!fileSystem.MoveFile(destination, backup)) {
    (void)fileSystem.DeleteFile(temporary);
    return InstrumentExportTransactionResult::Error;
  }
  if (!fileSystem.MoveFile(temporary, destination)) {
    if (!fileSystem.MoveFile(backup, destination)) {
      // Leave the backup journal in place when restoration itself fails; the
      // next call can recover it before attempting a new export.
      return InstrumentExportTransactionResult::Error;
    }
    (void)fileSystem.DeleteFile(temporary);
    return InstrumentExportTransactionResult::Error;
  }

  // The replacement is complete. A cleanup failure is recoverable: the next
  // call validates the installed destination and removes the stale backup.
  (void)fileSystem.DeleteFile(backup);
  return InstrumentExportTransactionResult::Saved;
}

template <std::size_t Capacity>
bool BuildInstrumentExportSiblingPaths(char (&destination)[Capacity],
                                       char (&temporary)[Capacity],
                                       char (&backup)[Capacity],
                                       const char *directory,
                                       const char *name) {
  if (directory == nullptr || name == nullptr || name[0] == '\0' ||
      name[0] == '.' || std::strchr(name, '/') != nullptr ||
      std::strchr(name, '\\') != nullptr)
    return false;

  const int destinationLength =
      std::snprintf(destination, Capacity, "%s/%s.pti", directory, name);
  const int temporaryLength =
      std::snprintf(temporary, Capacity, "%s/%s.tmp", directory, name);
  const int backupLength =
      std::snprintf(backup, Capacity, "%s/%s.bak", directory, name);
  return destinationLength > 0 && temporaryLength > 0 && backupLength > 0 &&
         static_cast<std::size_t>(destinationLength) < Capacity &&
         static_cast<std::size_t>(temporaryLength) < Capacity &&
         static_cast<std::size_t>(backupLength) < Capacity;
}
