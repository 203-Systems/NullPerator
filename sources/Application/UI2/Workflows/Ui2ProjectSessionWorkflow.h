/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "Application/UI2/Controllers/Ui2ProjectBrowserController.h"
#include "Application/UI2/Controllers/Ui2ProjectLifecycleController.h"
#include "Application/UI2/Ui2ProjectRenderBackend.h"
#include "Application/UI2/Workflows/Ui2ProjectWorkflow.h"

namespace ui2 {
class Ui2ProjectSessionWorkflow final {
  Ui2ProjectRenderBackend backend_;

public:
  Ui2ProjectSessionWorkflow(Project &project, TrackerSessionState &session)
      : backend_(project, session), render(backend_) {}
  Ui2ProjectSessionWorkflow(const Ui2ProjectSessionWorkflow &) = delete;
  Ui2ProjectSessionWorkflow &
  operator=(const Ui2ProjectSessionWorkflow &) = delete;
  void Reset() {
    render.Reset();
    controller = {};
    browser = {};
    lifecycle = {};
    input = {};
    deferredSave.Cancel();
    saveAsPending = false;
  }
  Ui2ProjectRenderController render;
  Ui2ProjectController controller{};
  Ui2ProjectBrowserController browser{};
  Ui2ProjectLifecycleController lifecycle{};
  Ui2ControllerInputState input{};
  Ui2DeferredProjectSave deferredSave{};
  bool saveAsPending = false;
};
} // namespace ui2
