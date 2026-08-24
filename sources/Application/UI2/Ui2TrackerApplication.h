/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/ITrackerInputSink.h"
#include "Application/Session/TrackerApplicationSession.h"
#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/UI2/Controllers/Ui2GrooveController.h"
#include "Application/UI2/Controllers/Ui2DeviceController.h"
#include "Application/UI2/Controllers/Ui2ThemeController.h"
#include "Application/UI2/Controllers/Ui2FontController.h"
#include "Application/UI2/Controllers/Ui2ProjectController.h"
#include "Application/UI2/Controllers/Ui2RenameController.h"
#include "Application/UI2/Controllers/Ui2TrackerControllerHub.h"
#include "Application/UI2/Ui2ApplicationRuntime.h"
#include "Application/UI2/Ui2NativeApplicationStateSource.h"
#include "Application/UI2/Ui2TrackerSessionModelPort.h"

#include <array>

namespace ui2 {

// The single native owner of UI2 input, controller state, model mutation and
// rendering. Platform adapters deliver TrackerAction values here directly;
// legacy AppWindow/View instances are intentionally absent from this graph.
class Ui2TrackerApplication final : public ITrackerInputSink {
public:
  explicit Ui2TrackerApplication(IUiPresenter &presenter);

  [[nodiscard]] bool Init();
  void DispatchTrackerAction(TrackerAction action, bool pressed) override;
  [[nodiscard]] PresentResult Present();
  void Invalidate() { runtime_.Invalidate(); }

  [[nodiscard]] TrackerApplicationSession &Session() { return session_; }
  [[nodiscard]] const TrackerApplicationSession &Session() const {
    return session_;
  }
  [[nodiscard]] Ui2NativeApplicationStateSource &StateSource() {
    return source_;
  }
  [[nodiscard]] UiApplicationPage ActivePage() const { return activePage_; }
  bool ActivatePage(UiApplicationPage page);

private:
  void HandleProject(TrackerAction action, bool pressed);
  void HandleGroove(TrackerAction action, bool pressed);
  void HandleDevice(TrackerAction action, bool pressed);
  void ExecuteDevice(Ui2DeviceCommand command);
  void HandleTheme(TrackerAction action, bool pressed);
  void ExecuteTheme(Ui2ThemeCommand command);
  void HandleFont(TrackerAction action, bool pressed);
  void HandleRename(TrackerAction action, bool pressed);
  void ExecuteProject(Ui2ProjectCommand command);
  void ExecuteGroove(Ui2GrooveCommand command);
  void SynchronizeGridPage();

  TrackerApplicationSession session_;
  Ui2TrackerSessionModelPort modelPort_;
  Ui2TrackerCommandExecutor tracker_;
  Ui2ProjectController project_{};
  Ui2GrooveController groove_{};
  Ui2DeviceController device_{};
  Ui2ThemeController theme_{};
  Ui2FontController font_{};
  Ui2RenameController rename_{};
  Ui2ControllerInputState projectInput_{};
  Ui2NativeApplicationStateSource source_;
  UiApplicationRuntime runtime_;
  UiApplicationPage activePage_ = UiApplicationPage::Song;
  bool initialized_ = false;
  std::array<UiApplicationPage,
             static_cast<std::size_t>(TrackerAction::Count)>
      pressOwners_{};
};

} // namespace ui2
