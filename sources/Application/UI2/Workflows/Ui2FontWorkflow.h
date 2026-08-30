/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2FontController.h"
#include "Foundation/Variables/Variable.h"

#include <algorithm>
#include <cstdint>

namespace ui2 {

enum class Ui2FontWorkflowResult : std::uint8_t {
  None,
  TextCaseChanged,
  BrowserUnavailable,
  DefaultRestored,
  ConfigUnavailable,
};

inline Ui2FontWorkflowResult Ui2ExecuteFontCommand(Ui2FontCommand command,
                                                    Variable *configured) {
  switch (command.type) {
  case Ui2FontCommandType::None:
    return Ui2FontWorkflowResult::None;
  case Ui2FontCommandType::SetTextCase: {
    if (configured == nullptr ||
        configured->GetID() != FourCC::VarUITextCase)
      return Ui2FontWorkflowResult::ConfigUnavailable;
    const int value = std::min<int>(command.value, 2);
    if (configured->GetInt() == value)
      return Ui2FontWorkflowResult::None;
    configured->SetInt(value);
    return Ui2FontWorkflowResult::TextCaseChanged;
  }
  case Ui2FontCommandType::BrowseFont:
    // NPF discovery and parsing do not exist yet. This command deliberately
    // has no model mutation and no Browser transition.
    return Ui2FontWorkflowResult::BrowserUnavailable;
  case Ui2FontCommandType::RestoreDefault:
    if (configured == nullptr || configured->GetID() != FourCC::VarUIFont)
      return Ui2FontWorkflowResult::ConfigUnavailable;
    if (!configured->IsModified())
      return Ui2FontWorkflowResult::None;
    configured->Reset();
    return Ui2FontWorkflowResult::DefaultRestored;
  }
  return Ui2FontWorkflowResult::None;
}

} // namespace ui2
