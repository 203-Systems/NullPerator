/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Model/ProjectDefaults.h"
#include "Application/Model/Scale.h"
#include "Application/UI2/Controllers/Ui2ProjectController.h"
#include "Foundation/Types/Types.h"

#include <algorithm>

namespace ui2 {

struct Ui2ProjectValuePlan final {
  FourCC variable = FourCC::VarTempo;
  int minimum = 0;
  int maximum = 0;
  int delta = 0;
  bool wraps = false;
  bool valid = false;
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

} // namespace ui2
