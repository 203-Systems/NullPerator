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
#include "ProjectFileJournal.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include <cstdio>
#include <cstring>

#include "PersistencyPaths.h"
using namespace PersistencyPaths;

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
    return allowStaging && std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0;
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
                 STAGING_TRANSACTION_PURGE_TEMP_FILE, STAGING_PURGE_CONTENTS)) {
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
  if (!internalTransaction && !IsSafeProjectName_(projectName, allowStaging)) {
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

bool PersistencyService::DeleteDirectoryContents_(uint8_t depth) {
  auto fs = FileSystem::GetInstance();
  if (depth > MAX_DELETE_DEPTH) {
    Trace::Error("PERSISTENCYSERVICE: delete depth exceeded");
    return false;
  }

  while (true) {
    fileIndexes_.clear();
    if (!fs->listChecked(&fileIndexes_, "", false, true)) {
      Trace::Error("PERSISTENCYSERVICE: Directory listing truncated");
      return false;
    }

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
                                            bool saveAs, bool allowStaging) {
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

  etl::vector<const char *, 3> modelSegments = {PROJECTS_DIR, stageName,
                                                PROJECT_DATA_FILE};
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

    const size_t sourceLength =
        std::strlen(PROJECTS_DIR) + 1U + std::strlen(sourceProject) + 1U +
        std::strlen(PROJECT_SAMPLES_DIR) + 1U + std::strlen(filenameBuffer);
    const size_t targetLength =
        std::strlen(PROJECTS_DIR) + 1U + std::strlen(targetProject) + 1U +
        std::strlen(PROJECT_SAMPLES_DIR) + 1U + std::strlen(filenameBuffer);
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
  if (!IsSafeProjectName_(projectName, allowStaging))
    return PERSIST_ERROR;

  char destinationPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char tempPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backupPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(destinationPath, projectName, filename) ||
      !BuildProjectFilePath(tempPath, projectName, tempFilename) ||
      !BuildProjectFilePath(backupPath, projectName, backupFilename)) {
    return PERSIST_ERROR;
  }

  const project_file_journal::Paths paths{destinationPath, tempPath,
                                          backupPath};
  const bool saved = project_file_journal::SaveAtomically(
      *FileSystem::GetInstance(), paths,
      [this](const char *path) {
        return SaveProjectFile_(path) == PERSIST_SAVED;
      },
      [this](const char *path) {
        return ValidateProjectFile_(path) == PERSIST_LOADED;
      });
  return saved ? PERSIST_SAVED : PERSIST_ERROR;
}

bool PersistencyService::RecoverProjectFileJournal_(const char *projectName,
                                                    const char *filename,
                                                    const char *tempFilename,
                                                    const char *backupFilename,
                                                    bool allowStaging) {
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

  const project_file_journal::Paths paths{destinationPath, tempPath,
                                          backupPath};
  return project_file_journal::Recover(
      *FileSystem::GetInstance(), paths, [this](const char *path) {
        return ValidateProjectFile_(path) == PERSIST_LOADED;
      });
}

bool PersistencyService::RecoverAutosaveJournal_(const char *projectName,
                                                 bool allowStaging) {
  return RecoverProjectFileJournal_(projectName, AUTO_SAVE_FILENAME,
                                    AUTO_SAVE_TEMP_FILENAME,
                                    AUTO_SAVE_BACKUP_FILENAME, allowStaging);
}

bool PersistencyService::RecoverBaseJournal_(const char *projectName,
                                             bool allowStaging) {
  return RecoverProjectFileJournal_(projectName, PROJECT_DATA_FILE,
                                    PROJECT_DATA_TEMP_FILE,
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
  if (!BuildProjectFilePath(backupPath, projectName,
                            autosave ? AUTO_SAVE_BACKUP_FILENAME
                                     : PROJECT_DATA_BACKUP_FILE)) {
    return PERSIST_LOAD_FAILED;
  }
  FileSystem *fs = FileSystem::GetInstance();
  if (!fs->exists(backupPath) ||
      ValidateProjectFile_(backupPath) != PERSIST_LOADED) {
    return PERSIST_LOAD_FAILED;
  }
  return LoadProjectFile_(backupPath);
}

bool PersistencyService::PromoteProjectJournalBackup_(const char *projectName,
                                                      bool autosave,
                                                      bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return false;
  char destinationPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char tempPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backupPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(destinationPath, projectName,
                            autosave ? AUTO_SAVE_FILENAME
                                     : PROJECT_DATA_FILE) ||
      !BuildProjectFilePath(tempPath, projectName,
                            autosave ? AUTO_SAVE_TEMP_FILENAME
                                     : PROJECT_DATA_TEMP_FILE) ||
      !BuildProjectFilePath(backupPath, projectName,
                            autosave ? AUTO_SAVE_BACKUP_FILENAME
                                     : PROJECT_DATA_BACKUP_FILE)) {
    return false;
  }
  const project_file_journal::Paths paths{destinationPath, tempPath,
                                          backupPath};
  return project_file_journal::PromoteBackup(
      *FileSystem::GetInstance(), paths, [this](const char *path) {
        return ValidateProjectFile_(path) == PERSIST_LOADED;
      });
}

bool PersistencyService::FinalizeProjectJournal_(const char *projectName,
                                                 bool autosave,
                                                 bool allowStaging) {
  if (!IsSafeProjectName_(projectName, allowStaging))
    return false;
  char destinationPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char tempPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backupPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(destinationPath, projectName,
                            autosave ? AUTO_SAVE_FILENAME
                                     : PROJECT_DATA_FILE) ||
      !BuildProjectFilePath(tempPath, projectName,
                            autosave ? AUTO_SAVE_TEMP_FILENAME
                                     : PROJECT_DATA_TEMP_FILE) ||
      !BuildProjectFilePath(backupPath, projectName,
                            autosave ? AUTO_SAVE_BACKUP_FILENAME
                                     : PROJECT_DATA_BACKUP_FILE)) {
    return false;
  }
  const project_file_journal::Paths paths{destinationPath, tempPath,
                                          backupPath};
  return project_file_journal::Finalize(*FileSystem::GetInstance(), paths);
}

PersistencyResult PersistencyService::ValidateProjectFile_(const char *path) {
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
    const char *recoverySource = tempExists     ? PROJECT_STATE_TEMP_FILE
                                 : backupExists ? PROJECT_STATE_BACKUP_FILE
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
  if (length <= 0 || length > MAX_PROJECT_NAME_LENGTH || backup->Error() != 0) {
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

PersistencyResult PersistencyService::SaveProjectState_(
    const char *projectName, bool allowStaging, bool retainPreviousBackup,
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
           std::memcmp(stored, text, static_cast<size_t>(expectedLength)) == 0;
  };

  const bool preservePrevious = retainPreviousBackup &&
                                previousProjectName != nullptr &&
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
      if (!markerMatchesText(PROJECT_STATE_BACKUP_FILE, previousProjectName)) {
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
      I_File *rawBackup = AcquireLegacyFileHandle_DO_NOT_USE(std::move(backup));
      const bool backupClosed = CloseFile_DO_NOT_USE(rawBackup);
      if (!backupSynced || !backupClosed ||
          !fs->MoveFile(PROJECT_STATE_BACKUP_TEMP_FILE,
                        PROJECT_STATE_BACKUP_FILE) ||
          !markerMatchesText(PROJECT_STATE_BACKUP_FILE, previousProjectName)) {
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
      const bool backupRemoved = fs->DeleteFile(PROJECT_STATE_BACKUP_FILE);
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

  char destinationPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char tempPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  char backupPath[MAX_PROJECT_SAMPLE_PATH_LENGTH]{};
  if (!BuildProjectFilePath(destinationPath, projectName, AUTO_SAVE_FILENAME) ||
      !BuildProjectFilePath(tempPath, projectName, AUTO_SAVE_TEMP_FILENAME) ||
      !BuildProjectFilePath(backupPath, projectName,
                            AUTO_SAVE_BACKUP_FILENAME)) {
    return false;
  }
  const project_file_journal::Paths paths{destinationPath, tempPath,
                                          backupPath};
  return project_file_journal::Discard(*FileSystem::GetInstance(), paths);
}
