/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Model/Project.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Session/TrackerSessionState.h"
#include "Foundation/Observable.h"

#include <cstdint>

// Project, player and persistence lifecycle shared by every presentation
// implementation. This deliberately owns no View, GUIWindow or renderer.
class TrackerApplicationSession final : public I_Observer {
public:
  enum class LoadResult : std::int8_t { Failed = -1, Loaded = 0 };

  explicit TrackerApplicationSession(const char *projectName);
  ~TrackerApplicationSession() override;

  [[nodiscard]] LoadResult LoadProject(const char *projectName,
                                       bool createProject = false);
  void CloseProject();

  // The UI host supplies whether its current controller/modal state permits an
  // autosave. Playback and recording remain session-level safety checks.
  [[nodiscard]] bool AutoSave(bool controllerAllowsSave,
                              bool recordingActive);

  [[nodiscard]] Project &ProjectModel() { return project_; }
  [[nodiscard]] const Project &ProjectModel() const { return project_; }
  [[nodiscard]] TrackerSessionState &EditorState() { return editorState_; }
  [[nodiscard]] const TrackerSessionState &EditorState() const {
    return editorState_;
  }
  [[nodiscard]] const char *ProjectName() const { return projectName_; }
  [[nodiscard]] bool PlayerInitialized() const { return playerInitialized_; }
  [[nodiscard]] bool AudioReady() const { return audioReady_; }
  [[nodiscard]] bool IsLoaded() const { return loaded_; }

  void Update(Observable &observable, I_ObservableData *data) override;

private:
  void UpdateProjectName();

  Project project_;
  TrackerSessionState editorState_;
  char projectName_[MAX_PROJECT_NAME_LENGTH + 1]{};
  bool playerInitialized_ = false;
  bool audioReady_ = false;
  bool loaded_ = false;
};
