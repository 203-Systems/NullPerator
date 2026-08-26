/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "TrackerApplicationSession.h"
#include "TrackerProjectLoadPolicy.h"

#include "Application/Commands/ApplicationCommandDispatcher.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Mixer.h"
#include "Application/Model/Table.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/Player.h"
#include "Application/Player/TablePlayback.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"

#include <cstring>
#include <nanoprintf.h>

TrackerApplicationSession::TrackerApplicationSession(const char *projectName)
    : project_(projectName), editorState_(&project_) {
  npf_snprintf(projectName_, sizeof(projectName_), "%s",
               projectName == nullptr ? UNNAMED_PROJECT_NAME : projectName);
}

TrackerApplicationSession::~TrackerApplicationSession() {
  if (loaded_)
    CloseProject();
}

TrackerApplicationSession::LoadResult TrackerApplicationSession::LoadProject(
    const char *projectName, bool createProject, bool discardCurrentAutoSave,
    bool recoverPreviousState) {
  if (projectName == nullptr || projectName[0] == '\0')
    return LoadResult::Failed;

  PersistencyService *persist = PersistencyService::GetInstance();
  const bool stagingProject =
      std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0;
  if (!PersistencyService::IsSafeProjectName_(projectName, stagingProject))
    return LoadResult::Failed;
  FileSystem *fileSystem = FileSystem::GetInstance();
  constexpr const char *stagingProjectPath =
      PROJECTS_DIR "/" UNNAMED_PROJECT_NAME "/" PROJECT_DATA_FILE;
  constexpr const char *stagingAutosavePath =
      PROJECTS_DIR "/" UNNAMED_PROJECT_NAME "/" AUTO_SAVE_FILENAME;
  const bool stagingPayloadExists =
      stagingProject &&
      (fileSystem->exists(stagingProjectPath) ||
       fileSystem->exists(stagingAutosavePath));
  // Validate a pre-existing project before resetting the live model. This is
  // intentionally a second parse: PersistencyService::Load performs the real
  // restore, while this pass guarantees a missing/corrupt selection cannot
  // destroy the current in-memory session merely by being highlighted.
  if (tracker_session_detail::ShouldPreflightProjectLoad(
          createProject, stagingProject, stagingPayloadExists) &&
      persist->Validate_(projectName, stagingProject) != PERSIST_LOADED)
    return LoadResult::Failed;

  char previousProjectName[MAX_PROJECT_NAME_LENGTH + 1]{};
  const bool hadLoadedProject = loaded_;
  const bool previousAudioReady = audioReady_;
  const TrackerSessionState previousEditorState = editorState_;
  bool rollbackPrepared = false;
  bool stagingTransactionStarted = false;
  bool stagingHadPrevious = false;
  if (hadLoadedProject) {
    npf_snprintf(previousProjectName, sizeof(previousProjectName), "%s",
                 projectName_);
    // Serialize the current model to a bounded on-disk transaction record.
    // This avoids a second ~169 KiB Project allocation on ESP32 while making
    // semantic restore errors and validate/load TOCTOU failures recoverable.
    if (persist->SaveLoadRollback() != PERSIST_SAVED) {
      persist->ClearLoadRollback();
      return LoadResult::Failed;
    }
    rollbackPrepared = true;
  }

  Player *player = Player::GetInstance();
  if (player->IsRunning())
    player->Stop();

  SamplePool *pool = SamplePool::GetInstance();
  auto resetModel = [this, pool](const char *name) {
    TablePlayback::Reset();
    TableHolder::GetInstance()->Reset();
    Mixer::GetInstance()->Clear();
    pool->Reset();
    project_.Load(name);
    loaded_ = false;
  };

  auto activateModel = [this, player]() {
    WatchedVariable::Disable();
    if (Variable *projectNameVariable =
            project_.FindVariable(FourCC::VarProjectName)) {
      auto *watched = static_cast<WatchedVariable *>(projectNameVariable);
      watched->RemoveObserver(*this);
      watched->AddObserver(*this);
    }
    project_.GetInstrumentBank()->Init();
    WatchedVariable::Enable();

    ApplicationCommandDispatcher::GetInstance()->Init(&project_);
    editorState_.Load(&project_);

    bool playerReady = true;
    if (!playerInitialized_) {
      playerReady = player->Init(&project_, &editorState_);
      playerInitialized_ = true;
    } else {
      player->BindProject(&project_, &editorState_);
    }
    audioReady_ = playerReady;
    UpdateProjectName();
    loaded_ = true;
  };

  auto failAndRollback = [&]() {
    if (stagingTransactionStarted) {
      if (!persist->RollbackStagingProjectReplacement_(stagingHadPrevious)) {
        Trace::Error("Failed to roll back untitled project directory");
      }
      stagingTransactionStarted = false;
    }

    if (!rollbackPrepared)
      return LoadResult::Failed;

    resetModel(previousProjectName);
    if (!pool->Load(previousProjectName)) {
      Trace::Error("Failed to restore sample pool for '%s'",
                   previousProjectName);
      // Keep the serialized rollback file: a later boot/retry may have working
      // media even though this directory enumeration failed.
      return LoadResult::Failed;
    }
    if (persist->RestoreLoadRollback() != PERSIST_LOADED) {
      Trace::Error("Failed to restore project load transaction for '%s'",
                   previousProjectName);
      // Keep the transaction file for manual/startup recovery if the first
      // restore attempt itself encountered an I/O failure.
      return LoadResult::Failed;
    }
    activateModel();
    // The serialized model covers project data; the small fixed-size editor
    // state preserves cursor/page playback context without another model copy.
    editorState_ = previousEditorState;
    editorState_.project_ = &project_;
    editorState_.song_ = &project_.song_;
    audioReady_ = previousAudioReady;
    if (persist->SaveProjectState_(
            previousProjectName,
            std::strcmp(previousProjectName, UNNAMED_PROJECT_NAME) == 0) !=
        PERSIST_SAVED) {
      Trace::Error("Failed to restore project marker for '%s'",
                   previousProjectName);
    }
    persist->ClearLoadRollback();
    return LoadResult::Failed;
  };

  if (createProject && stagingProject) {
    if (!persist->BeginStagingProjectReplacement_(
            hadLoadedProject ? previousProjectName : "",
            stagingHadPrevious)) {
      Trace::Error("Failed to begin untitled project transaction");
      if (rollbackPrepared)
        persist->ClearLoadRollback();
      return LoadResult::Failed;
    }
    stagingTransactionStarted = true;
  }

  resetModel(projectName);
  bool missingUnnamedProject = false;
  if (stagingProject && !createProject) {
    constexpr const char *samplesPath =
        PROJECTS_DIR "/" UNNAMED_PROJECT_NAME "/" PROJECT_SAMPLES_DIR;
    if (!fileSystem->exists(samplesPath) &&
        !fileSystem->makeDir(samplesPath, true)) {
      Trace::Error("Failed to create untitled samples directory");
      return failAndRollback();
    }
    missingUnnamedProject = !stagingPayloadExists;
  }

  if (createProject || missingUnnamedProject) {
    if (persist->CreateProject() != PERSIST_SAVED) {
      Trace::Error("Failed to create new project '%s'", projectName);
      return failAndRollback();
    }
  }

  if (!pool->Load(projectName)) {
    Trace::Error("Failed to load sample pool for '%s'", projectName);
    return failAndRollback();
  }
  bool loadedFromAutosave = false;
  bool semanticLoaded =
      persist->Load_(projectName, stagingProject, &loadedFromAutosave) ==
      PERSIST_LOADED;

  // A structurally valid generation can still fail late semantic restoration
  // after mutating instruments/tables. Never layer another generation onto
  // that partial model: rebuild every model-owned pool before each retry.
  if (!semanticLoaded && loadedFromAutosave) {
    Trace::Error("Autosave restore failed for '%s'; retrying backup",
                 projectName);
    resetModel(projectName);
    if (!pool->Load(projectName))
      return failAndRollback();
    semanticLoaded =
        persist->LoadProjectJournalBackup_(projectName, true,
                                           stagingProject) == PERSIST_LOADED &&
        persist->PromoteProjectJournalBackup_(projectName, true,
                                              stagingProject);
  }

  if (!semanticLoaded) {
    Trace::Error("Project restore failed for '%s'; retrying base",
                 projectName);
    resetModel(projectName);
    if (!pool->Load(projectName))
      return failAndRollback();
    loadedFromAutosave = false;
    semanticLoaded =
        persist->LoadBase_(projectName, stagingProject) == PERSIST_LOADED;
    if (!semanticLoaded) {
      Trace::Error("Base restore failed for '%s'; retrying backup",
                   projectName);
      resetModel(projectName);
      if (!pool->Load(projectName))
        return failAndRollback();
      semanticLoaded =
          persist->LoadProjectJournalBackup_(projectName, false,
                                             stagingProject) ==
              PERSIST_LOADED &&
          persist->PromoteProjectJournalBackup_(projectName, false,
                                                stagingProject);
    }

    if (semanticLoaded &&
        !persist->ClearAutosave_(projectName, stagingProject)) {
      Trace::Error("Failed to clear rejected autosave for '%s'", projectName);
    }
  }

  if (!semanticLoaded) {
    pool->Reset();
    TableHolder::GetInstance()->Reset();

    // A boot-recovered COMMITTED `.untitled` stays transactionally protected
    // until semantic restore. Rejecting it must restore both the previous
    // project marker and the old untitled directory before fallback.
    char transactionPrevious[MAX_PROJECT_NAME_LENGTH + 1U]{};
    if (stagingProject &&
        persist->HasCommittedStagingProjectReplacement_()) {
      if (!persist->RollbackCommittedStagingProjectReplacement_(
              transactionPrevious)) {
        Trace::Error("Failed to roll back semantic-invalid untitled");
        return failAndRollback();
      }
      if (!hadLoadedProject && recoverPreviousState &&
          transactionPrevious[0] != '\0') {
        return LoadProject(transactionPrevious, false, false, false);
      }
    }

    // `.current.bak` is retained until a project completes semantic restore.
    // At boot, retry that previous pointer once before creating untitled.
    if (!hadLoadedProject && recoverPreviousState) {
      char previousProject[MAX_PROJECT_NAME_LENGTH + 1U]{};
      if (persist->ReadPreviousProjectName_(previousProject)) {
        return LoadProject(previousProject, false, false, false);
      }
    }
    return failAndRollback();
  }

  if (!persist->FinalizeProjectJournal_(projectName, loadedFromAutosave,
                                        stagingProject)) {
    // The loaded generation is already known semantic-good. Retaining a
    // journal sibling is safe and lets the next boot retry cleanup.
    Trace::Error("Project journal cleanup deferred for '%s'", projectName);
  }

  activateModel();
  // The current-project marker is the load transaction's durable commit
  // point. Keep both the old in-memory rollback and any untitled directory
  // backup until it is synced. If this fails while switching an already
  // loaded session, restore the live project instead of reporting a candidate
  // that will silently disappear on reboot.
  if (persist->SaveProjectState_(
          projectName_, stagingProject, stagingTransactionStarted,
          hadLoadedProject ? previousProjectName : "") !=
      PERSIST_SAVED) {
    Trace::Error("Failed to save project state for '%s'", projectName_);
    // A first-boot/new session has no in-memory predecessor, but its untitled
    // directory is still protected by the staging transaction. Reporting a
    // loaded project without a durable `.current` marker makes the next boot
    // replace that only copy. Roll back every failed marker commit, not just a
    // switch from an already-loaded project.
    return failAndRollback();
  }
  if (stagingTransactionStarted) {
    // SaveProjectState_ cannot distinguish old and new `.untitled` sessions.
    // Persist a separate transaction phase before backup cleanup so reboot
    // recovery can tell commit-before-cleanup from pre-commit power loss.
    if (!persist->CommitStagingProjectReplacement_(stagingHadPrevious)) {
      Trace::Error("Failed to commit untitled project transaction");
      return failAndRollback();
    }
    stagingTransactionStarted = false;
  } else if (stagingProject &&
             persist->HasCommittedStagingProjectReplacement_() &&
             !persist->FinalizeCommittedStagingProjectReplacement_()) {
    // COMMITTED remains durable, so a cleanup error must not turn a
    // semantic-good loaded session into a destructive rollback.
    Trace::Error("Untitled transaction cleanup deferred");
  }
  // Committing a different project is the first point at which the caller's
  // request to discard the previous recovery file becomes irreversible.
  if (discardCurrentAutoSave && hadLoadedProject &&
      !persist->ClearAutosave_(
          previousProjectName,
          std::strcmp(previousProjectName, UNNAMED_PROJECT_NAME) == 0)) {
    Trace::Error("Failed to clear previous project autosave for '%s'",
                 previousProjectName);
  }
  if (rollbackPrepared)
    persist->ClearLoadRollback();
  // A project load remains successful when the audio driver fails. The UI2
  // host surfaces AudioReady()==false as a system dialog, matching the old
  // behavior without conflating model recovery with device initialization.
  return LoadResult::Loaded;
}

TrackerApplicationSession::LoadResult TrackerApplicationSession::NewProject() {
  return LoadProject(UNNAMED_PROJECT_NAME, true, true);
}

TrackerApplicationSession::SaveResult
TrackerApplicationSession::SaveProject(const char *oldProjectName, bool saveAs,
                                       bool overwrite) {
  if (!loaded_ || projectName_[0] == '\0')
    return SaveResult::Failed;
  PersistencyService *persist = PersistencyService::GetInstance();
  if (saveAs && !overwrite && persist->Exists(projectName_))
    return SaveResult::Exists;
  const bool stagingProject =
      std::strcmp(projectName_, UNNAMED_PROJECT_NAME) == 0;
  if (persist->Save_(projectName_,
                     oldProjectName == nullptr ? "" : oldProjectName, saveAs,
                     stagingProject) != PERSIST_SAVED)
    return SaveResult::Failed;
  if (persist->SaveProjectState_(projectName_, stagingProject) !=
      PERSIST_SAVED)
    return SaveResult::Failed;
  return SaveResult::Saved;
}

bool TrackerApplicationSession::DeleteProject(const char *projectName) {
  if (projectName == nullptr || projectName[0] == '\0' ||
      std::strcmp(projectName, projectName_) == 0 ||
      Player::GetInstance()->IsRunning())
    return false;
  return PersistencyService::GetInstance()->DeleteProject(projectName);
}

void TrackerApplicationSession::DiscardAutoSave() {
  if (loaded_ && projectName_[0] != '\0' &&
      !PersistencyService::GetInstance()->ClearAutosave_(
          projectName_,
          std::strcmp(projectName_, UNNAMED_PROJECT_NAME) == 0)) {
    Trace::Error("Failed to discard autosave for '%s'", projectName_);
  }
}

void TrackerApplicationSession::CloseProject() {
  Player *player = Player::GetInstance();
  player->Stop();
  SamplePool::GetInstance()->Reset();
  TableHolder::GetInstance()->Reset();
  TablePlayback::Reset();
  ApplicationCommandDispatcher::GetInstance()->Close();
  loaded_ = false;
}

bool TrackerApplicationSession::AutoSave(bool controllerAllowsSave,
                                         bool recordingActive) {
  if (!loaded_ || !controllerAllowsSave || recordingActive)
    return false;
  Player *player = Player::GetInstance();
  if (player->IsRunning())
    return false;

  Trace::Log("APPLICATION_SESSION", "AutoSaving Project Data");
  const PersistencyResult result =
      PersistencyService::GetInstance()->AutoSaveProjectData_(
          projectName_, std::strcmp(projectName_, UNNAMED_PROJECT_NAME) == 0);
  if (result != PERSIST_SAVED) {
    Trace::Error("APPLICATION_SESSION", "Failed to auto-save project data");
  }
  return result == PERSIST_SAVED;
}

void TrackerApplicationSession::Update(Observable &, I_ObservableData *data) {
  if (data != nullptr &&
      reinterpret_cast<std::uintptr_t>(data) ==
          static_cast<std::uintptr_t>(FourCC::VarProjectName)) {
    UpdateProjectName();
  }
}

void TrackerApplicationSession::UpdateProjectName() {
  project_.GetProjectName(projectName_);
  projectName_[MAX_PROJECT_NAME_LENGTH] = '\0';
}
