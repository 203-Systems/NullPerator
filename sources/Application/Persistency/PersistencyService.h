/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _PERSISTENCY_SERVICE_H_
#define _PERSISTENCY_SERVICE_H_

#include "Application/Instruments/I_Instrument.h"
#include "Externals/TinyXML2/tinyxml2.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/yxml/yxml.h"

#include "Foundation/Services/Service.h"
#include "Foundation/T_Singleton.h"
#include "PersistenceConstants.h"

enum PersistencyResult {
  PERSIST_SAVED,
  PERSIST_LOAD_FAILED,
  PERSIST_LOADED,
  PERSIST_ERROR,
  PERSIST_EXISTS,
};

#define UNNAMED_PROJECT_NAME ".untitled"
#define STAGING_BACKUP_PROJECT_NAME ".untitled.session-backup"
// The untitled replacement uses an explicit durable phase marker because the
// old and new projects have the same public name.  `.current` alone therefore
// cannot distinguish a crash before the new session commits from one after it
// commits.  These files live at the filesystem root, outside the user project
// namespace.
#define STAGING_TRANSACTION_PENDING_FILE                                    \
  "/.picotracker-untitled-session-pending"
#define STAGING_TRANSACTION_PENDING_TEMP_FILE                               \
  "/.picotracker-untitled-session-pending.tmp"
#define STAGING_TRANSACTION_COMMIT_FILE                                     \
  "/.picotracker-untitled-session-commit"
#define STAGING_TRANSACTION_COMMIT_TEMP_FILE                                \
  "/.picotracker-untitled-session-commit.tmp"
#define STAGING_TRANSACTION_PURGE_FILE                                      \
  "/.picotracker-untitled-session-purge"
#define STAGING_TRANSACTION_PURGE_TEMP_FILE                                 \
  "/.picotracker-untitled-session-purge.tmp"
// Each prefix alone is longer than MAX_PROJECT_NAME_LENGTH, so no project
// created by older firmware can ever collide with transaction directories.
#define SAVE_AS_STAGE_PREFIX ".picotracker-saveas-stage."
#define SAVE_AS_BACKUP_PREFIX ".picotracker-saveas-backup."
#define PROJECT_DATA_FILE "lgptsav.dat"
#define PROJECT_DATA_TEMP_FILE "lgptsav.tmp"
#define PROJECT_DATA_BACKUP_FILE "lgptsav.bak"
#define AUTO_SAVE_FILENAME "autosave.dat"
#define AUTO_SAVE_TEMP_FILENAME "autosave.tmp"
#define AUTO_SAVE_BACKUP_FILENAME "autosave.bak"

class TrackerApplicationSession;
#ifdef HOST_TEST
struct PersistencyServiceTestPeer;
#endif

class PersistencyService : public Service,
                           public T_Singleton<PersistencyService> {
public:
  PersistencyService();
  // User supplied project names are always a single, bounded path component.
  // The fixed .untitled staging name is deliberately not a user project name;
  // only the session-only helpers below may opt in to it.
  [[nodiscard]] static bool IsValidProjectName(const char *projectName);
  // Browser/controllers use the same persistence-owned reserved-name policy
  // as transactional recovery. These names must never be user-addressable.
  [[nodiscard]] static bool IsInternalProjectName(const char *projectName);
  PersistencyResult Save(const char *projectName, const char *oldProjectName,
                         bool saveAs);
  [[nodiscard]] PersistencyResult Validate(const char *projectName);
  PersistencyResult Load(const char *projectName);
  PersistencyResult LoadCurrentProjectName(char *projectName);
  PersistencyResult SaveProjectState(const char *projectName);
  PersistencyResult CreateProject();
  bool Exists(const char *projectName);
  bool PurgeUnnamedProject();
  bool DeleteProject(const char *projectName);
  PersistencyResult AutoSaveProjectData(const char *projectName);
  bool ClearAutosave(const char *projectName);
  // A load transaction snapshots only the serialized model, not a second
  // 169-KiB Project object. The file lives outside individual project
  // directories so switching projects cannot delete the rollback source.
  [[nodiscard]] PersistencyResult SaveLoadRollback();
  [[nodiscard]] PersistencyResult RestoreLoadRollback();
  void ClearLoadRollback();

  PersistencyResult
  ExportInstrument(I_Instrument *instrument,
                   etl::string<MAX_INSTRUMENT_NAME_LENGTH> name,
                   bool overwrite = false);
  PersistencyResult ImportInstrument(I_Instrument *instrument,
                                     const char *name);
  InstrumentType DetectInstrumentType(const char *name);
  // Completes or rolls back sibling .tmp/.bak journals before a browser scan
  // so a power loss cannot make the last committed instrument disappear.
  [[nodiscard]] bool RecoverInstrumentExports();

private:
  friend class TrackerApplicationSession;
#ifdef HOST_TEST
  friend struct PersistencyServiceTestPeer;
#endif

  [[nodiscard]] static bool IsSafeProjectName_(const char *projectName,
                                               bool allowStaging);
  [[nodiscard]] bool Exists_(const char *projectName, bool allowStaging);
  [[nodiscard]] PersistencyResult Validate_(const char *projectName,
                                            bool allowStaging);
  [[nodiscard]] PersistencyResult Load_(const char *projectName,
                                        bool allowStaging,
                                        bool *usedAutosave = nullptr);
  [[nodiscard]] PersistencyResult LoadBase_(const char *projectName,
                                            bool allowStaging);
  [[nodiscard]] PersistencyResult
  LoadProjectJournalBackup_(const char *projectName, bool autosave,
                            bool allowStaging);
  [[nodiscard]] bool PromoteProjectJournalBackup_(const char *projectName,
                                                   bool autosave,
                                                   bool allowStaging);
  [[nodiscard]] bool FinalizeProjectJournal_(const char *projectName,
                                              bool autosave,
                                              bool allowStaging);
  [[nodiscard]] PersistencyResult Save_(const char *projectName,
                                        const char *oldProjectName,
                                        bool saveAs, bool allowStaging);
  [[nodiscard]] PersistencyResult
  SaveProjectState_(const char *projectName, bool allowStaging,
                    bool retainPreviousBackup = false,
                    const char *previousProjectName = nullptr);
  [[nodiscard]] PersistencyResult
  AutoSaveProjectData_(const char *projectName, bool allowStaging);
  [[nodiscard]] bool ClearAutosave_(const char *projectName,
                                    bool allowStaging);
  [[nodiscard]] bool DeleteProject_(const char *projectName,
                                    bool allowStaging);
  [[nodiscard]] bool CopyProjectSamples_(const char *sourceProject,
                                         const char *targetProject);
  [[nodiscard]] PersistencyResult SaveAsProject_(const char *projectName,
                                                 const char *oldProjectName);
  [[nodiscard]] bool RecoverInternalProjectTransactions_();
  [[nodiscard]] bool ReadPreviousProjectName_(char *projectName);
  [[nodiscard]] bool RecoverStagingProjectReplacement_();
  [[nodiscard]] bool RecoverSaveAsTransactions_();
  [[nodiscard]] bool
  BeginStagingProjectReplacement_(const char *previousProjectName,
                                  bool &hadPrevious);
  [[nodiscard]] bool CommitStagingProjectReplacement_(bool hadPrevious);
  [[nodiscard]] bool RollbackStagingProjectReplacement_(bool hadPrevious);
  [[nodiscard]] bool HasCommittedStagingProjectReplacement_();
  [[nodiscard]] bool FinalizeCommittedStagingProjectReplacement_();
  [[nodiscard]] bool RollbackCommittedStagingProjectReplacement_(
      char *previousProjectName);
  [[nodiscard]] bool CompleteStagingProjectPurge_();
  [[nodiscard]] bool ClearStagingTransactionMarkers_();
  [[nodiscard]] bool WriteStagingTransactionMarker_(const char *path,
                                                     const char *tempPath,
                                                     const char *contents);
  [[nodiscard]] bool HasStagingTransactionMarker_(const char *path,
                                                   const char *contents);
  [[nodiscard]] bool ReadStagingPendingMarker_(bool &hadPrevious,
                                               char *previousProjectName);
  PersistencyResult CreateProjectDirs_(const char *projectName);
  void CreatePath(etl::istring &path,
                  const etl::ivector<const char *> &segments);
  PersistencyResult SaveProjectData(const char *projectName, bool autosave,
                                    bool allowStaging = false);
  PersistencyResult SaveProjectFile_(const char *path);
  [[nodiscard]] PersistencyResult SaveProjectFileAtomically_(
      const char *projectName, const char *filename, const char *tempFilename,
      const char *backupFilename, bool allowStaging);
  [[nodiscard]] PersistencyResult ValidateProjectFile_(const char *path);
  [[nodiscard]] bool RecoverProjectFileJournal_(
      const char *projectName, const char *filename, const char *tempFilename,
      const char *backupFilename, bool allowStaging);
  [[nodiscard]] bool RecoverBaseJournal_(const char *projectName,
                                         bool allowStaging);
  [[nodiscard]] bool RecoverAutosaveJournal_(const char *projectName,
                                             bool allowStaging);
  PersistencyResult LoadProjectFile_(const char *path);
  bool DeleteDirectoryContents_(uint8_t depth);
  bool DeleteDirectoryTree_(const char *dirname, uint8_t depth);

  // need these as statically allocated buffers as too big for stack
  etl::vector<int, MAX_FILE_INDEX_SIZE> fileIndexes_;
  etl::string<MAX_PROJECT_SAMPLE_PATH_LENGTH> pathBufferA;
  etl::string<MAX_PROJECT_SAMPLE_PATH_LENGTH> pathBufferB;
  char deleteNameBuffer_[PFILENAME_SIZE];
};

#endif
