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
    if (Intersect(bounds, clip).Empty())
      continue;
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
    case UiCommandKind::FillVerticalPaletteRamp: {
      // Damage rendering frequently clips a tall VU command to a one-tile
      // band. Walk only the visible rows so an ESP32 does not pay O(full
      // meter height) for every small level change.
      const RectI16 visibleRamp = Intersect(bounds, clip);
      if (visibleRamp.Empty())
        break;
      const std::int16_t firstRow =
          static_cast<std::int16_t>(visibleRamp.y - bounds.y);
      const std::int16_t lastRow =
          static_cast<std::int16_t>(visibleRamp.Bottom() - bounds.y);
      for (std::int16_t row = firstRow; row < lastRow; ++row) {
        surface.FillRect(
            {bounds.x, static_cast<std::int16_t>(bounds.y + row), bounds.width,
             1},
            static_cast<PaletteIndex>(command.color + row), clip);
      }
      break;
    }
    case UiCommandKind::SparseCoverageMask: {
      if (palette == nullptr || bounds.width <= 0 || bounds.height <= 0 ||
          command.payload > stream.text.size() ||
          stream.text.size() - command.payload < 2U) {
        break;
      }
      const auto byteAt = [&](std::size_t index) {
        return static_cast<std::uint8_t>(stream.text[index]);
      };
      const std::size_t length = byteAt(command.payload) |
                                 (byteAt(command.payload + 1U) << 8U);
      std::size_t cursor = command.payload + 2U;
      if (length > stream.text.size() - cursor) break;
      const std::size_t end = cursor + length;
      for (std::int16_t column = 0; column < command.bounds.width; ++column) {
        if (end - cursor < 2U) break;
        const std::uint8_t startY = byteAt(cursor++);
        const std::uint8_t runLength = byteAt(cursor++);
        if (startY == 0xFFU && runLength == 0U) continue;
        if (startY >= command.bounds.height || runLength == 0U ||
            runLength > command.bounds.height - startY) {
          break;
        }
        const std::size_t packedLength = (runLength + 3U) / 4U;
        if (packedLength > end - cursor) break;
        const std::int16_t x =
            static_cast<std::int16_t>(bounds.x + column);
        for (std::uint8_t row = 0; row < runLength; ++row) {
          const std::uint8_t packed = byteAt(cursor + row / 4U);
          const std::uint8_t quarterCoverage = static_cast<std::uint8_t>(
              ((packed >> ((row % 4U) * 2U)) & 0x03U) + 1U);
          const std::int16_t y =
              static_cast<std::int16_t>(bounds.y + startY + row);
          if (x >= clip.x && y >= clip.y && x < clip.Right() &&
              y < clip.Bottom()) {
            surface.SetPixel(
                x, y,
                palette->AntialiasIndex(
                    static_cast<UiCoverage>(command.auxiliaryColor),
                    quarterCoverage));
          }
        }
        cursor += packedLength;
      }
      break;
    }
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
