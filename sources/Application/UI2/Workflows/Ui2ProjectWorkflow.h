/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Model/ProjectDefaults.h"
#include "Application/Model/Scale.h"
#include "Application/UI2/Controllers/Ui2ProjectController.h"
#include "Application/UI2/Ui2ProjectNamePresentation.h"
#include "Foundation/Types/Types.h"

#include <algorithm>
#include <cstdint>

namespace ui2 {

struct Ui2ProjectValuePlan final {
  FourCC variable = FourCC::VarTempo;
  int minimum = 0;
  int maximum = 0;
  int delta = 0;
  bool wraps = false;
  bool valid = false;
};

enum class Ui2ProjectSaveStart : std::uint8_t { SaveNow, RenameFirst };

// Tracks only the transient intent created when SAVE is requested for the
// reserved `.untitled` staging project. Persistence still owns Save As and
// overwrite semantics; this value merely joins the existing rename page back
// to the original SAVE action without allocating or leaking the internal name
// into the draft.
class Ui2DeferredProjectSave final {
public:
  [[nodiscard]] Ui2ProjectSaveStart Request(const char *storageName) {
    pendingRename_ =
        Ui2ProjectNamePresentation(storageName).NeedsNameBeforeSave();
    return pendingRename_ ? Ui2ProjectSaveStart::RenameFirst
                          : Ui2ProjectSaveStart::SaveNow;
  }

  [[nodiscard]] bool CompleteRename() {
    const bool shouldSave = pendingRename_;
    pendingRename_ = false;
    return shouldSave;
  }

  void Cancel() { pendingRename_ = false; }
  [[nodiscard]] bool PendingRename() const { return pendingRename_; }

private:
  bool pendingRename_ = false;
};

// Pure project-domain policy extracted from Ui2TrackerApplication. Keeping the
// plan as a small value makes it directly testable and avoids allocating a
// command/workflow object on embedded targets.
class Ui2ProjectWorkflow final {
public:
  [[nodiscard]] static constexpr Ui2ProjectValuePlan
  ValuePlan(Ui2ProjectCommand command) {
    switch (command.type) {
    case Ui2ProjectCommandType::AdjustTempo:
      return {FourCC::VarTempo, MIN_TEMPO, MAX_TEMPO, command.value, false,
              true};
    case Ui2ProjectCommandType::AdjustTranspose:
      return {FourCC::VarTranspose, -48, 48, command.value, false, true};
    case Ui2ProjectCommandType::AdjustScale:
      return {FourCC::VarScale, 0, numScales - 1, command.value, true, true};
    case Ui2ProjectCommandType::AdjustRoot:
      return {FourCC::VarScaleRoot, 0, 11, command.value, true, true};
    case Ui2ProjectCommandType::None:
    case Ui2ProjectCommandType::NewProject:
    case Ui2ProjectCommandType::LoadProject:
    case Ui2ProjectCommandType::SaveProject:
    case Ui2ProjectCommandType::RenameProject:
    case Ui2ProjectCommandType::BrowseSamplePool:
    case Ui2ProjectCommandType::RemoveUnusedSamples:
    case Ui2ProjectCommandType::RemoveUnusedInstruments:
    case Ui2ProjectCommandType::RenderMixdown:
    case Ui2ProjectCommandType::RenderStems:
      return {};
    }
    return {};
  }

  [[nodiscard]] static constexpr int
  ApplyValue(int current, const Ui2ProjectValuePlan &plan) {
    if (!plan.valid)
      return current;
    if (!plan.wraps)
      return std::clamp(current + plan.delta, plan.minimum, plan.maximum);

    const int count = plan.maximum - plan.minimum + 1;
    return ((current + plan.delta - plan.minimum) % count + count) % count +
           plan.minimum;
  }
};

static_assert(sizeof(Ui2ProjectValuePlan) <= 24U,
              "project value plans must remain small embedded values");
static_assert(sizeof(Ui2DeferredProjectSave) == 1U,
              "deferred project save state must remain a one-byte flag");

} // namespace ui2
