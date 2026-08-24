/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Views/BaseClasses/ModalView.h"
#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"
#include "Externals/etl/include/etl/string.h"

#include <cstddef>
#include <cstdint>

// Shared, fixed-capacity rename editor used by Project, Theme and Instrument.
// It intentionally supports append/backspace rather than a movable caret: the
// complete controller state is 8 bytes plus the 21-byte draft buffer.
class RenameModalView final : public ModalView {
public:
  static constexpr std::uint8_t DraftCapacity = 20U;
  static constexpr int SaveReturnCode = 100;

  static RenameModalView *Create(View &view, const char *value,
                                 std::uint8_t maxLength = 16U);
  ~RenameModalView() override = default;
  void Destroy() override;

  void DrawView() override;
  void ProcessButtonMask(unsigned short mask, bool pressed) override;
  void OnFocus() override {}
  void AnimationUpdate() override {}
  void OnPlayerUpdate(PlayerEventType, unsigned int) override {}

  [[nodiscard]] Ui2DialogSnapshot SnapshotForUi2() const override;
  [[nodiscard]] const char *Value() const { return draft_.c_str(); }

private:
  RenameModalView(View &view, const char *value, std::uint8_t maxLength);

  enum class Focus : std::uint8_t { Input, Keyboard, Actions };

  void Append(char character);
  void Backspace();
  void Randomize();
  void ActivateKey();
  void MoveKeyboardVertical(int direction);
  void MoveFromActionsToKeyboard();
  void MoveAction(int direction);
  [[nodiscard]] std::uint8_t KeyboardRowLength() const;
  [[nodiscard]] std::uint8_t SelectedKeyIndex() const;

  etl::string<DraftCapacity> draft_;
  std::uint8_t maxLength_ = 16U;
  std::uint8_t keyboardRow_ = 0U;
  std::uint8_t keyboardColumn_ = 0U;
  std::uint8_t selectedAction_ = 2U;
  bool uppercase_ = true;
  Focus focus_ = Focus::Input;

  static bool inUse_;
  static void *storage_;
};

inline constexpr std::size_t kRenameModalStorageLimit =
    sizeof(void *) == 4U ? 256U : 288U;
static_assert(sizeof(RenameModalView) <= kRenameModalStorageLimit);
