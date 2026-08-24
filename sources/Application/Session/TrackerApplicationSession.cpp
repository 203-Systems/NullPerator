/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "TrackerApplicationSession.h"

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

TrackerApplicationSession::LoadResult
TrackerApplicationSession::LoadProject(const char *projectName,
                                       bool createProject) {
  if (projectName == nullptr || projectName[0] == '\0')
    return LoadResult::Failed;

  PersistencyService *persist = PersistencyService::GetInstance();
  Player *player = Player::GetInstance();
  if (player->IsRunning())
    player->Stop();

  TablePlayback::Reset();
  TableHolder::GetInstance()->Reset();
  Mixer::GetInstance()->Clear();
  SamplePool *pool = SamplePool::GetInstance();
  pool->Reset();

  project_.Load(projectName);
  bool missingUnnamedProject = false;
  if (std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0) {
    FileSystem *fileSystem = FileSystem::GetInstance();
    constexpr const char *samplesPath =
        PROJECTS_DIR "/" UNNAMED_PROJECT_NAME "/" PROJECT_SAMPLES_DIR;
    constexpr const char *projectPath =
        PROJECTS_DIR "/" UNNAMED_PROJECT_NAME "/" PROJECT_DATA_FILE;
    constexpr const char *autosavePath =
        PROJECTS_DIR "/" UNNAMED_PROJECT_NAME "/" AUTO_SAVE_FILENAME;
    if (!fileSystem->exists(samplesPath) &&
        !fileSystem->makeDir(samplesPath, true)) {
      Trace::Error("Failed to create untitled samples directory");
      return LoadResult::Failed;
    }
    missingUnnamedProject =
        !fileSystem->exists(projectPath) && !fileSystem->exists(autosavePath);
  }

  if (createProject || missingUnnamedProject) {
    if (persist->CreateProject() != PERSIST_SAVED) {
      Trace::Error("Failed to create new project '%s'", projectName);
      return LoadResult::Failed;
    }
  }

  pool->Load(projectName);
  if (persist->Load(projectName) != PERSIST_LOADED) {
    Trace::Error("Failed to load project '%s'", projectName);
    pool->Reset();
    TableHolder::GetInstance()->Reset();
    return LoadResult::Failed;
  }

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
  if (persist->SaveProjectState(projectName_) != PERSIST_SAVED) {
    Trace::Error("Failed to save project state for '%s'", projectName_);
  }
  // A project load remains successful when the audio driver fails. The UI2
  // host surfaces AudioReady()==false as a system dialog, matching the old
  // behavior without conflating model recovery with device initialization.
  return LoadResult::Loaded;
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
      PersistencyService::GetInstance()->AutoSaveProjectData(projectName_);
  if (result != PERSIST_SAVED) {
    Trace::Error("APPLICATION_SESSION", "Failed to auto-save project data");
  }
  // Preserve the legacy cadence: a failed write is retried at the next normal
  // interval instead of on every UI tick.
  return true;
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
