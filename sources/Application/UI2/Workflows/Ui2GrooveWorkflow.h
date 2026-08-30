/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2GrooveController.h"

namespace ui2 {

struct Ui2GrooveWorkflowResult final {
  bool projectMutated = false;
  bool selectNumber = false;
  bool dispatchPerformance = false;
};

// Executes the data-only part of the Groove workflow. Transport and editor
// selection stay at the application boundary, while step mutation is now a
// deterministic, allocation-free unit that can be tested independently.
class Ui2GrooveWorkflow final {
public:
  [[nodiscard]] static constexpr Ui2GrooveWorkflowResult
  Execute(Ui2GrooveCommand command, std::uint8_t *steps) {
    if (!command.HasValue() || steps == nullptr)
      return {};

    switch (command.type) {
    case Ui2GrooveCommandType::InitializeStep:
      if (steps[command.row] != Ui2GrooveStepPolicy::Empty)
        return {};
      steps[command.row] = Ui2GrooveStepPolicy::Initial;
      return {.projectMutated = true};
    case Ui2GrooveCommandType::ClearStep:
      if (steps[command.row] == Ui2GrooveStepPolicy::Empty)
        return {};
      steps[command.row] = Ui2GrooveStepPolicy::Empty;
      return {.projectMutated = true};
    case Ui2GrooveCommandType::AdjustStep: {
      const std::uint8_t adjusted =
          Ui2GrooveStepPolicy::Adjust(steps[command.row], command.value);
      if (adjusted == steps[command.row])
        return {};
      steps[command.row] = adjusted;
      return {.projectMutated = true};
    }
    case Ui2GrooveCommandType::SelectNumber:
      return {.selectNumber = true};
    case Ui2GrooveCommandType::StartPlayback:
      return {.dispatchPerformance = true};
    case Ui2GrooveCommandType::None:
      return {};
    }
    return {};
  }
};

} // namespace ui2
