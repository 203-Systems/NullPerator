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
      if (command.synchronized &&
          steps[command.row] != Ui2GrooveStepPolicy::Empty) {
        // M8 coarse Groove editing treats even/odd rows as a timing pair:
        // 06/06 + UP on row 0 becomes 07/05, and UP on row 1 reverses the
        // emphasis. Preserve that pair sum atomically; at PicoTracker's 1..15
        // boundaries a partial adjustment would change the phrase duration.
        const std::uint8_t pairedRow =
            static_cast<std::uint8_t>(command.row ^ 1U);
        if (steps[pairedRow] != Ui2GrooveStepPolicy::Empty) {
          const std::uint8_t paired = Ui2GrooveStepPolicy::Adjust(
              steps[pairedRow], static_cast<std::int16_t>(-command.value));
          const unsigned oldTotal =
              static_cast<unsigned>(steps[command.row]) + steps[pairedRow];
          const unsigned newTotal = static_cast<unsigned>(adjusted) + paired;
          if (newTotal != oldTotal)
            return {};
          steps[pairedRow] = paired;
        }
      }
      steps[command.row] = adjusted;
      return {.projectMutated = true};
    }
    case Ui2GrooveCommandType::SelectNumber:
      return {.selectNumber = true};
    case Ui2GrooveCommandType::StartPlayback:
    case Ui2GrooveCommandType::ToggleSolo:
    case Ui2GrooveCommandType::UnmuteAll:
      return {.dispatchPerformance = true};
    case Ui2GrooveCommandType::None:
      return {};
    }
    return {};
  }
};

} // namespace ui2
