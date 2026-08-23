/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Render/UiRasterizer.h"

#include "UI2/Text/UiFont5x7.h"

namespace ui2 {

void UiRasterizer::Render(UiCommandStream stream, UiIndexedSurface &surface,
                          const UiPalette *palette, PointI16 origin,
                          RectI16 clip) {
  for (const UiCommand &command : stream.commands) {
    RectI16 bounds = command.bounds;
    bounds.x = static_cast<std::int16_t>(bounds.x + origin.x);
    bounds.y = static_cast<std::int16_t>(bounds.y + origin.y);
    switch (command.kind) {
    case UiCommandKind::FillRect:
      surface.FillRect(bounds, command.color, clip);
      break;
    case UiCommandKind::FillRoundedRect:
      surface.FillRoundedRect(bounds, command.color, command.auxiliaryColor,
                              command.parameter, clip);
      break;
    case UiCommandKind::FillCoverageRoundedRect:
      if (palette != nullptr) {
        surface.FillCoverageRoundedRect(
            bounds, command.color, *palette,
            static_cast<UiCoverage>(command.auxiliaryColor), command.parameter,
            clip);
      } else {
        surface.FillRoundedRect(bounds, command.color, command.color,
                                command.parameter, clip);
      }
      break;
    case UiCommandKind::FillVerticalPaletteRamp:
      for (std::int16_t row = 0; row < bounds.height; ++row) {
        surface.FillRect(
            {bounds.x, static_cast<std::int16_t>(bounds.y + row), bounds.width,
             1},
            static_cast<PaletteIndex>(command.color + row), clip);
      }
      break;
    case UiCommandKind::Text: {
      const std::size_t length = command.auxiliaryColor;
      if (command.payload > stream.text.size() ||
          length > stream.text.size() - command.payload) {
        break;
      }
      PointI16 glyphOrigin{bounds.x, bounds.y};
      for (const char character :
           stream.text.subspan(command.payload, length)) {
        surface.DrawGlyph5x7(glyphOrigin, UiFont5x7::Glyph(character),
                             command.color, command.parameter, clip);
        glyphOrigin.x = static_cast<std::int16_t>(
            glyphOrigin.x + UiFont5x7::kAdvance * command.parameter);
      }
      break;
    }
    }
  }
}

} // namespace ui2
