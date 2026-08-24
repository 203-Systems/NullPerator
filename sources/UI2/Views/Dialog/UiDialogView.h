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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ui2 {

enum class UiDialogKind : std::uint8_t {
  Message,
  TextInput,
  RenderProgress,
  FullScreen,
  Rename,
};

enum class UiDialogAction : std::uint8_t {
  Ok,
  Yes,
  Cancel,
  No,
  Save,
  Random,
};

enum class UiDialogFocus : std::uint8_t {
  Input,
  Keyboard,
  Actions,
};

inline constexpr std::size_t kUiDialogActionCapacity = 4U;

struct UiDialogViewData {
  UiDialogKind kind = UiDialogKind::Message;
  std::string_view title = "DIAGNOSTIC MESSAGE";
  std::string_view label = "NAME";
  std::string_view value = "ONECYCAC";
  std::string_view elapsed = "00:08";
  std::uint8_t progressWidth = 93;
  std::array<UiDialogAction, kUiDialogActionCapacity> actions{};
  std::uint8_t actionCount = 0;
  std::uint8_t selectedAction = 0;
  std::uint8_t selectedKey = 0;
  RectI16 cursorVisualRect{};
  bool actionsFocused = true;
  bool saveEnabled = true;
  bool uppercase = true;
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiDialogFocus focus = UiDialogFocus::Actions;

  bool operator==(const UiDialogViewData &) const = default;
};

class UiDialogView {
public:
  // Normal dialogs use the fixed overlay scene and suppress the page Bottom
  // Bar. FullScreen and Rename replace the page scene entirely.
  [[nodiscard]] static UiBuildStatus Apply(const UiDialogViewData &data,
                                           UiFrameScene &scene);
  static void RenderDelta(const UiDialogViewData &previous,
                          const UiDialogViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface,
                          const UiPalette &palette);
  [[nodiscard]] static RectI16 DamageRect(UiDialogKind kind);
  [[nodiscard]] static RectI16
  CursorTargetRect(const UiDialogViewData &data);
};

} // namespace ui2
