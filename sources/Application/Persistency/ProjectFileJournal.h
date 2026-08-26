/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "System/Console/Trace.h"

#include <cstring>

namespace project_file_journal {

// PersistencyService owns project-name policy and turns a project plus payload
// kind into these paths.  The journal deliberately knows neither so the same
// filesystem transaction can be fault-tested without constructing the model.
struct Paths {
  const char *destination;
  const char *temporary;
  const char *backup;
};

inline bool HasDistinctPaths(const Paths &paths) {
  return paths.destination != nullptr && paths.temporary != nullptr &&
         paths.backup != nullptr && paths.destination[0] != '\0' &&
         paths.temporary[0] != '\0' && paths.backup[0] != '\0' &&
         std::strcmp(paths.destination, paths.temporary) != 0 &&
         std::strcmp(paths.destination, paths.backup) != 0 &&
         std::strcmp(paths.temporary, paths.backup) != 0;
}

template <typename FileSystemType, typename Validator>
bool Recover(FileSystemType &fileSystem, const Paths &paths,
             Validator &&validate) {
  if (!HasDistinctPaths(paths))
    return false;

  const bool destinationValid =
      fileSystem.exists(paths.destination) && validate(paths.destination);
  if (destinationValid) {
    // Structural XML validity is not the semantic commit boundary. Keep the
    // previous generation until TrackerApplicationSession has restored the
    // destination without MarkError. A stale temp is uncommitted and safe to
    // discard, while the backup is finalized explicitly by Session.
    if (fileSystem.exists(paths.temporary) &&
        !fileSystem.DeleteFile(paths.temporary)) {
      Trace::Error("PERSISTENCYSERVICE: Stale project temp remains");
    }
    return true;
  }

  const bool backupValid =
      fileSystem.exists(paths.backup) && validate(paths.backup);
  const bool temporaryValid =
      fileSystem.exists(paths.temporary) && validate(paths.temporary);
  const char *recoveryPath =
      backupValid ? paths.backup
                  : temporaryValid ? paths.temporary : nullptr;
  if (recoveryPath == nullptr) {
    // A corrupt payload must not shadow another project generation. Preserve
    // all potentially useful bytes when none is structurally valid.
    return true;
  }

  if (fileSystem.exists(paths.destination) &&
      !fileSystem.DeleteFile(paths.destination)) {
    Trace::Error("PERSISTENCYSERVICE: Could not remove corrupt project file");
    return false;
  }
  if (!fileSystem.MoveFile(recoveryPath, paths.destination)) {
    Trace::Error("PERSISTENCYSERVICE: Could not recover project journal");
    return false;
  }
  const char *otherPath =
      recoveryPath == paths.backup ? paths.temporary : paths.backup;
  if (fileSystem.exists(otherPath) && !fileSystem.DeleteFile(otherPath))
    Trace::Error("PERSISTENCYSERVICE: Stale project journal remains");
  return true;
}

template <typename FileSystemType, typename Writer, typename Validator>
bool SaveAtomically(FileSystemType &fileSystem, const Paths &paths,
                    Writer &&writeTemporary, Validator &&validate) {
  if (!Recover(fileSystem, paths, validate))
    return false;

  if (fileSystem.exists(paths.temporary) &&
      !fileSystem.DeleteFile(paths.temporary)) {
    return false;
  }
  if (!writeTemporary(paths.temporary) || !validate(paths.temporary)) {
    (void)fileSystem.DeleteFile(paths.temporary);
    return false;
  }

  // POSIX/WASM can atomically replace the destination in one rename.
  if (fileSystem.MoveFile(paths.temporary, paths.destination)) {
    if (fileSystem.exists(paths.backup) &&
        !fileSystem.DeleteFile(paths.backup)) {
      Trace::Error("PERSISTENCYSERVICE: Project backup cleanup deferred");
    }
    return true;
  }
  if (!fileSystem.exists(paths.destination)) {
    (void)fileSystem.DeleteFile(paths.temporary);
    return false;
  }

  // SdFat refuses rename-over-existing. Keep the old file as a journal until
  // the synced replacement is installed.
  if (fileSystem.exists(paths.backup) &&
      !fileSystem.DeleteFile(paths.backup)) {
    (void)fileSystem.DeleteFile(paths.temporary);
    return false;
  }
  if (!fileSystem.MoveFile(paths.destination, paths.backup)) {
    (void)fileSystem.DeleteFile(paths.temporary);
    return false;
  }
  if (!fileSystem.MoveFile(paths.temporary, paths.destination)) {
    if (!fileSystem.MoveFile(paths.backup, paths.destination))
      Trace::Error("PERSISTENCYSERVICE: Could not restore project file");
    (void)fileSystem.DeleteFile(paths.temporary);
    return false;
  }
  if (!fileSystem.DeleteFile(paths.backup))
    Trace::Error("PERSISTENCYSERVICE: Project backup cleanup deferred");
  return true;
}

template <typename FileSystemType, typename Validator>
bool PromoteBackup(FileSystemType &fileSystem, const Paths &paths,
                   Validator &&validate) {
  if (!HasDistinctPaths(paths) || !fileSystem.exists(paths.backup) ||
      !validate(paths.backup)) {
    return false;
  }
  if (fileSystem.exists(paths.temporary) &&
      !fileSystem.DeleteFile(paths.temporary)) {
    return false;
  }
  // The backup remains the only known semantic-good generation until the
  // structurally valid but rejected destination has been removed. A crash in
  // this window is recovered by Recover().
  if (fileSystem.exists(paths.destination) &&
      !fileSystem.DeleteFile(paths.destination)) {
    return false;
  }
  if (!fileSystem.MoveFile(paths.backup, paths.destination))
    return false;
  return validate(paths.destination);
}

template <typename FileSystemType>
bool Finalize(FileSystemType &fileSystem, const Paths &paths) {
  if (!HasDistinctPaths(paths))
    return false;
  if (fileSystem.exists(paths.temporary) &&
      !fileSystem.DeleteFile(paths.temporary)) {
    return false;
  }
  return !fileSystem.exists(paths.backup) ||
         fileSystem.DeleteFile(paths.backup);
}

// Discard is ordered and fail-closed: the authoritative payload remains until
// no stale backup can be promoted over a newer committed base after reboot.
template <typename FileSystemType>
bool Discard(FileSystemType &fileSystem, const Paths &paths) {
  if (!HasDistinctPaths(paths))
    return false;
  const char *files[] = {paths.temporary, paths.backup, paths.destination};
  for (const char *path : files) {
    if (fileSystem.exists(path) && !fileSystem.DeleteFile(path))
      return false;
  }
  return true;
}

} // namespace project_file_journal
