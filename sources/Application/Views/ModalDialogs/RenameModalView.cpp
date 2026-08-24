/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "RenameModalView.h"

#include "Application/AppWindow.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "System/System/System.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>

namespace {

struct KeyboardRow {
  std::string_view keys;
  int start;
  int step;
};

constexpr std::array<KeyboardRow, 4> kKeyboard{{
    {"1234567890", 12, 23},
    {"QWERTYUIOP", 12, 23},
    {"ASDFGHJKL", 23, 24},
    {"ZXCVBNM", 43, 26},
}};

constexpr std::array<int, 5> kSpecialKeyCenters{26, 63, 120, 176, 213};
constexpr std::uint8_t kSpecialKeyboardRow = kKeyboard.size();
constexpr std::uint8_t kKeyboardRowCount = kKeyboard.size() + 1U;

constexpr std::array<std::string_view, 30> kAdjectives{
    "bad",  "mad",  "sad",  "big",  "hot",  "red",  "wet",  "low",
    "fat",  "thin", "cold", "high", "good", "sour", "sweet", "slow",
    "fast", "dark", "blue", "pink", "cyan", "loud", "snug", "long",
    "hard", "soft", "mean", "lost", "busy", "last"};

constexpr std::array<std::string_view, 100> kNouns{
    "sun", "sky", "car", "jet", "hut", "cat", "bat", "fox", "day", "bay",
    "ski", "egg", "pot", "pan", "box", "pie", "cap", "tie", "fog", "map",
    "fig", "toy", "jug", "bug", "mug", "paw", "arm", "sea", "dog", "ray",
    "bag", "log", "pin", "tea", "cow", "rug", "lab", "hub", "pub", "pea",
    "mop", "fee", "nib", "eel", "zen", "gas", "leg", "jam", "row", "air",
    "age", "art", "hat", "lip", "ink", "pad", "toe", "axe", "nut", "bar",
    "ivy", "dye", "ion", "dam", "ash", "peg", "hen", "cue", "spa", "ale",
    "owl", "bed", "oil", "cup", "tax", "van", "bid", "gap", "cut", "tip",
    "ace", "gig", "web", "spy", "rye", "ark", "rag", "set", "net", "bet",
    "bun", "pit", "era", "zoo", "tub", "gin", "app", "job", "elk", "ape"};

constexpr std::array<const char *, 3> kLegacyActionLabels{
    "Cancel", "Random", "Save"};

int KeyCenter(const KeyboardRow &row, std::uint8_t column) {
  return row.start + static_cast<int>(column) * row.step;
}

} // namespace

bool RenameModalView::inUse_ = false;
alignas(RenameModalView) static unsigned char
    RenameModalViewStorage[sizeof(RenameModalView)];
void *RenameModalView::storage_ = RenameModalViewStorage;

RenameModalView *RenameModalView::Create(View &view, const char *value,
                                         std::uint8_t maxLength) {
  if (inUse_) {
    auto *existing = reinterpret_cast<RenameModalView *>(storage_);
    existing->~RenameModalView();
    inUse_ = false;
  }
  inUse_ = true;
  return new (storage_) RenameModalView(view, value, maxLength);
}

RenameModalView::RenameModalView(View &view, const char *value,
                                 std::uint8_t maxLength)
    : ModalView(view), draft_(value == nullptr ? "" : value),
      maxLength_(std::min<std::uint8_t>(maxLength, DraftCapacity)) {
  if (draft_.size() > maxLength_)
    draft_.resize(maxLength_);
  if (draft_.empty())
    selectedAction_ = 1U;
}

void RenameModalView::Destroy() {
  this->~RenameModalView();
  inUse_ = false;
}

Ui2DialogSnapshot RenameModalView::SnapshotForUi2() const {
  Ui2DialogSnapshot snapshot;
  snapshot.kind = ui2::UiDialogKind::Rename;
  snapshot.SetTitle("RENAME");
  snapshot.SetLabel("NAME");
  snapshot.SetValue(draft_.c_str());
  snapshot.PushAction(ui2::UiDialogAction::Cancel);
  snapshot.PushAction(ui2::UiDialogAction::Random);
  snapshot.PushAction(ui2::UiDialogAction::Save);
  snapshot.SetSelectedAction(selectedAction_, focus_ == Focus::Actions);
  snapshot.saveEnabled = !draft_.empty();
  snapshot.SetRenameUppercase(uppercase_);
  const ui2::UiDialogFocus focus =
      focus_ == Focus::Input      ? ui2::UiDialogFocus::Input
      : focus_ == Focus::Keyboard ? ui2::UiDialogFocus::Keyboard
                                  : ui2::UiDialogFocus::Actions;
  snapshot.SetRenameFocus(focus, SelectedKeyIndex());
  return snapshot;
}

void RenameModalView::Append(char character) {
  if (draft_.size() >= maxLength_)
    return;
  draft_.push_back(character);
}

void RenameModalView::Backspace() {
  if (!draft_.empty())
    draft_.pop_back();
  if (draft_.empty() && selectedAction_ == 2U)
    selectedAction_ = 1U;
}

void RenameModalView::Randomize() {
  const std::uint32_t random = System::GetInstance()->GetRandomNumber();
  draft_.clear();
  const std::string_view adjective = kAdjectives[random % kAdjectives.size()];
  const std::string_view noun = kNouns[random % kNouns.size()];
  for (const char character : adjective)
    Append(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
  Append('-');
  for (const char character : noun)
    Append(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
}

std::uint8_t RenameModalView::SelectedKeyIndex() const {
  std::uint8_t index = 0U;
  for (std::uint8_t row = 0; row < keyboardRow_; ++row)
    index = static_cast<std::uint8_t>(index + kKeyboard[row].keys.size());
  return static_cast<std::uint8_t>(index + keyboardColumn_);
}

std::uint8_t RenameModalView::KeyboardRowLength() const {
  return keyboardRow_ == kSpecialKeyboardRow
             ? static_cast<std::uint8_t>(kSpecialKeyCenters.size())
             : static_cast<std::uint8_t>(kKeyboard[keyboardRow_].keys.size());
}

void RenameModalView::ActivateKey() {
  if (keyboardRow_ == kSpecialKeyboardRow) {
    if (keyboardColumn_ == 0U)
      uppercase_ = !uppercase_;
    else if (keyboardColumn_ == 1U)
      Append('-');
    else if (keyboardColumn_ == 2U)
      Append(' ');
    else if (keyboardColumn_ == 3U)
      Append('_');
    else
      Backspace();
    return;
  }

  char key = kKeyboard[keyboardRow_].keys[keyboardColumn_];
  if (!uppercase_ && key >= 'A' && key <= 'Z')
    key = static_cast<char>(key + ('a' - 'A'));
  Append(key);
}

void RenameModalView::MoveKeyboardVertical(int direction) {
  const int nextRow = static_cast<int>(keyboardRow_) + direction;
  if (nextRow < 0) {
    focus_ = Focus::Input;
    return;
  }
  if (nextRow >= static_cast<int>(kKeyboardRowCount)) {
    focus_ = Focus::Actions;
    return;
  }
  const int center = keyboardRow_ == kSpecialKeyboardRow
                         ? kSpecialKeyCenters[keyboardColumn_]
                         : KeyCenter(kKeyboard[keyboardRow_], keyboardColumn_);
  keyboardRow_ = static_cast<std::uint8_t>(nextRow);
  int closestDistance = 1000;
  std::uint8_t closest = 0U;
  for (std::uint8_t column = 0; column < KeyboardRowLength(); ++column) {
    const int keyCenter = keyboardRow_ == kSpecialKeyboardRow
                              ? kSpecialKeyCenters[column]
                              : KeyCenter(kKeyboard[keyboardRow_], column);
    const int distance = std::abs(keyCenter - center);
    if (distance < closestDistance) {
      closestDistance = distance;
      closest = column;
    }
  }
  keyboardColumn_ = closest;
}

void RenameModalView::MoveFromActionsToKeyboard() {
  keyboardRow_ = kSpecialKeyboardRow;
  const int actionCenter = selectedAction_ == 0U ? 40
                           : selectedAction_ == 1U ? 120
                                                   : 197;
  int closestDistance = 1000;
  for (std::uint8_t column = 0; column < kSpecialKeyCenters.size(); ++column) {
    const int distance = std::abs(kSpecialKeyCenters[column] - actionCenter);
    if (distance < closestDistance) {
      closestDistance = distance;
      keyboardColumn_ = column;
    }
  }
  focus_ = Focus::Keyboard;
}

void RenameModalView::MoveAction(int direction) {
  if (draft_.empty()) {
    selectedAction_ = selectedAction_ == 0U ? 1U : 0U;
    return;
  }
  const int next = (static_cast<int>(selectedAction_) + direction + 3) % 3;
  selectedAction_ = static_cast<std::uint8_t>(next);
}

void RenameModalView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed)
    return;

  if (focus_ == Focus::Input) {
    if (mask & EPBM_EDIT) {
      Backspace();
    } else if (mask & (EPBM_DOWN | EPBM_ENTER)) {
      focus_ = Focus::Keyboard;
    } else if (mask & EPBM_UP) {
      focus_ = Focus::Actions;
    }
  } else if (focus_ == Focus::Keyboard) {
    if (mask & EPBM_EDIT) {
      Backspace();
    } else if (mask & EPBM_ENTER) {
      ActivateKey();
    } else if (mask & EPBM_LEFT) {
      keyboardColumn_ = keyboardColumn_ == 0U
                            ? static_cast<std::uint8_t>(KeyboardRowLength() - 1U)
                            : static_cast<std::uint8_t>(keyboardColumn_ - 1U);
    } else if (mask & EPBM_RIGHT) {
      keyboardColumn_ = static_cast<std::uint8_t>(
          (keyboardColumn_ + 1U) % KeyboardRowLength());
    } else if (mask & EPBM_UP) {
      MoveKeyboardVertical(-1);
    } else if (mask & EPBM_DOWN) {
      MoveKeyboardVertical(1);
    }
  } else {
    if (mask & EPBM_LEFT) {
      MoveAction(-1);
    } else if (mask & EPBM_RIGHT) {
      MoveAction(1);
    } else if (mask & EPBM_UP) {
      MoveFromActionsToKeyboard();
    } else if (mask & EPBM_DOWN) {
      focus_ = Focus::Input;
    } else if (mask & EPBM_EDIT) {
      Backspace();
      focus_ = Focus::Input;
    } else if (mask & EPBM_ENTER) {
      if (selectedAction_ == 0U) {
        EndModal(MBL_CANCEL);
      } else if (selectedAction_ == 1U) {
        Randomize();
      } else if (!draft_.empty()) {
        EndModal(SaveReturnCode);
      }
    }
  }
  isDirty_ = true;
}

void RenameModalView::DrawView() {
  SetWindow(28, 18);
  GUITextProperties properties;
  SetColor(CD_NORMAL);
  DrawString(11, 0, "Rename", properties);
  DrawString(1, 2, "Name", properties);
  DrawString(7, 2, draft_.c_str(), properties);

  for (std::uint8_t rowIndex = 0; rowIndex < kKeyboard.size(); ++rowIndex) {
    const std::string_view keys = kKeyboard[rowIndex].keys;
    char line[16]{};
    std::memcpy(line, keys.data(), keys.size());
    DrawString(static_cast<int>((28 - keys.size()) / 2), 4 + rowIndex * 2,
               line, properties);
  }
  DrawString(1, 12, uppercase_ ? "ABC" : "abc", properties);
  DrawString(6, 12, "-", properties);
  DrawString(10, 12, "SPACE", properties);
  DrawString(18, 12, "_", properties);
  DrawString(23, 12, "DEL", properties);
  for (std::uint8_t index = 0; index < kLegacyActionLabels.size(); ++index) {
    properties.invert_ = focus_ == Focus::Actions && index == selectedAction_;
    DrawString(2 + index * 9, 16, kLegacyActionLabels[index], properties);
  }
}
