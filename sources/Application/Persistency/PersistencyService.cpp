/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "PersistencyService.h"
#include "../Instruments/SamplePool.h"
#include "Foundation/Services/ServiceRegistry.h"

#include "Foundation/Types/Types.h"
#include "InstrumentExportTransaction.h"
#include "InstrumentFileValidator.h"
#include "Persistent.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include <cstdio>
#include <cstring>

#define PROJECT_STATE_FILE "/.current"
#define PROJECT_STATE_TEMP_FILE "/.current.tmp"
#define PROJECT_STATE_BACKUP_FILE "/.current.bak"
#define PROJECT_STATE_BACKUP_TEMP_FILE "/.current.bak.tmp"
#define LOAD_ROLLBACK_FILE PROJECTS_DIR "/.load-rollback.dat"
#define STAGING_PROJECT_PATH PROJECTS_DIR "/" UNNAMED_PROJECT_NAME
#define STAGING_BACKUP_PATH PROJECTS_DIR "/" STAGING_BACKUP_PROJECT_NAME
#define MAX_DELETE_DEPTH 3

namespace {

template <size_t Capacity>
bool BuildSaveAsTransactionName(char (&destination)[Capacity],
                                const char *prefix, const char *projectName) {
  const size_t prefixLength = std::strlen(prefix);
  const size_t projectLength = std::strlen(projectName);
  if (prefixLength + projectLength + 1U > Capacity)
    return false;
  std::memcpy(destination, prefix, prefixLength);
  std::memcpy(destination + prefixLength, projectName, projectLength + 1U);
  return true;
}

template <size_t Capacity>
bool BuildProjectPath(char (&destination)[Capacity], const char *projectName) {
  const int length = std::snprintf(destination, Capacity, "%s/%s",
                                   PROJECTS_DIR, projectName);
  return length > 0 && static_cast<size_t>(length) < Capacity;
}

template <size_t Capacity>
bool BuildProjectFilePath(char (&destination)[Capacity],
                          const char *projectName, const char *filename) {
  const int length = std::snprintf(destination, Capacity, "%s/%s/%s",
                                   PROJECTS_DIR, projectName, filename);
  return length > 0 && static_cast<size_t>(length) < Capacity;
}

const char *SaveAsTransactionTarget(const char *name) {
  const size_t stagePrefixLength = std::strlen(SAVE_AS_STAGE_PREFIX);
  if (std::strncmp(name, SAVE_AS_STAGE_PREFIX, stagePrefixLength) == 0)
    return name + stagePrefixLength;
  const size_t backupPrefixLength = std::strlen(SAVE_AS_BACKUP_PREFIX);
  if (std::strncmp(name, SAVE_AS_BACKUP_PREFIX, backupPrefixLength) == 0)
    return name + backupPrefixLength;
  return nullptr;
}

constexpr const char *STAGING_COMMIT_CONTENTS = "COMMITTED";
constexpr const char *STAGING_PURGE_CONTENTS = "PURGE";

bool IsSafeInstrumentFilename(const char *name) {
  if (name == nullptr)
    return false;
  const size_t length = std::strlen(name);
  return length > std::strlen(INSTRUMENT_FILE_EXTENSION) &&
         length <= MAX_INSTRUMENT_FILENAME_LENGTH &&
         std::strchr(name, '/') == nullptr &&
         std::strchr(name, '\\') == nullptr &&
         strcasecmp(name + length - std::strlen(INSTRUMENT_FILE_EXTENSION),
                    INSTRUMENT_FILE_EXTENSION) == 0;
}

bool ReadInstrumentEnvelope(const char *name, InstrumentType &type,
                            char *version, size_t versionCapacity) {
  type = IT_NONE;
  if (version != nullptr && versionCapacity != 0U)
    version[0] = '\0';
  if (!IsSafeInstrumentFilename(name))
    return false;

  PersistencyDocument doc;
  if (!doc.Load(name) || !doc.FirstChild() ||
      std::strcmp(doc.ElemName(), "INSTRUMENT") != 0) {
    return false;
  }

  bool hasType = false;
  bool hasVersion = false;
  bool attribute = doc.NextAttribute();
  while (attribute) {
    if (strcasecmp(doc.attrname_, "TYPE") == 0) {
      if (hasType)
        return false;
      hasType = true;
      for (int index = IT_NONE; index < IT_LAST; ++index) {
        if (strcasecmp(doc.attrval_, InstrumentTypeNames[index]) == 0) {
          type = static_cast<InstrumentType>(index);
          break;
        }
      }
    } else if (strcasecmp(doc.attrname_, "VERSION") == 0) {
      if (hasVersion)
        return false;
      hasVersion = true;
      if (version != nullptr && versionCapacity != 0U) {
        const size_t length = std::strlen(doc.attrval_);
        if (length >= versionCapacity)
          return false;
        std::memcpy(version, doc.attrval_, length + 1U);
      }
    }
    attribute = doc.NextAttribute();
  }
  return !doc.HadError() && hasType && type > IT_NONE && type < IT_LAST;
}

} // namespace

PersistencyService::PersistencyService()
    : Service(FourCC::ServicePersistency) {};

bool PersistencyService::IsInternalProjectName(const char *projectName) {
  if (projectName == nullptr)
    return false;
  return std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0 ||
         std::strcmp(projectName, STAGING_BACKUP_PROJECT_NAME) == 0 ||
         std::strncmp(projectName, SAVE_AS_STAGE_PREFIX,
                      std::strlen(SAVE_AS_STAGE_PREFIX)) == 0 ||
         std::strncmp(projectName, SAVE_AS_BACKUP_PREFIX,
                      std::strlen(SAVE_AS_BACKUP_PREFIX)) == 0;
}

bool PersistencyService::IsSafeProjectName_(const char *projectName,
                                            bool allowStaging) {
  if (projectName == nullptr)
    return false;

  size_t length = 0;
  while (projectName[length] != '\0' && length <= MAX_PROJECT_NAME_LENGTH) {
    if (projectName[length] == '/' || projectName[length] == '\\')
      return false;
    ++length;
  }

  if (length == 0 || length > MAX_PROJECT_NAME_LENGTH ||
      std::strcmp(projectName, ".") == 0 ||
      std::strcmp(projectName, "..") == 0) {
    return false;
  }
  if (IsInternalProjectName(projectName))
    return allowStaging &&
           std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0;
  return true;
}

bool PersistencyService::IsValidProjectName(const char *projectName) {
  return IsSafeProjectName_(projectName, false);
}

PersistencyResult PersistencyService::CreateProject() {
  Trace::Log("APPLICATION", "create new project");
  if (CreateProjectDirs_(UNNAMED_PROJECT_NAME) != PERSIST_SAVED)
    return PERSIST_ERROR;
  return SaveProjectData(UNNAMED_PROJECT_NAME, false, true);
};

bool PersistencyService::PurgeUnnamedProject() {
  FileSystem *fs = FileSystem::GetInstance();
  if (fs->exists(STAGING_TRANSACTION_PURGE_FILE)) {
    if (!HasStagingTransactionMarker_(STAGING_TRANSACTION_PURGE_FILE,
                                      STAGING_PURGE_CONTENTS)) {
      Trace::Error("PERSISTENCYSERVICE: Invalid untitled purge marker");
      return false;
    }
  } else if (!WriteStagingTransactionMarker_(
                 STAGING_TRANSACTION_PURGE_FILE,
                 STAGING_TRANSACTION_PURGE_TEMP_FILE,
                 STAGING_PURGE_CONTENTS)) {
    Trace::Error("PERSISTENCYSERVICE: Could not persist untitled purge");
    return false;
  }
  return CompleteStagingProjectPurge_();
};

bool PersistencyService::DeleteProject(const char *projectName) {
  return DeleteProject_(projectName, false);
}

bool PersistencyService::DeleteProject_(const char *projectName,
                                        bool allowStaging) {
  const bool internalTransaction =
      allowStaging && IsInternalProjectName(projectName);
  if (!internalTransaction &&
      !IsSafeProjectName_(projectName, allowStaging)) {
    Trace::Error("PERSISTENCYSERVICE: Unsafe project name rejected");
    return false;
  }

  auto fs = FileSystem::GetInstance();

  Trace::Debug("PERSISTENCYSERVICE", "Deleting project: %s", projectName);

  if (!fs->chdir(PROJECTS_DIR)) {
    Trace::Error("PERSISTENCYSERVICE: Could not change to projects dir");
    return false;
  }

  if (!fs->chdir(projectName)) {
    Trace::Error("PERSISTENCYSERVICE: Could not change to project dir");
    return false;
  }

  if (!DeleteDirectoryContents_(0)) {
    Trace::Error("PERSISTENCYSERVICE: Could not delete project contents");
    // attempt to recover to /projects for caller consistency
    fs->chdir("..");
    return false;
  }

  if (!fs->chdir("..")) { // up to projects dir
    Trace::Error("PERSISTENCYSERVICE: Could not change back to projects dir");
    return false;
  }

  if (!fs->DeleteDir(projectName)) {
    Trace::Error("PERSISTENCYSERVICE: Could not delete the project dir");
    return false;
  }

  return true;
}

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
      pendingExists && ReadStagingPendingMarker_(pendingHadPrevious,
                                                 previousProjectName);
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
          PROJECT_STATE_FILE, PROJECT_STATE_TEMP_FILE,
          PROJECT_STATE_BACKUP_FILE, PROJECT_STATE_BACKUP_TEMP_FILE,
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
            Trace::Error(
                "PERSISTENCYSERVICE: Save As backup cleanup deferred");
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
  return RecoverStagingProjectReplacement_() &&
         RecoverSaveAsTransactions_();
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
  if (!WriteStagingTransactionMarker_(
          STAGING_TRANSACTION_PENDING_FILE,
          STAGING_TRANSACTION_PENDING_TEMP_FILE, pending)) {
    Trace::Error("PERSISTENCYSERVICE: Could not persist untitled pending state");
    return false;
  }

  if (!stagingExists)
    return true;

  if (!fs->MoveFile(STAGING_PROJECT_PATH, STAGING_BACKUP_PATH)) {
    Trace::Error("PERSISTENCYSERVICE: Could not stage untitled directory");
    if (!ClearStagingTransactionMarkers_())
      Trace::Error("PERSISTENCYSERVICE: Could not clear untitled pending state");
    return false;
  }
  hadPrevious = true;
  return true;
}

bool PersistencyService::CommitStagingProjectReplacement_(bool hadPrevious) {
  FileSystem *fs = FileSystem::GetInstance();
  const bool commitExists = fs->exists(STAGING_TRANSACTION_COMMIT_FILE);
  const bool alreadyCommitted =
      commitExists && HasStagingTransactionMarker_(
                          STAGING_TRANSACTION_COMMIT_FILE,
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
      !WriteStagingTransactionMarker_(
          STAGING_TRANSACTION_COMMIT_FILE,
          STAGING_TRANSACTION_COMMIT_TEMP_FILE, STAGING_COMMIT_CONTENTS)) {
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
    Trace::Error(
        "PERSISTENCYSERVICE: Unexpected committed empty-stage backup");
    return false;
  }

  if (previousProjectName[0] == '\0') {
    constexpr const char *stateFiles[] = {
        PROJECT_STATE_FILE, PROJECT_STATE_TEMP_FILE,
        PROJECT_STATE_BACKUP_FILE, PROJECT_STATE_BACKUP_TEMP_FILE,
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
    Trace::Error("PERSISTENCYSERVICE: Refusing to roll back committed untitled");
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
      PROJECT_STATE_FILE, PROJECT_STATE_TEMP_FILE,
      PROJECT_STATE_BACKUP_FILE, PROJECT_STATE_BACKUP_TEMP_FILE,
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

bool PersistencyService::WriteStagingTransactionMarker_(
    const char *path, const char *tempPath, const char *contents) {
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

bool PersistencyService::HasStagingTransactionMarker_(
    const char *path, const char *contents) {
  FileSystem *fs = FileSystem::GetInstance();
  auto marker = fs->Open(path, "r");
  if (!marker)
    return false;
  char stored[sizeof("PENDING:1:") + MAX_PROJECT_NAME_LENGTH]{};
  const int length = marker->Read(stored, sizeof(stored) - 1U);
  const size_t expectedLength = std::strlen(contents);
  return length == static_cast<int>(expectedLength) &&
         marker->Error() == 0 &&
         std::memcmp(stored, contents, expectedLength) == 0;
}

bool PersistencyService::ReadStagingPendingMarker_(
    bool &hadPrevious, char *previousProjectName) {
  if (previousProjectName == nullptr)
    return false;
  auto marker = FileSystem::GetInstance()->Open(
      STAGING_TRANSACTION_PENDING_FILE, "r");
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
      !IsSafeProjectName_(
          previous, std::strcmp(previous, UNNAMED_PROJECT_NAME) == 0)) {
    return false;
  }
  hadPrevious = stored[8] == '1';
  std::strcpy(previousProjectName, previous);
  return true;
}

bool PersistencyService::DeleteDirectoryContents_(uint8_t depth) {
  auto fs = FileSystem::GetInstance();
  if (depth > MAX_DELETE_DEPTH) {
    Trace::Error("PERSISTENCYSERVICE: delete depth exceeded");
    return false;
  }

  while (true) {
    fileIndexes_.clear();
    fs->list(&fileIndexes_, "", false, true);

    bool foundEntry = false;
    bool deletedEntry = false;
    for (size_t i = 0; i < fileIndexes_.size(); ++i) {
      fs->getFileName(fileIndexes_[i], deleteNameBuffer_,
                      sizeof(deleteNameBuffer_));

      if ((strcmp(deleteNameBuffer_, ".") == 0) ||
          (strcmp(deleteNameBuffer_, "..") == 0)) {
        continue;
      }

      foundEntry = true;

      const PicoFileType type = fs->getFileType(fileIndexes_[i]);
      if (type == PFT_FILE) {
        if (!fs->DeleteFile(deleteNameBuffer_)) {
          Trace::Error("PERSISTENCYSERVICE: Could not delete file: %s",
                       deleteNameBuffer_);
          return false;
        }
      } else if (type == PFT_DIR) {
        if (!DeleteDirectoryTree_(deleteNameBuffer_, depth + 1)) {
          return false;
        }
      } else {
        Trace::Error("PERSISTENCYSERVICE: Unknown file type for %s",
                     deleteNameBuffer_);
        return false;
      }

      deletedEntry = true;
      break;
    }

    if (!foundEntry) {
      return true;
    }

    if (!deletedEntry) {
      Trace::Error("PERSISTENCYSERVICE: Unable to delete all entries");
      return false;
    }
  }
}

bool PersistencyService::DeleteDirectoryTree_(const char *dirname,
                                              uint8_t depth) {
  auto fs = FileSystem::GetInstance();
  char dirnameCopy[PFILENAME_SIZE];
  strncpy(dirnameCopy, dirname, sizeof(dirnameCopy));
  dirnameCopy[sizeof(dirnameCopy) - 1] = '\0';

  if (depth > MAX_DELETE_DEPTH) {
    Trace::Error("PERSISTENCYSERVICE: delete depth exceeded");
    return false;
  }

  if (!fs->chdir(dirnameCopy)) {
    Trace::Error("PERSISTENCYSERVICE: Could not chdir into dir: %s",
                 dirnameCopy);
    return false;
  }

  bool success = DeleteDirectoryContents_(depth);
  if (!fs->chdir("..")) {
    Trace::Error("PERSISTENCYSERVICE: Could not return to parent dir");
    return false;
  }
  if (!success) {
    return false;
  }

  if (!fs->DeleteDir(dirnameCopy)) {
    Trace::Error("PERSISTENCYSERVICE: Could not delete dir: %s", dirnameCopy);
    return false;
  }

  return true;
}

PersistencyResult
PersistencyService::CreateProjectDirs_(const char *projectName) {
  auto fs = FileSystem::GetInstance();

  // create samples sub dir as well as project dir containing it
  etl::vector<const char *, 3> segments = {PROJECTS_DIR, projectName,
                                           PROJECT_SAMPLES_DIR};
  CreatePath(pathBufferA, segments);

  auto result = fs->makeDir(pathBufferA.c_str(), true);
  Trace::Log("PERSISTENCYSERVICE", "created samples subdir: %s [%b]",
             pathBufferA.c_str(), result);

  return result ? PersistencyResult::PERSIST_SAVED
                : PersistencyResult::PERSIST_ERROR;
}

PersistencyResult PersistencyService::Save(const char *projectName,
                                           const char *oldProjectName,
                                           bool saveAs) {
  return Save_(projectName, oldProjectName, saveAs, false);
}

PersistencyResult PersistencyService::Save_(const char *projectName,
                                            const char *oldProjectName,
                                            bool saveAs,
                                            bool allowStaging) {
  const bool plainStagingSave =
      allowStaging && !saveAs && projectName != nullptr &&
      std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0;
  if (!plainStagingSave && !IsValidProjectName(projectName)) {
    Trace::Error("PERSISTENCYSERVICE: Unsafe save target rejected");
    return PERSIST_ERROR;
  }
  if (saveAs && !IsSafeProjectName_(oldProjectName, true)) {
    Trace::Error("PERSISTENCYSERVICE: Unsafe Save As source rejected");
    return PERSIST_ERROR;
  }

  if (saveAs)
    return SaveAsProject_(projectName, oldProjectName);
  return SaveProjectData(projectName, false, plainStagingSave);
};

PersistencyResult
PersistencyService::SaveAsProject_(const char *projectName,
                                   const char *oldProjectName) {
  if (!RecoverSaveAsTransactions_())
    return PERSIST_ERROR;

  char stageName[sizeof(SAVE_AS_STAGE_PREFIX) + MAX_PROJECT_NAME_LENGTH]{};
  char backupName[sizeof(SAVE_AS_BACKUP_PREFIX) + MAX_PROJECT_NAME_LENGTH]{};
  char targetPath[sizeof(PROJECTS_DIR) + MAX_PROJECT_NAME_LENGTH + 1U]{};
  char stagePath[sizeof(PROJECTS_DIR) + sizeof(stageName) + 1U]{};
  char backupPath[sizeof(PROJECTS_DIR) + sizeof(backupName) + 1U]{};
  if (!BuildSaveAsTransactionName(stageName, SAVE_AS_STAGE_PREFIX,
                                  projectName) ||
      !BuildSaveAsTransactionName(backupName, SAVE_AS_BACKUP_PREFIX,
                                  projectName) ||
      !BuildProjectPath(targetPath, projectName) ||
      !BuildProjectPath(stagePath, stageName) ||
      !BuildProjectPath(backupPath, backupName)) {
    return PERSIST_ERROR;
  }

  FileSystem *fs = FileSystem::GetInstance();
  if (fs->exists(stagePath) || fs->exists(backupPath)) {
    Trace::Error("PERSISTENCYSERVICE: Save As journal is still busy");
    return PERSIST_ERROR;
  }
  if (CreateProjectDirs_(stageName) != PERSIST_SAVED) {
    if (fs->exists(stagePath) && !DeleteProject_(stageName, true))
      Trace::Error("PERSISTENCYSERVICE: Could not clean partial Save As stage");
    return PERSIST_ERROR;
  }

  auto discardStage = [&]() {
    if (fs->exists(stagePath) && !DeleteProject_(stageName, true))
      Trace::Error("PERSISTENCYSERVICE: Could not clean failed Save As stage");
  };
  if (!CopyProjectSamples_(oldProjectName, stageName)) {
    Trace::Error("PERSISTENCYSERVICE: Failed to copy Save As samples");
    discardStage();
    return PERSIST_ERROR;
  }

  etl::vector<const char *, 3> modelSegments = {
      PROJECTS_DIR, stageName, PROJECT_DATA_FILE};
  CreatePath(pathBufferA, modelSegments);
  if (SaveProjectFile_(pathBufferA.c_str()) != PERSIST_SAVED ||
      ValidateProjectFile_(pathBufferA.c_str()) != PERSIST_LOADED) {
    Trace::Error("PERSISTENCYSERVICE: Save As staging validation failed");
    discardStage();
    return PERSIST_ERROR;
  }

  const bool targetExisted = fs->exists(targetPath);
  if (targetExisted && !fs->MoveFile(targetPath, backupPath)) {
    Trace::Error("PERSISTENCYSERVICE: Could not stage Save As target");
    discardStage();
    return PERSIST_ERROR;
  }
  if (!fs->MoveFile(stagePath, targetPath)) {
    Trace::Error("PERSISTENCYSERVICE: Could not install Save As stage");
    discardStage();
    if (targetExisted && !fs->MoveFile(backupPath, targetPath))
      Trace::Error("PERSISTENCYSERVICE: Could not restore Save As target");
    return PERSIST_ERROR;
  }

  if (targetExisted && !DeleteProject_(backupName, true)) {
    // Keep the fully validated target. Startup recovery will retry removal;
    // never destroy new data to roll back from a cleanup-only failure.
    Trace::Error("PERSISTENCYSERVICE: Save As backup cleanup deferred");
    return PERSIST_ERROR;
  }
  return PERSIST_SAVED;
}

bool PersistencyService::CopyProjectSamples_(const char *sourceProject,
                                             const char *targetProject) {
  FileSystem *fs = FileSystem::GetInstance();
  if (!fs->chdir(PROJECTS_DIR) || !fs->chdir(sourceProject) ||
      !fs->chdir(PROJECT_SAMPLES_DIR)) {
    Trace::Error("PERSISTENCYSERVICE: Could not enter Save As sample source");
    return false;
  }

  Trace::Debug("get list of samples to copy from old project: %s",
               sourceProject);

  // The legacy void listing API allowed an I/O-short directory scan to
  // masquerade as an empty sample set. Transactional Save As uses the checked
  // adapter contract and aborts before touching the target on any scan error.
  fileIndexes_.clear();
  if (!fs->listChecked(&fileIndexes_, ".wav", false)) {
    Trace::Error("PERSISTENCYSERVICE: Sample directory scan failed");
    return false;
  }
  if (fileIndexes_.full()) {
    Trace::Error("PERSISTENCYSERVICE: Sample listing may be truncated");
    return false;
  }
  char filenameBuffer[PFILENAME_SIZE]{};
  for (size_t i = 0; i < fileIndexes_.size(); i++) {
    filenameBuffer[0] = '\0';
    fs->getFileName(fileIndexes_[i], filenameBuffer, sizeof(filenameBuffer));

    if (std::strcmp(filenameBuffer, ".") == 0 ||
        std::strcmp(filenameBuffer, "..") == 0) {
      continue;
    }
    if (filenameBuffer[0] == '\0' ||
        fs->getFileType(fileIndexes_[i]) != PFT_FILE) {
      Trace::Error("PERSISTENCYSERVICE: Invalid sample list entry");
      return false;
    }

    const size_t sourceLength = std::strlen(PROJECTS_DIR) + 1U +
                                std::strlen(sourceProject) + 1U +
                                std::strlen(PROJECT_SAMPLES_DIR) + 1U +
                                std::strlen(filenameBuffer);
    const size_t targetLength = std::strlen(PROJECTS_DIR) + 1U +
                                std::strlen(targetProject) + 1U +
                                std::strlen(PROJECT_SAMPLES_DIR) + 1U +
                                std::strlen(filenameBuffer);
    if (sourceLength > pathBufferA.capacity() ||
        targetLength > pathBufferB.capacity()) {
      Trace::Error("PERSISTENCYSERVICE: Sample path exceeds fixed capacity");
      return false;
    }

    etl::vector<const char *, 4> filePathSegments = {
        PROJECTS_DIR, sourceProject, PROJECT_SAMPLES_DIR, filenameBuffer};
    CreatePath(pathBufferA, filePathSegments);
    filePathSegments = {PROJECTS_DIR, targetProject, PROJECT_SAMPLES_DIR,
                        filenameBuffer};
    CreatePath(pathBufferB, filePathSegments);

    if (!fs->CopyFile(pathBufferA.c_str(), pathBufferB.c_str())) {
      Trace::Error("PERSISTENCYSERVICE: Could not copy sample: %s",
                   filenameBuffer);
      return false;
    }
  }
  return true;
}

PersistencyResult
PersistencyService::AutoSaveProjectData(const char *projectName) {
  return AutoSaveProjectData_(projectName, false);
};

PersistencyResult
PersistencyService::AutoSaveProjectData_(const char *projectName,
                                         bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return PERSIST_ERROR;
  return SaveProjectData(projectName, true, allowStaging);
};

PersistencyResult PersistencyService::SaveProjectData(const char *projectName,
                                                      bool autosave,
                                                      bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return PERSIST_ERROR;

  const PersistencyResult result = SaveProjectFileAtomically_(
      projectName, autosave ? AUTO_SAVE_FILENAME : PROJECT_DATA_FILE,
      autosave ? AUTO_SAVE_TEMP_FILENAME : PROJECT_DATA_TEMP_FILE,
      autosave ? AUTO_SAVE_BACKUP_FILENAME : PROJECT_DATA_BACKUP_FILE,
      allowStaging);
  if (result != PERSIST_SAVED || autosave)
    return result;

  // A stale autosave would shadow the newly synced base on next boot.
  // Deletion failure therefore makes the explicit save incomplete.
  if (!ClearAutosave_(projectName, allowStaging)) {
    Trace::Error("PERSISTENCYSERVICE: Explicit save remains shadowed");
    return PERSIST_ERROR;
  }
  return PERSIST_SAVED;
};

PersistencyResult PersistencyService::SaveProjectFile_(const char *path) {

  auto fs = FileSystem::GetInstance();
  auto fp = fs->Open(path, "w");
  if (!fp) {
    Trace::Error("PERSISTENCYSERVICE: Could not open file for writing: %s",
                 path);
    return PERSIST_ERROR;
  }
  Trace::Log("PERSISTENCYSERVICE", "Opened Proj File: %s", path);
  {
    // The printer must be finalized before explicitly closing the file. The
    // explicit close is important on VFS/FatFS because delayed media errors
    // can otherwise be lost by FileHandle's non-reporting destructor.
    tinyxml2::XMLPrinter printer(fp.get());

    printer.OpenElement("PICOTRACKER");

    // Loop on all registered persistable subservices
    for (auto *sub : SubServices()) {
      auto *currentItem = static_cast<Persistent *>(static_cast<void *>(sub));
      currentItem->Save(&printer);
    }

    printer.CloseElement();
  }

  const bool synced = fp->Sync() && fp->Error() == 0;
  I_File *rawFile = AcquireLegacyFileHandle_DO_NOT_USE(std::move(fp));
  const bool closed = CloseFile_DO_NOT_USE(rawFile);
  if (!synced || !closed) {
    Trace::Error("PERSISTENCYSERVICE: Failed to flush project file: %s", path);
    return PERSIST_ERROR;
  }
  return PERSIST_SAVED;
}

PersistencyResult PersistencyService::SaveProjectFileAtomically_(
    const char *projectName, const char *filename, const char *tempFilename,
    const char *backupFilename, bool allowStaging) {
  if (!RecoverProjectFileJournal_(projectName, filename, tempFilename,
                                  backupFilename, allowStaging)) {
    return PERSIST_ERROR;
  }

  char destinationPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char tempPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backupPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(destinationPath, projectName, filename) ||
      !BuildProjectFilePath(tempPath, projectName, tempFilename) ||
      !BuildProjectFilePath(backupPath, projectName, backupFilename)) {
    return PERSIST_ERROR;
  }

  FileSystem *fs = FileSystem::GetInstance();
  if (fs->exists(tempPath) && !fs->DeleteFile(tempPath))
    return PERSIST_ERROR;
  if (SaveProjectFile_(tempPath) != PERSIST_SAVED ||
      ValidateProjectFile_(tempPath) != PERSIST_LOADED) {
    fs->DeleteFile(tempPath);
    return PERSIST_ERROR;
  }

  // POSIX/WASM can atomically replace the destination in one rename.
  if (fs->MoveFile(tempPath, destinationPath)) {
    if (fs->exists(backupPath) && !fs->DeleteFile(backupPath))
      Trace::Error("PERSISTENCYSERVICE: Project backup cleanup deferred");
    return PERSIST_SAVED;
  }
  if (!fs->exists(destinationPath)) {
    fs->DeleteFile(tempPath);
    return PERSIST_ERROR;
  }

  // SdFat refuses rename-over-existing. Keep the old file as a journal until
  // the synced replacement is installed.
  if (fs->exists(backupPath) && !fs->DeleteFile(backupPath)) {
    fs->DeleteFile(tempPath);
    return PERSIST_ERROR;
  }
  if (!fs->MoveFile(destinationPath, backupPath)) {
    fs->DeleteFile(tempPath);
    return PERSIST_ERROR;
  }
  if (!fs->MoveFile(tempPath, destinationPath)) {
    if (!fs->MoveFile(backupPath, destinationPath))
      Trace::Error("PERSISTENCYSERVICE: Could not restore project file");
    fs->DeleteFile(tempPath);
    return PERSIST_ERROR;
  }
  if (!fs->DeleteFile(backupPath))
    Trace::Error("PERSISTENCYSERVICE: Project backup cleanup deferred");
  return PERSIST_SAVED;
}

bool PersistencyService::RecoverProjectFileJournal_(
    const char *projectName, const char *filename, const char *tempFilename,
    const char *backupFilename, bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return false;

  char destinationPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char tempPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backupPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(destinationPath, projectName, filename) ||
      !BuildProjectFilePath(tempPath, projectName, tempFilename) ||
      !BuildProjectFilePath(backupPath, projectName, backupFilename)) {
    return false;
  }

  FileSystem *fs = FileSystem::GetInstance();
  const bool destinationValid =
      fs->exists(destinationPath) &&
      ValidateProjectFile_(destinationPath) == PERSIST_LOADED;
  if (destinationValid) {
    // Structural XML validity is not the semantic commit boundary. Keep the
    // previous generation until TrackerApplicationSession has restored the
    // destination without MarkError; otherwise a range check introduced by a
    // firmware update (or a payload bit flip that preserves XML) could make
    // preflight delete the only loadable base. A stale temp is uncommitted and
    // safe to discard, while the backup is finalized explicitly by Session.
    if (fs->exists(tempPath) && !fs->DeleteFile(tempPath))
      Trace::Error("PERSISTENCYSERVICE: Stale project temp remains");
    return true;
  }

  const bool backupValid =
      fs->exists(backupPath) &&
      ValidateProjectFile_(backupPath) == PERSIST_LOADED;
  const bool tempValid = fs->exists(tempPath) &&
                         ValidateProjectFile_(tempPath) == PERSIST_LOADED;
  const char *recoveryPath = backupValid ? backupPath : tempValid ? tempPath
                                                               : nullptr;
  if (recoveryPath == nullptr) {
    // Selection code only chooses a syntactically valid destination. A corrupt
    // autosave therefore cannot shadow the base, and a corrupt base fails load
    // without discarding any potentially useful bytes.
    return true;
  }

  if (fs->exists(destinationPath) && !fs->DeleteFile(destinationPath)) {
    Trace::Error("PERSISTENCYSERVICE: Could not remove corrupt project file");
    return false;
  }
  if (!fs->MoveFile(recoveryPath, destinationPath)) {
    Trace::Error("PERSISTENCYSERVICE: Could not recover project journal");
    return false;
  }
  const char *otherPath = recoveryPath == backupPath ? tempPath : backupPath;
  if (fs->exists(otherPath) && !fs->DeleteFile(otherPath))
    Trace::Error("PERSISTENCYSERVICE: Stale project journal remains");
  return true;
}

bool PersistencyService::RecoverAutosaveJournal_(const char *projectName,
                                                 bool allowStaging) {
  return RecoverProjectFileJournal_(
      projectName, AUTO_SAVE_FILENAME, AUTO_SAVE_TEMP_FILENAME,
      AUTO_SAVE_BACKUP_FILENAME, allowStaging);
}

bool PersistencyService::RecoverBaseJournal_(const char *projectName,
                                             bool allowStaging) {
  return RecoverProjectFileJournal_(
      projectName, PROJECT_DATA_FILE, PROJECT_DATA_TEMP_FILE,
      PROJECT_DATA_BACKUP_FILE, allowStaging);
}

PersistencyResult PersistencyService::SaveLoadRollback() {
  return SaveProjectFile_(LOAD_ROLLBACK_FILE);
}

PersistencyResult PersistencyService::RestoreLoadRollback() {
  return LoadProjectFile_(LOAD_ROLLBACK_FILE);
}

void PersistencyService::ClearLoadRollback() {
  FileSystem::GetInstance()->DeleteFile(LOAD_ROLLBACK_FILE);
}

// return true if existing proj with the given name already exists
bool PersistencyService::Exists(const char *projectName) {
  return Exists_(projectName, false);
}

bool PersistencyService::Exists_(const char *projectName, bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return false;

  etl::string<128> projectFilePath(PROJECTS_DIR);
  projectFilePath.append("/");
  projectFilePath.append(projectName);

  auto fs = FileSystem::GetInstance();
  return fs->exists(projectFilePath.c_str());
}

PersistencyResult PersistencyService::Validate(const char *projectName) {
  return Validate_(projectName, false);
}

PersistencyResult PersistencyService::Validate_(const char *projectName,
                                                bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return PERSIST_LOAD_FAILED;

  if (!RecoverBaseJournal_(projectName, allowStaging) ||
      !RecoverAutosaveJournal_(projectName, allowStaging))
    return PERSIST_LOAD_FAILED;
  char autosavePath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char basePath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(autosavePath, projectName, AUTO_SAVE_FILENAME) ||
      !BuildProjectFilePath(basePath, projectName, PROJECT_DATA_FILE)) {
    return PERSIST_LOAD_FAILED;
  }
  FileSystem *fs = FileSystem::GetInstance();
  if (fs->exists(autosavePath) &&
      ValidateProjectFile_(autosavePath) == PERSIST_LOADED) {
    return PERSIST_LOADED;
  }
  return ValidateProjectFile_(basePath);
}

PersistencyResult PersistencyService::Load(const char *projectName) {
  return Load_(projectName, false);
}

PersistencyResult PersistencyService::Load_(const char *projectName,
                                            bool allowStaging,
                                            bool *usedAutosave) {
  if (usedAutosave != nullptr)
    *usedAutosave = false;
  if (!IsSafeProjectName_(projectName, allowStaging))
    return PERSIST_LOAD_FAILED;

  if (!RecoverBaseJournal_(projectName, allowStaging) ||
      !RecoverAutosaveJournal_(projectName, allowStaging))
    return PERSIST_LOAD_FAILED;
  char autosavePath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(autosavePath, projectName, AUTO_SAVE_FILENAME)) {
    return PERSIST_LOAD_FAILED;
  }
  FileSystem *fs = FileSystem::GetInstance();
  if (fs->exists(autosavePath) &&
      ValidateProjectFile_(autosavePath) == PERSIST_LOADED) {
    if (usedAutosave != nullptr)
      *usedAutosave = true;
    Trace::Log("PERSISTENCYSERVICE", "using autosave: true");
    // A semantic restore failure may already have mutated several Persistent
    // objects. The Session must perform a complete model/pool/table reset
    // before explicitly calling LoadBase_; never layer base restore here.
    return LoadProjectFile_(autosavePath);
  }
  Trace::Log("PERSISTENCYSERVICE", "using autosave: false");
  return LoadBase_(projectName, allowStaging);
}

PersistencyResult PersistencyService::LoadBase_(const char *projectName,
                                                bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging) ||
      !RecoverBaseJournal_(projectName, allowStaging)) {
    return PERSIST_LOAD_FAILED;
  }
  char basePath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(basePath, projectName, PROJECT_DATA_FILE))
    return PERSIST_LOAD_FAILED;
  return LoadProjectFile_(basePath);
}

PersistencyResult PersistencyService::LoadProjectJournalBackup_(
    const char *projectName, bool autosave, bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return PERSIST_LOAD_FAILED;
  char backupPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(
          backupPath, projectName,
          autosave ? AUTO_SAVE_BACKUP_FILENAME : PROJECT_DATA_BACKUP_FILE)) {
    return PERSIST_LOAD_FAILED;
  }
  FileSystem *fs = FileSystem::GetInstance();
  if (!fs->exists(backupPath) ||
      ValidateProjectFile_(backupPath) != PERSIST_LOADED) {
    return PERSIST_LOAD_FAILED;
  }
  return LoadProjectFile_(backupPath);
}

bool PersistencyService::PromoteProjectJournalBackup_(
    const char *projectName, bool autosave, bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return false;
  char destinationPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char tempPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backupPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(
          destinationPath, projectName,
          autosave ? AUTO_SAVE_FILENAME : PROJECT_DATA_FILE) ||
      !BuildProjectFilePath(
          tempPath, projectName,
          autosave ? AUTO_SAVE_TEMP_FILENAME : PROJECT_DATA_TEMP_FILE) ||
      !BuildProjectFilePath(
          backupPath, projectName,
          autosave ? AUTO_SAVE_BACKUP_FILENAME : PROJECT_DATA_BACKUP_FILE)) {
    return false;
  }
  FileSystem *fs = FileSystem::GetInstance();
  if (!fs->exists(backupPath) ||
      ValidateProjectFile_(backupPath) != PERSIST_LOADED)
    return false;
  if (fs->exists(tempPath) && !fs->DeleteFile(tempPath))
    return false;
  // The backup remains the only known semantic-good generation until the
  // structurally valid but rejected destination has been removed. A crash in
  // this window is recovered by RecoverProjectFileJournal_.
  if (fs->exists(destinationPath) && !fs->DeleteFile(destinationPath))
    return false;
  if (!fs->MoveFile(backupPath, destinationPath))
    return false;
  return ValidateProjectFile_(destinationPath) == PERSIST_LOADED;
}

bool PersistencyService::FinalizeProjectJournal_(const char *projectName,
                                                 bool autosave,
                                                 bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return false;
  char tempPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backupPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(
          tempPath, projectName,
          autosave ? AUTO_SAVE_TEMP_FILENAME : PROJECT_DATA_TEMP_FILE) ||
      !BuildProjectFilePath(
          backupPath, projectName,
          autosave ? AUTO_SAVE_BACKUP_FILENAME : PROJECT_DATA_BACKUP_FILE)) {
    return false;
  }
  FileSystem *fs = FileSystem::GetInstance();
  if (fs->exists(tempPath) && !fs->DeleteFile(tempPath))
    return false;
  return !fs->exists(backupPath) || fs->DeleteFile(backupPath);
}

PersistencyResult
PersistencyService::ValidateProjectFile_(const char *path) {
  PersistencyDocument doc;
  if (!doc.Load(path))
    return PERSIST_LOAD_FAILED;
  const bool elem = doc.FirstChild();
  if (!elem || std::strcmp(doc.ElemName(), "PICOTRACKER") != 0)
    return PERSIST_LOAD_FAILED;
  return doc.Finish() ? PERSIST_LOADED : PERSIST_LOAD_FAILED;
}

PersistencyResult PersistencyService::LoadProjectFile_(const char *path) {
  PersistencyDocument doc;
  if (!doc.Load(path))
    return PERSIST_LOAD_FAILED;

  bool elem = doc.FirstChild(); // advance to first child
  if (!elem || strcmp(doc.ElemName(), "PICOTRACKER")) {
    Trace::Error("could not find master node");
    return PERSIST_LOAD_FAILED;
  }

  elem = doc.FirstChild();
  while (elem) {
    for (auto *sub : SubServices()) {
      auto *currentItem = static_cast<Persistent *>(static_cast<void *>(sub));
      if (currentItem->Restore(&doc)) {
        break;
      }
    }
    elem = doc.NextSibling();
  }
  if (!doc.Finish()) {
    Trace::Error("XML or payload errors detected while loading project '%s'",
                 path);
    return PERSIST_LOAD_FAILED;
  }
  return PERSIST_LOADED;
}

PersistencyResult
PersistencyService::LoadCurrentProjectName(char *projectName) {
  if (projectName == nullptr)
    return PERSIST_LOAD_FAILED;

  FileSystem *fs = FileSystem::GetInstance();
  if (!RecoverInternalProjectTransactions_()) {
    Trace::Error("PERSISTENCYSERVICE: Project transaction recovery failed");
    return PERSIST_LOAD_FAILED;
  }
  auto selectExistingUntitled = [&]() {
    // A missing marker is normal on first boot, but it can also mean the first
    // durable marker write failed after a valid untitled project was already
    // saved. Never classify that sole recoverable payload as a fresh-create
    // request: doing so would replace it on the next NewProject transaction.
    if (Validate_(UNNAMED_PROJECT_NAME, true) != PERSIST_LOADED)
      return false;
    std::strcpy(projectName, UNNAMED_PROJECT_NAME);
    return true;
  };
  if (!fs->exists(PROJECT_STATE_FILE)) {
    // FAT replacement sequence:
    //   current(old) -> backup, then temp(new) -> current.
    // If power is lost between the renames, both backup and the fully synced
    // temp exist. Install the new candidate first and retain the backup until
    // Session completes semantic restore. If the candidate later proves bad,
    // the normal backup fallback below restores the old pointer.
    const bool tempExists = fs->exists(PROJECT_STATE_TEMP_FILE);
    const bool backupExists = fs->exists(PROJECT_STATE_BACKUP_FILE);
    const char *recoverySource = tempExists
                                     ? PROJECT_STATE_TEMP_FILE
                                     : backupExists
                                           ? PROJECT_STATE_BACKUP_FILE
                                           : nullptr;
    if (recoverySource == nullptr)
      return selectExistingUntitled() ? PERSIST_LOADED : PERSIST_LOAD_FAILED;
    bool recovered = fs->MoveFile(recoverySource, PROJECT_STATE_FILE);
    // A temp rename can itself fail because that candidate is unreadable. The
    // synced old marker is still a safe fallback and must not be ignored.
    if (!recovered && tempExists && backupExists)
      recovered = fs->MoveFile(PROJECT_STATE_BACKUP_FILE, PROJECT_STATE_FILE);
    if (!recovered) {
      Trace::Error("PERSISTENCYSERVICE: Could not recover project state");
      return PERSIST_LOAD_FAILED;
    }
  }
  auto readValidState = [&](char *destination) {
    auto current = fs->Open(PROJECT_STATE_FILE, "r");
    if (!current)
      return false;

    char stored[MAX_PROJECT_NAME_LENGTH + 2U]{};
    const int length = current->Read(stored, sizeof(stored) - 1U);
    if (length <= 0 || length > MAX_PROJECT_NAME_LENGTH ||
        current->Error() != 0) {
      return false;
    }
    stored[length] = '\0';
    if (std::strlen(stored) != static_cast<size_t>(length) ||
        !IsSafeProjectName_(stored, true) || !Exists_(stored, true) ||
        Validate_(stored, true) != PERSIST_LOADED)
      return false;
    std::strcpy(destination, stored);
    Trace::Log("APPLICATION", "read [%d] load proj name: %s", length,
               destination);
    return true;
  };

  if (readValidState(projectName)) {
    if (fs->exists(PROJECT_STATE_TEMP_FILE) &&
        !fs->DeleteFile(PROJECT_STATE_TEMP_FILE)) {
      Trace::Error("PERSISTENCYSERVICE: Could not remove stale state temp");
    }
    // Keep the previous marker until Session completes semantic restore and
    // SaveProjectState_ commits this candidate. Structural XML validation is
    // not enough to discard the last known-good project pointer.
    return PERSIST_LOADED;
  }

  // The FAT-compatible replacement path can lose power between its two
  // renames. If the new state is invalid, prefer the synced backup.
  if (fs->exists(PROJECT_STATE_BACKUP_FILE)) {
    const bool removedInvalid =
        !fs->exists(PROJECT_STATE_FILE) || fs->DeleteFile(PROJECT_STATE_FILE);
    if (removedInvalid &&
        fs->MoveFile(PROJECT_STATE_BACKUP_FILE, PROJECT_STATE_FILE) &&
        readValidState(projectName)) {
      if (fs->exists(PROJECT_STATE_TEMP_FILE) &&
          !fs->DeleteFile(PROJECT_STATE_TEMP_FILE)) {
        Trace::Error("PERSISTENCYSERVICE: Could not remove stale state temp");
      }
      return PERSIST_LOADED;
    }
  }

  if (fs->exists(PROJECT_STATE_TEMP_FILE)) {
    const bool removedInvalid =
        !fs->exists(PROJECT_STATE_FILE) || fs->DeleteFile(PROJECT_STATE_FILE);
    if (removedInvalid &&
        fs->MoveFile(PROJECT_STATE_TEMP_FILE, PROJECT_STATE_FILE) &&
        readValidState(projectName)) {
      return PERSIST_LOADED;
    }
  }

  Trace::Log("APPLICATION", "Invalid current project, loading untitled");
  if (fs->exists(PROJECT_STATE_FILE) && !fs->DeleteFile(PROJECT_STATE_FILE)) {
    Trace::Error("PERSISTENCYSERVICE: Could not delete invalid project state");
  }
  if (fs->exists(PROJECT_STATE_TEMP_FILE) &&
      !fs->DeleteFile(PROJECT_STATE_TEMP_FILE)) {
    Trace::Error("PERSISTENCYSERVICE: Could not delete invalid state temp");
  }
  // The current marker may itself be corrupt while a synced untitled base or
  // autosave journal is recoverable. Promote either before Session decides
  // whether this is the normal first-boot create path; otherwise CreateProject
  // could overwrite the only recoverable staging payload.
  if (!RecoverBaseJournal_(UNNAMED_PROJECT_NAME, true) ||
      !RecoverAutosaveJournal_(UNNAMED_PROJECT_NAME, true))
    return PERSIST_LOAD_FAILED;
  std::strcpy(projectName, UNNAMED_PROJECT_NAME);
  return PERSIST_LOADED;
}

bool PersistencyService::ReadPreviousProjectName_(char *projectName) {
  if (projectName == nullptr)
    return false;
  FileSystem *fs = FileSystem::GetInstance();
  auto backup = fs->Open(PROJECT_STATE_BACKUP_FILE, "r");
  if (!backup)
    return false;

  char stored[MAX_PROJECT_NAME_LENGTH + 2U]{};
  const int length = backup->Read(stored, sizeof(stored) - 1U);
  if (length <= 0 || length > MAX_PROJECT_NAME_LENGTH ||
      backup->Error() != 0) {
    return false;
  }
  stored[length] = '\0';
  if (std::strlen(stored) != static_cast<size_t>(length) ||
      !IsSafeProjectName_(stored, true) || !Exists_(stored, true) ||
      Validate_(stored, true) != PERSIST_LOADED) {
    return false;
  }
  std::strcpy(projectName, stored);
  return true;
}

PersistencyResult
PersistencyService::SaveProjectState(const char *projectName) {
  return SaveProjectState_(projectName, false);
}

PersistencyResult
PersistencyService::SaveProjectState_(const char *projectName,
                                      bool allowStaging,
                                      bool retainPreviousBackup,
                                      const char *previousProjectName) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return PERSIST_ERROR;

  FileSystem *fs = FileSystem::GetInstance();
  auto markerMatchesText = [&](const char *path, const char *text) {
    auto marker = fs->Open(path, "r");
    if (!marker)
      return false;
    char stored[MAX_PROJECT_NAME_LENGTH + 2U]{};
    const int expectedLength = static_cast<int>(std::strlen(text));
    const int storedLength = marker->Read(stored, sizeof(stored) - 1U);
    return storedLength == expectedLength && marker->Error() == 0 &&
           std::memcmp(stored, text,
                       static_cast<size_t>(expectedLength)) == 0;
  };

  const bool preservePrevious =
      retainPreviousBackup && previousProjectName != nullptr &&
      previousProjectName[0] != '\0';
  if (preservePrevious &&
      !IsSafeProjectName_(
          previousProjectName,
          std::strcmp(previousProjectName, UNNAMED_PROJECT_NAME) == 0)) {
    return PERSIST_ERROR;
  }

  if (fs->exists(PROJECT_STATE_BACKUP_TEMP_FILE) &&
      !fs->DeleteFile(PROJECT_STATE_BACKUP_TEMP_FILE)) {
    return PERSIST_ERROR;
  }
  if (preservePrevious) {
    if (fs->exists(PROJECT_STATE_BACKUP_FILE)) {
      if (!markerMatchesText(PROJECT_STATE_BACKUP_FILE,
                             previousProjectName)) {
        Trace::Error("PERSISTENCYSERVICE: Conflicting retained state backup");
        return PERSIST_ERROR;
      }
    } else {
      auto backup = fs->Open(PROJECT_STATE_BACKUP_TEMP_FILE, "w");
      if (!backup)
        return PERSIST_ERROR;
      const int previousLength =
          static_cast<int>(std::strlen(previousProjectName));
      const bool backupSynced =
          backup->Write(previousProjectName, 1, previousLength) ==
              previousLength &&
          backup->Sync() && backup->Error() == 0;
      I_File *rawBackup =
          AcquireLegacyFileHandle_DO_NOT_USE(std::move(backup));
      const bool backupClosed = CloseFile_DO_NOT_USE(rawBackup);
      if (!backupSynced || !backupClosed ||
          !fs->MoveFile(PROJECT_STATE_BACKUP_TEMP_FILE,
                        PROJECT_STATE_BACKUP_FILE) ||
          !markerMatchesText(PROJECT_STATE_BACKUP_FILE,
                             previousProjectName)) {
        if (fs->exists(PROJECT_STATE_BACKUP_TEMP_FILE))
          fs->DeleteFile(PROJECT_STATE_BACKUP_TEMP_FILE);
        return PERSIST_ERROR;
      }
    }
  }

  if (fs->exists(PROJECT_STATE_TEMP_FILE) &&
      !fs->DeleteFile(PROJECT_STATE_TEMP_FILE)) {
    Trace::Error("PERSISTENCYSERVICE: Could not clear project state temp");
    return PERSIST_ERROR;
  }

  auto current = fs->Open(PROJECT_STATE_TEMP_FILE, "w");
  if (!current) {
    return PERSIST_ERROR;
  }

  const int length = static_cast<int>(std::strlen(projectName));
  const bool synced = current->Write(projectName, 1, length) == length &&
                      current->Sync() && current->Error() == 0;
  I_File *rawFile = AcquireLegacyFileHandle_DO_NOT_USE(std::move(current));
  const bool closed = CloseFile_DO_NOT_USE(rawFile);
  if (!synced || !closed) {
    Trace::Error("PERSISTENCYSERVICE: Failed to sync project state temp");
    fs->DeleteFile(PROJECT_STATE_TEMP_FILE);
    return PERSIST_ERROR;
  }

  // POSIX/WASM replace an existing regular file atomically in one rename.
  if (fs->MoveFile(PROJECT_STATE_TEMP_FILE, PROJECT_STATE_FILE)) {
    if (!retainPreviousBackup && fs->exists(PROJECT_STATE_BACKUP_FILE) &&
        !fs->DeleteFile(PROJECT_STATE_BACKUP_FILE)) {
      Trace::Error("PERSISTENCYSERVICE: Stale state backup remains");
    }
    return PERSIST_SAVED;
  }

  if (!fs->exists(PROJECT_STATE_FILE)) {
    fs->DeleteFile(PROJECT_STATE_TEMP_FILE);
    return PERSIST_ERROR;
  }

  if (retainPreviousBackup) {
    // The previous marker has already been synced independently. SdFat cannot
    // rename over current, so remove only the rejected/current generation and
    // install the new one. On failure, both the retained backup and synced
    // temp remain available to phase-marker recovery.
    if (!fs->DeleteFile(PROJECT_STATE_FILE) ||
        !fs->MoveFile(PROJECT_STATE_TEMP_FILE, PROJECT_STATE_FILE)) {
      return PERSIST_ERROR;
    }
    return PERSIST_SAVED;
  }

  if (fs->exists(PROJECT_STATE_BACKUP_FILE)) {
    if (markerMatchesText(PROJECT_STATE_FILE, projectName)) {
      // Session has already semantically loaded the requested current
      // project. No FAT rename is needed; retain current until the stale
      // previous pointer is removed, so every power-loss point remains
      // bootable.
      const bool tempRemoved = fs->DeleteFile(PROJECT_STATE_TEMP_FILE);
      const bool backupRemoved =
          fs->DeleteFile(PROJECT_STATE_BACKUP_FILE);
      return tempRemoved && backupRemoved ? PERSIST_SAVED : PERSIST_ERROR;
    }
    if (markerMatchesText(PROJECT_STATE_BACKUP_FILE, projectName)) {
      // Semantic fallback is rewriting the previous project while current
      // still names the rejected candidate. The backup is the only known
      // good pointer: never delete it before installing the same desired
      // value. If power fails between delete and rename, both backup and the
      // synced temp still name the good project and boot recovery can resume.
      if (!fs->DeleteFile(PROJECT_STATE_FILE) ||
          !fs->MoveFile(PROJECT_STATE_BACKUP_FILE, PROJECT_STATE_FILE)) {
        return PERSIST_ERROR;
      }
      if (fs->exists(PROJECT_STATE_TEMP_FILE) &&
          !fs->DeleteFile(PROJECT_STATE_TEMP_FILE)) {
        return PERSIST_ERROR;
      }
      return PERSIST_SAVED;
    }

    // An unrelated backup represents a transaction generation this call
    // cannot safely supersede without a second backup slot. Fail closed and
    // preserve all pre-existing pointers.
    fs->DeleteFile(PROJECT_STATE_TEMP_FILE);
    Trace::Error("PERSISTENCYSERVICE: Conflicting project state backup");
    return PERSIST_ERROR;
  }

  // SdFat deliberately refuses rename-over-existing. Preserve the previous
  // state as a recovery journal while doing its required two-rename fallback.
  if (!fs->MoveFile(PROJECT_STATE_FILE, PROJECT_STATE_BACKUP_FILE)) {
    fs->DeleteFile(PROJECT_STATE_TEMP_FILE);
    return PERSIST_ERROR;
  }
  if (!fs->MoveFile(PROJECT_STATE_TEMP_FILE, PROJECT_STATE_FILE)) {
    Trace::Error("PERSISTENCYSERVICE: Failed to install project state temp");
    if (!fs->MoveFile(PROJECT_STATE_BACKUP_FILE, PROJECT_STATE_FILE)) {
      Trace::Error("PERSISTENCYSERVICE: Failed to restore project state");
    }
    fs->DeleteFile(PROJECT_STATE_TEMP_FILE);
    return PERSIST_ERROR;
  }
  if (!fs->DeleteFile(PROJECT_STATE_BACKUP_FILE)) {
    Trace::Error("PERSISTENCYSERVICE: Saved state but backup cleanup failed");
    return PERSIST_ERROR;
  }
  return PERSIST_SAVED;
}

void PersistencyService::CreatePath(
    etl::istring &path, const etl::ivector<const char *> &segments) {
  // concatenate path segments into a single path
  path.clear();
  // iterate over segments and concatenate using iterator
  for (auto it = segments.begin(); it != segments.end(); ++it) {
    path.append(*it);
    if (it != segments.end() - 1) {
      path.append("/");
    }
  }
}

bool PersistencyService::ClearAutosave(const char *projectName) {
  return ClearAutosave_(projectName, false);
}

bool PersistencyService::ClearAutosave_(const char *projectName,
                                        bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return false;

  FileSystem *fs = FileSystem::GetInstance();
  // Delete recovery siblings first and fail closed.  The authoritative
  // autosave must remain until no stale backup can be promoted over a newly
  // synced base after a reboot.
  constexpr const char *files[] = {AUTO_SAVE_TEMP_FILENAME,
                                   AUTO_SAVE_BACKUP_FILENAME,
                                   AUTO_SAVE_FILENAME};
  for (const char *filename : files) {
    char path[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
    if (!BuildProjectFilePath(path, projectName, filename))
      return false;
    if (fs->exists(path) && !fs->DeleteFile(path))
      return false;
  }
  return true;
}

PersistencyResult PersistencyService::ExportInstrument(
    I_Instrument *instrument, etl::string<MAX_INSTRUMENT_NAME_LENGTH> name,
    bool overwrite) {
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr || instrument == nullptr)
    return PERSIST_ERROR;

  char destination[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char temporary[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backup[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildInstrumentExportSiblingPaths(destination, temporary, backup,
                                         INSTRUMENTS_DIR, name.c_str())) {
    Trace::Error("PERSISTENCYSERVICE: Unsafe instrument export name");
    return PERSIST_ERROR;
  }

  const InstrumentExportTransactionResult result =
      ExportInstrumentFileAtomically(
          *fs, destination, temporary, backup, overwrite,
          [&](const char *path) {
            auto fp = fs->Open(path, "w");
            if (!fp) {
              Trace::Error(
                  "PERSISTENCYSERVICE: Could not open instrument temp: %s",
                  path);
              return false;
            }
            {
              tinyxml2::XMLPrinter printer(fp.get());
              instrument->Save(&printer);
            }
            const bool synced = fp->Sync() && fp->Error() == 0;
            I_File *rawFile =
                AcquireLegacyFileHandle_DO_NOT_USE(std::move(fp));
            const bool closed = CloseFile_DO_NOT_USE(rawFile);
            if (!synced || !closed) {
              Trace::Error(
                  "PERSISTENCYSERVICE: Failed to flush instrument temp: %s",
                  path);
            }
            return synced && closed;
          },
          [](const char *path) { return ValidateInstrumentFilePayload(path); });
  switch (result) {
  case InstrumentExportTransactionResult::Saved:
    return PERSIST_SAVED;
  case InstrumentExportTransactionResult::Exists:
    return PERSIST_EXISTS;
  case InstrumentExportTransactionResult::Error:
    return PERSIST_ERROR;
  }
  return PERSIST_ERROR;
}

bool PersistencyService::RecoverInstrumentExports() {
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr || !fs->chdir(INSTRUMENTS_DIR))
    return false;

  const auto hasSuffix = [](const char *filename, const char *suffix,
                            size_t &stemLength) {
    if (filename == nullptr || suffix == nullptr)
      return false;
    const size_t length = std::strlen(filename);
    const size_t suffixLength = std::strlen(suffix);
    if (length <= suffixLength ||
        strcasecmp(filename + length - suffixLength, suffix) != 0) {
      return false;
    }
    stemLength = length - suffixLength;
    return stemLength <= MAX_INSTRUMENT_NAME_LENGTH && filename[0] != '.';
  };

  etl::vector<int, MAX_FILE_INDEX_SIZE> journals;
  if (!fs->listChecked(&journals, ".bak", false))
    return false;
  for (const int index : journals) {
    if (fs->getFileType(index) == PFT_DIR)
      continue;
    char filename[PFILENAME_SIZE]{};
    fs->getFileName(index, filename, sizeof(filename));
    size_t stemLength = 0U;
    if (!hasSuffix(filename, ".bak", stemLength))
      continue;
    char stem[MAX_INSTRUMENT_NAME_LENGTH + 1U]{};
    std::memcpy(stem, filename, stemLength);
    char destination[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
    char temporary[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
    char backup[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
    if (!BuildInstrumentExportSiblingPaths(destination, temporary, backup,
                                           INSTRUMENTS_DIR, stem)) {
      return false;
    }
    const int actualBackupLength = std::snprintf(
        backup, sizeof(backup), "%s/%s", INSTRUMENTS_DIR, filename);
    if (actualBackupLength <= 0 ||
        static_cast<size_t>(actualBackupLength) >= sizeof(backup) ||
        !RecoverInstrumentExportFile(
            *fs, destination, temporary, backup,
            [](const char *path) {
              return ValidateInstrumentFilePayload(path);
            })) {
      return false;
    }
  }

  // A .tmp without a sibling backup was never committed. It is safe to drop
  // after backup recovery and prevents interrupted first-time exports from
  // accumulating indefinitely.
  journals.clear();
  if (!fs->listChecked(&journals, ".tmp", false))
    return false;
  for (const int index : journals) {
    if (fs->getFileType(index) == PFT_DIR)
      continue;
    char filename[PFILENAME_SIZE]{};
    fs->getFileName(index, filename, sizeof(filename));
    size_t stemLength = 0U;
    if (!hasSuffix(filename, ".tmp", stemLength))
      continue;
    char path[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
    const int pathLength =
        std::snprintf(path, sizeof(path), "%s/%s", INSTRUMENTS_DIR, filename);
    if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(path) ||
        (fs->exists(path) && !fs->DeleteFile(path))) {
      return false;
    }
  }
  return true;
}

bool RecoverInstrumentExportJournals() {
  PersistencyService *persistence = PersistencyService::GetInstance();
  return persistence != nullptr && persistence->RecoverInstrumentExports();
}

InstrumentType PersistencyService::DetectInstrumentType(const char *name) {
  FileSystem *fs = FileSystem::GetInstance();

  if (fs == nullptr || !fs->chdir(INSTRUMENTS_DIR)) {
    Trace::Error(
        "PERSISTENCYSERVICE: Could not change to instruments directory");
    return IT_NONE;
  }

  InstrumentType importedType = IT_NONE;
  if (!ReadInstrumentEnvelope(name, importedType, nullptr, 0U)) {
    Trace::Error("PERSISTENCYSERVICE: Invalid instrument envelope: %s",
                 name == nullptr ? "<null>" : name);
    return IT_NONE;
  }
  return importedType;
}

PersistencyResult PersistencyService::ImportInstrument(I_Instrument *instrument,
                                                       const char *name) {
  FileSystem *fs = FileSystem::GetInstance();

  if (fs == nullptr || !fs->chdir(INSTRUMENTS_DIR)) {
    Trace::Error(
        "PERSISTENCYSERVICE: Could not change to instruments directory");
    return PERSIST_ERROR;
  }

  InstrumentType importedType = IT_NONE;
  char versionInfo[64]{};
  if (!ReadInstrumentEnvelope(name, importedType, versionInfo,
                              sizeof(versionInfo))) {
    Trace::Error("PERSISTENCYSERVICE: Invalid instrument envelope: %s",
                 name == nullptr ? "<null>" : name);
    return PERSIST_ERROR;
  }

  // Log the complete version info if available
  if (versionInfo[0] != '\0') {
    Trace::Log("PERSISTENCYSERVICE",
               "Instrument created with firmware version: %s",
               versionInfo);
  }

  if (instrument == nullptr || importedType == IT_NONE ||
      importedType != instrument->GetType()) {
    Trace::Error("PERSISTENCYSERVICE",
                 "Instrument import type mismatch (target:%d file:%d)",
                 instrument == nullptr ? IT_NONE : instrument->GetType(),
                 importedType);
    return PERSIST_ERROR;
  }

  if (!ValidateInstrumentFilePayload(name)) {
    Trace::Error("PERSISTENCYSERVICE",
                 "Incomplete instrument payload in file: %s", name);
    return PERSIST_ERROR;
  }

  // Reload from the root because both type detection and structural
  // validation consume the forward-only parser.
  PersistencyDocument doc;
  if (!doc.Load(name) || !doc.FirstChild() ||
      std::strcmp(doc.ElemName(), "INSTRUMENT") != 0)
    return PERSIST_ERROR;

  // Restore the instrument content
  if (!instrument->Restore(&doc) || !doc.Finish()) {
    Trace::Error(
        "PERSISTENCYSERVICE: Failed to restore instrument from file: %s", name);
    return PERSIST_ERROR;
  }

  // Extract instrument name from filename (minus .pti extension)
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> instrumentName;
  const char *dotPos = strrchr(name, '.');
  if (dotPos) {
    // Calculate the length of the name without extension
    size_t nameLength = dotPos - name;
    // Copy only up to MAX_INSTRUMENT_NAME_LENGTH characters
    nameLength =
        nameLength <= MAX_INSTRUMENT_NAME_LENGTH ? nameLength
                                                 : MAX_INSTRUMENT_NAME_LENGTH;
    instrumentName.assign(name, nameLength);
  } else {
    // No extension found, use the whole name (up to MAX_INSTRUMENT_NAME_LENGTH)
    instrumentName.assign(name, strlen(name) <= MAX_INSTRUMENT_NAME_LENGTH
                                    ? strlen(name)
                                    : MAX_INSTRUMENT_NAME_LENGTH);
  }

  // Set the instrument name
  Variable *nameVar = instrument->FindVariable(FourCC::InstrumentName);
  if (nameVar) {
    nameVar->SetString(instrumentName.c_str());
  }

  // Mark the instrument as changed to trigger UI updates
  instrument->SetChanged();
  instrument->NotifyObservers();

  Trace::Log("PERSISTENCYSERVICE", "Successfully imported instrument settings");
  Trace::Log("PERSISTENCYSERVICE", "Set instrument name to: %s",
             instrumentName.c_str());
  return PERSIST_LOADED;
}
