/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "../Instruments/SamplePool.h"
#include "Foundation/Services/ServiceRegistry.h"
#include "PersistencyService.h"

#include "Foundation/Types/Types.h"
#include "InstrumentExportTransaction.h"
#include "InstrumentFileValidator.h"
#include "Persistent.h"
#include "ProjectFileJournal.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include <cstdio>
#include <cstring>

#include "PersistencyPaths.h"
using namespace PersistencyPaths;

bool PersistencyService::RecoverStagingProjectReplacement_() {
  FileSystem *fs = FileSystem::GetInstance();
  if (fs->exists(STAGING_TRANSACTION_PURGE_FILE)) {
    if (!HasStagingTransactionMarker_(STAGING_TRANSACTION_PURGE_FILE,
                                      STAGING_PURGE_CONTENTS)) {
      Trace::Error("PERSISTENCYSERVICE: Invalid untitled purge marker");
      return false;
    }
    return CompleteStagingProjectPurge_();
  }
  // A temp without the installed marker precedes every destructive purge
  // step, so it is safe to discard and resume ordinary replacement recovery.
  if (fs->exists(STAGING_TRANSACTION_PURGE_TEMP_FILE) &&
      !fs->DeleteFile(STAGING_TRANSACTION_PURGE_TEMP_FILE)) {
    Trace::Error("PERSISTENCYSERVICE: Could not clear stale purge temp");
    return false;
  }

  bool pendingHadPrevious = false;
  char previousProjectName[MAX_PROJECT_NAME_LENGTH + 1U]{};
  const bool pendingExists = fs->exists(STAGING_TRANSACTION_PENDING_FILE);
  const bool pendingValid =
      pendingExists &&
      ReadStagingPendingMarker_(pendingHadPrevious, previousProjectName);
  if (pendingExists && !pendingValid) {
    Trace::Error("PERSISTENCYSERVICE: Invalid untitled pending marker");
    return false;
  }
  const bool commitExists = fs->exists(STAGING_TRANSACTION_COMMIT_FILE);
  const bool committed = HasStagingTransactionMarker_(
      STAGING_TRANSACTION_COMMIT_FILE, STAGING_COMMIT_CONTENTS);
  if (commitExists && !committed) {
    Trace::Error("PERSISTENCYSERVICE: Invalid untitled commit marker");
    return false;
  }

  bool stagingExists = fs->exists(STAGING_PROJECT_PATH);
  bool backupExists = fs->exists(STAGING_BACKUP_PATH);

  if (committed) {
    // COMMITTED is authoritative only while the replacement itself remains
    // structurally complete. If it disappeared/corrupted, the previous
    // directory and pointer are still safer than silently booting a partial
    // candidate.
    if (!stagingExists ||
        Validate_(UNNAMED_PROJECT_NAME, true) != PERSIST_LOADED) {
      char ignoredPrevious[MAX_PROJECT_NAME_LENGTH + 1U]{};
      return RollbackCommittedStagingProjectReplacement_(ignoredPrevious);
    }
    // Structural validity is only preflight. Keep the old untitled directory,
    // previous current-project marker and COMMITTED record until Session has
    // completed semantic restore. It then calls the explicit finalizer below.
    return true;
  }

  if (pendingValid) {
    if (pendingHadPrevious) {
      if (backupExists) {
        if (stagingExists && !DeleteProject_(UNNAMED_PROJECT_NAME, true))
          return false;
        if (!fs->MoveFile(STAGING_BACKUP_PATH, STAGING_PROJECT_PATH)) {
          Trace::Error("PERSISTENCYSERVICE: Could not roll back untitled");
          return false;
        }
      } else if (!stagingExists) {
        Trace::Error("PERSISTENCYSERVICE: Previous untitled copy is missing");
        return false;
      }
    } else {
      // No staging directory existed at Begin. Any `.untitled` visible now is
      // the uncommitted candidate and must not become committed merely because
      // its XML happens to parse.
      if (backupExists) {
        Trace::Error("PERSISTENCYSERVICE: Unexpected empty-stage backup");
        return false;
      }
      if (stagingExists && !DeleteProject_(UNNAMED_PROJECT_NAME, true))
        return false;
    }

    // Restore the previous project pointer before deleting PENDING. The
    // marker itself is the durable retry record if this FAT-safe rewrite is
    // interrupted.
    if (previousProjectName[0] != '\0') {
      if (SaveProjectState_(
              previousProjectName,
              std::strcmp(previousProjectName, UNNAMED_PROJECT_NAME) == 0) !=
          PERSIST_SAVED) {
        return false;
      }
    } else {
      constexpr const char *stateFiles[] = {
          PROJECT_STATE_FILE,
          PROJECT_STATE_TEMP_FILE,
          PROJECT_STATE_BACKUP_FILE,
          PROJECT_STATE_BACKUP_TEMP_FILE,
      };
      for (const char *path : stateFiles) {
        if (fs->exists(path) && !fs->DeleteFile(path))
          return false;
      }
    }
    return ClearStagingTransactionMarkers_();
  }

  if (!backupExists) {
    // No active transaction. Clear only stale temp/phase files.
    return ClearStagingTransactionMarkers_();
  }

  if (!stagingExists) {
    if (!fs->MoveFile(STAGING_BACKUP_PATH, STAGING_PROJECT_PATH)) {
      Trace::Error("PERSISTENCYSERVICE: Could not recover untitled backup");
      return false;
    }
    return ClearStagingTransactionMarkers_();
  }

  // Legacy transaction state without a phase marker: prefer the previous
  // directory, but do not guess a previous named-project pointer.
  if (!DeleteProject_(UNNAMED_PROJECT_NAME, true) ||
      !fs->MoveFile(STAGING_BACKUP_PATH, STAGING_PROJECT_PATH)) {
    Trace::Error("PERSISTENCYSERVICE: Could not roll back untitled project");
    return false;
  }
  return ClearStagingTransactionMarkers_();
}

bool PersistencyService::RecoverSaveAsTransactions_() {
  FileSystem *fs = FileSystem::GetInstance();
  if (!fs->exists(PROJECTS_DIR))
    return true;
  if (!fs->chdir(PROJECTS_DIR))
    return false;

  // Handle one journal directory per pass because deleting a directory may
  // invalidate platform file indices. The number of passes is bounded by the
  // fixed-capacity directory listing.
  for (size_t pass = 0; pass < MAX_FILE_INDEX_SIZE; ++pass) {
    fileIndexes_.clear();
    if (!fs->listChecked(&fileIndexes_, "", true, true)) {
      Trace::Error("PERSISTENCYSERVICE: Save As recovery scan failed");
      return false;
    }
    if (fileIndexes_.full()) {
      Trace::Error("PERSISTENCYSERVICE: Project transaction list truncated");
      return false;
    }

    char transactionName[PFILENAME_SIZE]{};
    const char *target = nullptr;
    for (const int index : fileIndexes_) {
      if (fs->getFileType(index) != PFT_DIR)
        continue;
      transactionName[0] = '\0';
      fs->getFileName(index, transactionName, sizeof(transactionName));
      transactionName[sizeof(transactionName) - 1U] = '\0';
      target = SaveAsTransactionTarget(transactionName);
      if (target != nullptr)
        break;
    }
    if (target == nullptr)
      return true;

    char targetCopy[MAX_PROJECT_NAME_LENGTH + 1U]{};
    if (!IsValidProjectName(target)) {
      Trace::Error("PERSISTENCYSERVICE: Invalid Save As journal name");
      if (!DeleteProject_(transactionName, true))
        return false;
      continue;
    }
    std::strcpy(targetCopy, target);

    char stageName[sizeof(SAVE_AS_STAGE_PREFIX) + MAX_PROJECT_NAME_LENGTH]{};
    char backupName[sizeof(SAVE_AS_BACKUP_PREFIX) + MAX_PROJECT_NAME_LENGTH]{};
    char targetPath[sizeof(PROJECTS_DIR) + MAX_PROJECT_NAME_LENGTH + 1U]{};
    char stagePath[sizeof(PROJECTS_DIR) + sizeof(stageName) + 1U]{};
    char backupPath[sizeof(PROJECTS_DIR) + sizeof(backupName) + 1U]{};
    if (!BuildSaveAsTransactionName(stageName, SAVE_AS_STAGE_PREFIX,
                                    targetCopy) ||
        !BuildSaveAsTransactionName(backupName, SAVE_AS_BACKUP_PREFIX,
                                    targetCopy) ||
        !BuildProjectPath(targetPath, targetCopy) ||
        !BuildProjectPath(stagePath, stageName) ||
        !BuildProjectPath(backupPath, backupName)) {
      return false;
    }

    const bool targetExists = fs->exists(targetPath);
    const bool stageExists = fs->exists(stagePath);
    const bool backupExists = fs->exists(backupPath);
    if (backupExists) {
      if (targetExists) {
        if (Validate_(targetCopy, false) == PERSIST_LOADED) {
          // stage->target completed. The validated target is authoritative.
          if (stageExists && !DeleteProject_(stageName, true))
            Trace::Error("PERSISTENCYSERVICE: Stale Save As stage remains");
          if (!DeleteProject_(backupName, true)) {
            // Do not block boot or delete the new target merely because
            // cleanup cannot finish. The browser hides this reserved journal.
            Trace::Error("PERSISTENCYSERVICE: Save As backup cleanup deferred");
            return true;
          }
        } else {
          // A target that cannot be parsed did not commit safely. Prefer the
          // pre-transaction backup rather than discarding the last good copy.
          if (stageExists && !DeleteProject_(stageName, true))
            Trace::Error("PERSISTENCYSERVICE: Stale Save As stage remains");
          if (!DeleteProject_(targetCopy, false) ||
              !fs->MoveFile(backupPath, targetPath)) {
            Trace::Error(
                "PERSISTENCYSERVICE: Could not roll back corrupt Save As");
            return false;
          }
        }
      } else {
        // target->backup completed but stage->target did not. Roll back the
        // old target; an uncommitted stage is never promoted at boot.
        if (stageExists && !DeleteProject_(stageName, true))
          Trace::Error("PERSISTENCYSERVICE: Save As stage cleanup deferred");
        if (!fs->MoveFile(backupPath, targetPath)) {
          Trace::Error("PERSISTENCYSERVICE: Could not restore Save As backup");
          return false;
        }
      }
    } else if (stageExists) {
      // A lone stage may be only partly copied. Never expose or promote it.
      if (!DeleteProject_(stageName, true)) {
        Trace::Error("PERSISTENCYSERVICE: Save As stage cleanup deferred");
        return true;
      }
    }
  }

  Trace::Error("PERSISTENCYSERVICE: Too many project transactions");
  return false;
}

bool PersistencyService::RecoverInternalProjectTransactions_() {
  return RecoverStagingProjectReplacement_() && RecoverSaveAsTransactions_();
}

bool PersistencyService::BeginStagingProjectReplacement_(
    const char *previousProjectName, bool &hadPrevious) {
  FileSystem *fs = FileSystem::GetInstance();
  hadPrevious = false;

  if (previousProjectName == nullptr ||
      (previousProjectName[0] != '\0' &&
       !IsSafeProjectName_(
           previousProjectName,
           std::strcmp(previousProjectName, UNNAMED_PROJECT_NAME) == 0))) {
    return false;
  }

  if (!RecoverStagingProjectReplacement_())
    return false;

  const bool stagingExists = fs->exists(STAGING_PROJECT_PATH);
  const bool backupExists = fs->exists(STAGING_BACKUP_PATH);
  if (stagingExists && backupExists) {
    Trace::Error("PERSISTENCYSERVICE: Ambiguous untitled backup state");
    return false;
  }
  if (backupExists) {
    Trace::Error("PERSISTENCYSERVICE: Untitled backup recovery incomplete");
    return false;
  }
  hadPrevious = stagingExists;

  // Record the transaction before moving the old directory.  If power fails
  // before the rename, recovery sees a valid staging directory and no backup
  // and preserves it.  If it fails after the rename, backup presence makes
  // the uncommitted rollback unambiguous.
  char pending[sizeof("PENDING:1:") + MAX_PROJECT_NAME_LENGTH]{};
  const int pendingLength =
      std::snprintf(pending, sizeof(pending), "PENDING:%c:%s",
                    hadPrevious ? '1' : '0', previousProjectName);
  if (pendingLength <= 0 ||
      static_cast<size_t>(pendingLength) >= sizeof(pending)) {
    return false;
  }
  if (!WriteStagingTransactionMarker_(STAGING_TRANSACTION_PENDING_FILE,
                                      STAGING_TRANSACTION_PENDING_TEMP_FILE,
                                      pending)) {
    Trace::Error(
        "PERSISTENCYSERVICE: Could not persist untitled pending state");
    return false;
  }

  if (!stagingExists)
    return true;

  if (!fs->MoveFile(STAGING_PROJECT_PATH, STAGING_BACKUP_PATH)) {
    Trace::Error("PERSISTENCYSERVICE: Could not stage untitled directory");
    if (!ClearStagingTransactionMarkers_())
      Trace::Error(
          "PERSISTENCYSERVICE: Could not clear untitled pending state");
    return false;
  }
  hadPrevious = true;
  return true;
}

bool PersistencyService::CommitStagingProjectReplacement_(bool hadPrevious) {
  FileSystem *fs = FileSystem::GetInstance();
  const bool commitExists = fs->exists(STAGING_TRANSACTION_COMMIT_FILE);
  const bool alreadyCommitted =
      commitExists &&
      HasStagingTransactionMarker_(STAGING_TRANSACTION_COMMIT_FILE,
                                   STAGING_COMMIT_CONTENTS);
  if (commitExists && !alreadyCommitted) {
    Trace::Error("PERSISTENCYSERVICE: Invalid untitled commit marker");
    return false;
  }
  if (!alreadyCommitted && hadPrevious && !fs->exists(STAGING_BACKUP_PATH)) {
    Trace::Error("PERSISTENCYSERVICE: Untitled transaction backup missing");
    return false;
  }

  // This marker, written only after SaveProjectState_ succeeds, is the
  // durable phase transition.  Recovery must never infer commit merely from
  // a parseable replacement directory because old and new are both named
  // `.untitled` in `.current`.
  if (!alreadyCommitted &&
      !WriteStagingTransactionMarker_(STAGING_TRANSACTION_COMMIT_FILE,
                                      STAGING_TRANSACTION_COMMIT_TEMP_FILE,
                                      STAGING_COMMIT_CONTENTS)) {
    Trace::Error("PERSISTENCYSERVICE: Could not persist untitled commit state");
    return false;
  }

  if (fs->exists(STAGING_BACKUP_PATH) &&
      !DeleteProject_(STAGING_BACKUP_PROJECT_NAME, true)) {
    // COMMITTED remains durable, so cleanup can safely resume at next boot.
    Trace::Error("PERSISTENCYSERVICE: Untitled backup cleanup deferred");
    return true;
  }
  if ((fs->exists(PROJECT_STATE_BACKUP_FILE) &&
       !fs->DeleteFile(PROJECT_STATE_BACKUP_FILE)) ||
      (fs->exists(PROJECT_STATE_BACKUP_TEMP_FILE) &&
       !fs->DeleteFile(PROJECT_STATE_BACKUP_TEMP_FILE))) {
    // Keep COMMITTED until the retained previous project pointer is cleaned.
    Trace::Error("PERSISTENCYSERVICE: Project marker cleanup deferred");
    return true;
  }
  if (!ClearStagingTransactionMarkers_()) {
    // With no backup left, stale markers are harmless and recovery will clear
    // them without changing the committed replacement.
    Trace::Error("PERSISTENCYSERVICE: Untitled marker cleanup deferred");
  }
  return true;
}

bool PersistencyService::HasCommittedStagingProjectReplacement_() {
  FileSystem *fs = FileSystem::GetInstance();
  return fs->exists(STAGING_TRANSACTION_COMMIT_FILE) &&
         HasStagingTransactionMarker_(STAGING_TRANSACTION_COMMIT_FILE,
                                      STAGING_COMMIT_CONTENTS);
}

bool PersistencyService::FinalizeCommittedStagingProjectReplacement_() {
  FileSystem *fs = FileSystem::GetInstance();
  if (!fs->exists(STAGING_TRANSACTION_COMMIT_FILE))
    return true;
  if (!HasCommittedStagingProjectReplacement_())
    return false;

  bool hadPrevious = fs->exists(STAGING_BACKUP_PATH);
  if (fs->exists(STAGING_TRANSACTION_PENDING_FILE)) {
    char previousProjectName[MAX_PROJECT_NAME_LENGTH + 1U]{};
    if (!ReadStagingPendingMarker_(hadPrevious, previousProjectName))
      return false;
  }
  return CommitStagingProjectReplacement_(hadPrevious);
}

bool PersistencyService::RollbackCommittedStagingProjectReplacement_(
    char *previousProjectName) {
  if (previousProjectName == nullptr)
    return false;
  previousProjectName[0] = '\0';

  FileSystem *fs = FileSystem::GetInstance();
  if (!HasCommittedStagingProjectReplacement_() ||
      !fs->exists(STAGING_TRANSACTION_PENDING_FILE)) {
    return false;
  }
  bool hadPrevious = false;
  if (!ReadStagingPendingMarker_(hadPrevious, previousProjectName))
    return false;

  // Restore the previous current-project pointer before changing either
  // untitled directory. PENDING + COMMITTED remain the durable retry record
  // if this marker write or any later directory operation loses power.
  if (previousProjectName[0] != '\0' &&
      SaveProjectState_(
          previousProjectName,
          std::strcmp(previousProjectName, UNNAMED_PROJECT_NAME) == 0) !=
          PERSIST_SAVED) {
    return false;
  }

  if (fs->exists(STAGING_PROJECT_PATH) &&
      !DeleteProject_(UNNAMED_PROJECT_NAME, true)) {
    return false;
  }
  const bool backupExists = fs->exists(STAGING_BACKUP_PATH);
  if (hadPrevious) {
    if (!backupExists ||
        !fs->MoveFile(STAGING_BACKUP_PATH, STAGING_PROJECT_PATH)) {
      Trace::Error("PERSISTENCYSERVICE: Committed untitled is unrecoverable");
      return false;
    }
  } else if (backupExists) {
    Trace::Error("PERSISTENCYSERVICE: Unexpected committed empty-stage backup");
    return false;
  }

  if (previousProjectName[0] == '\0') {
    constexpr const char *stateFiles[] = {
        PROJECT_STATE_FILE,
        PROJECT_STATE_TEMP_FILE,
        PROJECT_STATE_BACKUP_FILE,
        PROJECT_STATE_BACKUP_TEMP_FILE,
    };
    for (const char *path : stateFiles) {
      if (fs->exists(path) && !fs->DeleteFile(path))
        return false;
    }
  }
  return ClearStagingTransactionMarkers_();
}

bool PersistencyService::RollbackStagingProjectReplacement_(bool hadPrevious) {
  FileSystem *fs = FileSystem::GetInstance();
  (void)hadPrevious;
  // Never destructively roll back while a durable COMMITTED marker remains.
  // Otherwise keep PENDING until the shared recovery state machine has both
  // restored/deleted the staging directory and rewritten previous `.current`.
  // This makes a power loss at every rollback step retryable.
  if (fs->exists(STAGING_TRANSACTION_COMMIT_FILE)) {
    Trace::Error(
        "PERSISTENCYSERVICE: Refusing to roll back committed untitled");
    return false;
  }
  return RecoverStagingProjectReplacement_();
}

bool PersistencyService::CompleteStagingProjectPurge_() {
  FileSystem *fs = FileSystem::GetInstance();
  // The durable PURGE marker remains until both possible copies and every
  // replacement phase marker are gone. If any delete fails, recovery retries
  // this idempotent sequence instead of interpreting the backup as a project
  // that should be restored.
  if (fs->exists(STAGING_PROJECT_PATH) &&
      !DeleteProject_(UNNAMED_PROJECT_NAME, true)) {
    return false;
  }
  if (fs->exists(STAGING_BACKUP_PATH) &&
      !DeleteProject_(STAGING_BACKUP_PROJECT_NAME, true)) {
    return false;
  }
  constexpr const char *stateFiles[] = {
      PROJECT_STATE_FILE,
      PROJECT_STATE_TEMP_FILE,
      PROJECT_STATE_BACKUP_FILE,
      PROJECT_STATE_BACKUP_TEMP_FILE,
  };
  for (const char *path : stateFiles) {
    if (fs->exists(path) && !fs->DeleteFile(path))
      return false;
  }
  if (!ClearStagingTransactionMarkers_())
    return false;
  if (fs->exists(STAGING_TRANSACTION_PURGE_TEMP_FILE) &&
      !fs->DeleteFile(STAGING_TRANSACTION_PURGE_TEMP_FILE)) {
    return false;
  }
  return !fs->exists(STAGING_TRANSACTION_PURGE_FILE) ||
         fs->DeleteFile(STAGING_TRANSACTION_PURGE_FILE);
}

bool PersistencyService::ClearStagingTransactionMarkers_() {
  FileSystem *fs = FileSystem::GetInstance();
  constexpr const char *files[] = {
      STAGING_TRANSACTION_PENDING_FILE,
      STAGING_TRANSACTION_PENDING_TEMP_FILE,
      STAGING_TRANSACTION_COMMIT_FILE,
      STAGING_TRANSACTION_COMMIT_TEMP_FILE,
  };
  for (const char *path : files) {
    if (fs->exists(path) && !fs->DeleteFile(path))
      return false;
  }
  return true;
}

bool PersistencyService::WriteStagingTransactionMarker_(const char *path,
                                                        const char *tempPath,
                                                        const char *contents) {
  FileSystem *fs = FileSystem::GetInstance();
  if ((fs->exists(tempPath) && !fs->DeleteFile(tempPath)) || fs->exists(path))
    return false;

  auto marker = fs->Open(tempPath, "w");
  if (!marker)
    return false;
  const int length = static_cast<int>(std::strlen(contents));
  const bool synced = marker->Write(contents, 1, length) == length &&
                      marker->Sync() && marker->Error() == 0;
  I_File *rawFile = AcquireLegacyFileHandle_DO_NOT_USE(std::move(marker));
  const bool closed = CloseFile_DO_NOT_USE(rawFile);
  if (!synced || !closed || !fs->MoveFile(tempPath, path) ||
      !HasStagingTransactionMarker_(path, contents)) {
    if (fs->exists(tempPath))
      fs->DeleteFile(tempPath);
    if (fs->exists(path))
      fs->DeleteFile(path);
    return false;
  }
  return true;
}

bool PersistencyService::HasStagingTransactionMarker_(const char *path,
                                                      const char *contents) {
  FileSystem *fs = FileSystem::GetInstance();
  auto marker = fs->Open(path, "r");
  if (!marker)
    return false;
  char stored[sizeof("PENDING:1:") + MAX_PROJECT_NAME_LENGTH]{};
  const int length = marker->Read(stored, sizeof(stored) - 1U);
  const size_t expectedLength = std::strlen(contents);
  return length == static_cast<int>(expectedLength) && marker->Error() == 0 &&
         std::memcmp(stored, contents, expectedLength) == 0;
}

bool PersistencyService::ReadStagingPendingMarker_(bool &hadPrevious,
                                                   char *previousProjectName) {
  if (previousProjectName == nullptr)
    return false;
  auto marker =
      FileSystem::GetInstance()->Open(STAGING_TRANSACTION_PENDING_FILE, "r");
  if (!marker)
    return false;

  char stored[sizeof("PENDING:1:") + MAX_PROJECT_NAME_LENGTH]{};
  const int length = marker->Read(stored, sizeof(stored) - 1U);
  constexpr size_t prefixLength = sizeof("PENDING:1:") - 1U;
  if (length < static_cast<int>(prefixLength) || marker->Error() != 0)
    return false;
  stored[length] = '\0';
  if (std::strncmp(stored, "PENDING:", 8U) != 0 ||
      (stored[8] != '0' && stored[8] != '1') || stored[9] != ':') {
    return false;
  }

  const char *previous = stored + prefixLength;
  if (previous[0] != '\0' &&
      !IsSafeProjectName_(previous,
                          std::strcmp(previous, UNNAMED_PROJECT_NAME) == 0)) {
    return false;
  }
  hadPrevious = stored[8] == '1';
  std::strcpy(previousProjectName, previous);
  return true;
}
