/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Theme/UiPalette.h"

#include <cstdint>
#include <string_view>

namespace ui2 {

enum class UiDialogKind : std::uint8_t {
  Message,
  TextInput,
  RenderProgress,
  FullScreen,
};

struct UiDialogViewData {
  UiDialogKind kind = UiDialogKind::Message;
  std::string_view title = "DIAGNOSTIC MESSAGE";
  std::string_view label = "NAME";
  std::string_view value = "ONECYCAC";
  std::string_view elapsed = "00:08";
  std::uint8_t progressWidth = 93;

  bool operator==(const UiDialogViewData &) const = default;
};

class UiDialogView {
public:
  // Normal dialogs append to the already-built page scene and suppress its
  // Bottom Bar. FullScreen replaces the scene entirely.
  [[nodiscard]] static UiBuildStatus Apply(const UiDialogViewData &data,
                                           UiFrameScene &scene);
  static void RenderDelta(const UiDialogViewData &previous,
                          const UiDialogViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface,
                          const UiPalette &palette);
  [[nodiscard]] static RectI16 DamageRect(UiDialogKind kind);
};

} // namespace ui2
