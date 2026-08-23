/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Render/UiRasterizer.h"

namespace ui2 {

void UiRasterizer::Render(std::span<const UiCommand> commands,
                          UiIndexedSurface &surface) {
  for (const UiCommand &command : commands) {
    switch (command.kind) {
    case UiCommandKind::FillRect:
      surface.FillRect(command.bounds, command.color);
      break;
    case UiCommandKind::FillRoundedRect:
      surface.FillRoundedRect(command.bounds, command.color,
                              command.auxiliary_color, command.radius);
      break;
    }
  }
}

} // namespace ui2
