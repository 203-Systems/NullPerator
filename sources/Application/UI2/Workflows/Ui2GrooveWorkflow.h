/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2GrooveController.h"

#include <array>

namespace ui2 {

struct Ui2GrooveWorkflowResult final {
  bool projectMutated = false;
  bool interpolationApplied = false;
  bool selectNumber = false;
  bool dispatchPerformance = false;
};

struct Ui2GrooveClipboard final {
  std::array<std::uint8_t, Ui2GrooveController::RowCount> steps{};
  std::uint8_t count = 0U;
};

// Executes the data-only part of the Groove workflow. Transport and editor
// selection stay at the application boundary, while step mutation is now a
// deterministic, allocation-free unit that can be tested independently.
class Ui2GrooveWorkflow final {
public:
  [[nodiscard]] static constexpr Ui2GrooveWorkflowResult
  Execute(Ui2GrooveCommand command, std::uint8_t *steps) {
    Ui2GrooveClipboard clipboard;
    return Execute(command, steps, clipboard);
  }

  [[nodiscard]] static constexpr Ui2GrooveWorkflowResult
  Execute(Ui2GrooveCommand command, std::uint8_t *steps,
          Ui2GrooveClipboard &clipboard) {
    if (!command.HasValue() || steps == nullptr)
      return {};

    switch (command.type) {
    case Ui2GrooveCommandType::InitializeStep:
      if (command.row >= Ui2GrooveController::RowCount)
        return {};
      if (steps[command.row] != Ui2GrooveStepPolicy::Empty)
        return {};
      steps[command.row] = Ui2GrooveStepPolicy::Initial;
      return {.projectMutated = true};
    case Ui2GrooveCommandType::ClearStep:
      if (command.row >= Ui2GrooveController::RowCount)
        return {};
      if (steps[command.row] == Ui2GrooveStepPolicy::Empty)
        return {};
      steps[command.row] = Ui2GrooveStepPolicy::Empty;
      return {.projectMutated = true};
    case Ui2GrooveCommandType::AdjustStep: {
      if (command.row >= Ui2GrooveController::RowCount)
        return {};
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
    case Ui2GrooveCommandType::CopySelection:
      CopySelection(command.selection, steps, clipboard);
      return {};
    case Ui2GrooveCommandType::CutSelection: {
      std::uint8_t top = 0U;
      std::uint8_t bottom = 0U;
      if (!ResolveSelection(command.selection, top, bottom))
        return {};
      CopySelection(command.selection, steps, clipboard);
      bool mutated = false;
      for (std::uint8_t row = top; row <= bottom; ++row) {
        mutated = mutated || steps[row] != Ui2GrooveStepPolicy::Empty;
        steps[row] = Ui2GrooveStepPolicy::Empty;
      }
      return {.projectMutated = mutated};
    }
    case Ui2GrooveCommandType::PasteSelection: {
      if (command.row >= Ui2GrooveController::RowCount ||
          clipboard.count == 0U)
        return {};
      bool mutated = false;
      for (std::uint8_t index = 0U;
           index < clipboard.count &&
           static_cast<unsigned>(command.row) + index <
               Ui2GrooveController::RowCount;
           ++index) {
        const std::uint8_t row =
            static_cast<std::uint8_t>(command.row + index);
        mutated = mutated || steps[row] != clipboard.steps[index];
        steps[row] = clipboard.steps[index];
      }
      return {.projectMutated = mutated};
    }
    case Ui2GrooveCommandType::InterpolateSelection: {
      std::uint8_t top = 0U;
      std::uint8_t bottom = 0U;
      if (!ResolveSelection(command.selection, top, bottom) || top == bottom ||
          steps[top] == Ui2GrooveStepPolicy::Empty ||
          steps[bottom] == Ui2GrooveStepPolicy::Empty)
        return {};
      const unsigned span = static_cast<unsigned>(bottom - top);
      const unsigned start = steps[top];
      const unsigned end = steps[bottom];
      bool mutated = false;
      for (std::uint8_t row = static_cast<std::uint8_t>(top + 1U);
           row < bottom; ++row) {
        const unsigned offset = static_cast<unsigned>(row - top);
        const unsigned weighted = start * (span - offset) + end * offset;
        const std::uint8_t value =
            static_cast<std::uint8_t>((weighted + span / 2U) / span);
        mutated = mutated || steps[row] != value;
        steps[row] = value;
      }
      return {.projectMutated = mutated, .interpolationApplied = true};
    }
    case Ui2GrooveCommandType::SelectNumber:
      return {.selectNumber = true};
    case Ui2GrooveCommandType::StartPlayback:
    case Ui2GrooveCommandType::ToggleMute:
    case Ui2GrooveCommandType::ToggleSolo:
    case Ui2GrooveCommandType::UnmuteAll:
      return {.dispatchPerformance = true};
    case Ui2GrooveCommandType::None:
      return {};
    }
    return {};
  }

private:
  [[nodiscard]] static constexpr bool
  ResolveSelection(const Ui2GridSelectionState &selection, std::uint8_t &top,
                   std::uint8_t &bottom) {
    if (!selection.active || selection.anchorColumn != 0U ||
        selection.activeColumn != 0U ||
        selection.anchorRow >= Ui2GrooveController::RowCount ||
        selection.activeRow >= Ui2GrooveController::RowCount)
      return false;
    top = selection.Top();
    bottom = selection.Bottom();
    return true;
  }

  static constexpr void CopySelection(const Ui2GridSelectionState &selection,
                                      const std::uint8_t *steps,
                                      Ui2GrooveClipboard &clipboard) {
    std::uint8_t top = 0U;
    std::uint8_t bottom = 0U;
    if (!ResolveSelection(selection, top, bottom))
      return;
    clipboard.count = static_cast<std::uint8_t>(bottom - top + 1U);
    for (std::uint8_t index = 0U; index < clipboard.count; ++index)
      clipboard.steps[index] = steps[top + index];
  }
};

static_assert(std::is_trivially_copyable_v<Ui2GrooveClipboard>);

} // namespace ui2
