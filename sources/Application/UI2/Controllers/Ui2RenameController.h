/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace ui2 {

enum class Ui2RenameCommand : std::uint8_t { None, Cancel, Randomize, Save };

// Allocation-free controller for the full-screen UI2 rename page.
class Ui2RenameController {
public:
  static constexpr std::uint8_t DraftCapacity = 20U;
  using SaveValidator = bool (*)(const char *value);

  void Begin(const char *value, std::uint8_t maximumLength = 16U,
             SaveValidator saveValidator = nullptr,
             TrackerAction trigger = TrackerAction::Count) {
    maximumLength_ = std::min(maximumLength, DraftCapacity);
    saveValidator_ = saveValidator;
    draft_.fill('\0');
    const char *source = value == nullptr ? "" : value;
    length_ = static_cast<std::uint8_t>(
        std::min<std::size_t>(std::strlen(source), maximumLength_));
    std::copy_n(source, length_, draft_.begin());
    keyboardRow_ = keyboardColumn_ = 0U;
    selectedAction_ = CanSave() ? 2U : 1U;
    uppercase_ = true;
    focus_ = Focus::Input;
    active_ = true;
    ++instanceId_;
    input_ = {};
    releaseGate_.BlockUntilRelease(trigger);
  }

  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] const char *Value() const { return draft_.data(); }
  [[nodiscard]] std::uint32_t InstanceId() const { return instanceId_; }

  Ui2RenameCommand Handle(TrackerAction action, bool pressed) {
    if (!active_ || !input_.Update(action, pressed) ||
        !releaseGate_.Update(action, pressed) || !pressed)
      return Ui2RenameCommand::None;
    if (action == TrackerAction::Shift) {
      uppercase_ = !uppercase_;
      return Ui2RenameCommand::None;
    }
    if (focus_ == Focus::Input) {
      if (action == TrackerAction::Option)
        Backspace();
      else if (action == TrackerAction::Down || action == TrackerAction::Enter)
        focus_ = Focus::Keyboard;
      else if (action == TrackerAction::Up)
        focus_ = Focus::Actions;
      return Ui2RenameCommand::None;
    }
    if (focus_ == Focus::Keyboard) {
      if (action == TrackerAction::Option)
        Backspace();
      else if (action == TrackerAction::Enter)
        ActivateKey();
      else if (action == TrackerAction::Left)
        keyboardColumn_ = keyboardColumn_ == 0U
                              ? static_cast<std::uint8_t>(RowLength() - 1U)
                              : static_cast<std::uint8_t>(keyboardColumn_ - 1U);
      else if (action == TrackerAction::Right)
        keyboardColumn_ =
            static_cast<std::uint8_t>((keyboardColumn_ + 1U) % RowLength());
      else if (action == TrackerAction::Up)
        MoveVertical(-1);
      else if (action == TrackerAction::Down)
        MoveVertical(1);
      return Ui2RenameCommand::None;
    }
    if (action == TrackerAction::Left)
      MoveAction(-1);
    else if (action == TrackerAction::Right)
      MoveAction(1);
    else if (action == TrackerAction::Up)
      MoveActionsToKeyboard();
    else if (action == TrackerAction::Down)
      focus_ = Focus::Input;
    else if (action == TrackerAction::Option) {
      Backspace();
      focus_ = Focus::Input;
    } else if (action == TrackerAction::Enter) {
      if (selectedAction_ == 0U) {
        active_ = false;
        releaseGate_.Reset();
        return Ui2RenameCommand::Cancel;
      }
      if (selectedAction_ == 1U)
        return Ui2RenameCommand::Randomize;
      if (CanSave()) {
        active_ = false;
        releaseGate_.Reset();
        return Ui2RenameCommand::Save;
      }
    }
    return Ui2RenameCommand::None;
  }

  void Randomize(std::uint32_t seed) {
    static constexpr std::array<std::string_view, 12> adjectives{
        "FAST", "DARK", "LOUD", "SOFT", "LOST", "RED",
        "COLD", "HIGH", "LOW",  "WILD", "ODD",  "DEEP"};
    static constexpr std::array<std::string_view, 16> nouns{
        "SUN", "SKY", "FOX", "WAVE", "BEAT", "TONE", "LOOP", "GRID",
        "BASS", "KICK", "SNARE", "SYNTH", "BYTE", "NODE", "ECHO", "TRACK"};
    length_ = 0U;
    draft_.fill('\0');
    Append(adjectives[seed % adjectives.size()]);
    Append('-');
    Append(nouns[(seed / adjectives.size()) % nouns.size()]);
    selectedAction_ = 2U;
  }

  [[nodiscard]] Ui2DialogSnapshot Snapshot() const {
    Ui2DialogSnapshot snapshot;
    snapshot.kind = UiDialogKind::Rename;
    snapshot.SetTitle("RENAME");
    snapshot.SetLabel("NAME");
    snapshot.SetValue(draft_.data());
    snapshot.PushAction(UiDialogAction::Cancel);
    snapshot.PushAction(UiDialogAction::Random);
    snapshot.PushAction(UiDialogAction::Save);
    snapshot.SetSelectedAction(selectedAction_, focus_ == Focus::Actions);
    snapshot.saveEnabled = CanSave();
    snapshot.SetRenameUppercase(uppercase_);
    snapshot.SetRenameFocus(
        focus_ == Focus::Input      ? UiDialogFocus::Input
        : focus_ == Focus::Keyboard ? UiDialogFocus::Keyboard
                                    : UiDialogFocus::Actions,
        SelectedKeyIndex());
    return snapshot;
  }

private:
  enum class Focus : std::uint8_t { Input, Keyboard, Actions };
  struct KeyboardRow {
    std::string_view keys;
    std::int16_t start;
    std::int16_t step;
  };
  static constexpr std::array<KeyboardRow, 4> Keyboard{{
      {"1234567890", 12, 23}, {"QWERTYUIOP", 12, 23},
      {"ASDFGHJKL", 23, 24}, {"ZXCVBNM", 43, 26},
  }};
  static constexpr std::array<std::int16_t, 5> SpecialCenters{26, 63, 120,
                                                              176, 213};
  static constexpr std::uint8_t SpecialRow = Keyboard.size();

  void Append(char character) {
    if (length_ >= maximumLength_)
      return;
    draft_[length_++] = character;
    draft_[length_] = '\0';
  }
  void Append(std::string_view text) {
    for (char character : text)
      Append(character);
  }
  void Backspace() {
    if (length_ != 0U)
      draft_[--length_] = '\0';
    if (!CanSave() && selectedAction_ == 2U)
      selectedAction_ = 1U;
  }
  void ActivateKey() {
    if (keyboardRow_ == SpecialRow) {
      if (keyboardColumn_ == 0U)
        uppercase_ = !uppercase_;
      else if (keyboardColumn_ == 1U)
        Append('-');
      else if (keyboardColumn_ == 2U)
        Append(' ');
      else if (keyboardColumn_ == 3U)
        Append('.');
      else
        Backspace();
      return;
    }
    char key = Keyboard[keyboardRow_].keys[keyboardColumn_];
    if (!uppercase_ && key >= 'A' && key <= 'Z')
      key = static_cast<char>(key + ('a' - 'A'));
    Append(key);
  }
  [[nodiscard]] std::uint8_t RowLength() const {
    return keyboardRow_ == SpecialRow
               ? static_cast<std::uint8_t>(SpecialCenters.size())
               : static_cast<std::uint8_t>(Keyboard[keyboardRow_].keys.size());
  }
  [[nodiscard]] std::int16_t Center(std::uint8_t column) const {
    return keyboardRow_ == SpecialRow
               ? SpecialCenters[column]
               : static_cast<std::int16_t>(Keyboard[keyboardRow_].start +
                                           column * Keyboard[keyboardRow_].step);
  }
  void MoveVertical(std::int8_t direction) {
    const int next = static_cast<int>(keyboardRow_) + direction;
    if (next < 0) {
      focus_ = Focus::Input;
      return;
    }
    if (next > SpecialRow) {
      focus_ = Focus::Actions;
      return;
    }
    const std::int16_t center = Center(keyboardColumn_);
    keyboardRow_ = static_cast<std::uint8_t>(next);
    SelectClosest(center);
  }
  void SelectClosest(std::int16_t center) {
    std::int16_t best = 32767;
    for (std::uint8_t column = 0; column < RowLength(); ++column) {
      const auto distance = static_cast<std::int16_t>(std::abs(Center(column) - center));
      if (distance < best) {
        best = distance;
        keyboardColumn_ = column;
      }
    }
  }
  void MoveActionsToKeyboard() {
    static constexpr std::array<std::int16_t, 3> centers{40, 120, 197};
    keyboardRow_ = SpecialRow;
    SelectClosest(centers[selectedAction_]);
    focus_ = Focus::Keyboard;
  }
  void MoveAction(std::int8_t direction) {
    if (!CanSave()) {
      selectedAction_ = selectedAction_ == 0U ? 1U : 0U;
      return;
    }
    selectedAction_ = static_cast<std::uint8_t>(
        (static_cast<int>(selectedAction_) + direction + 3) % 3);
  }
  [[nodiscard]] bool CanSave() const {
    // A visually empty name (spaces only) must behave exactly like an empty
    // draft: SAVE stays disabled and action navigation skips it.
    const bool visible =
        std::any_of(draft_.begin(), draft_.begin() + length_,
                    [](char character) { return character != ' '; });
    return visible &&
           (saveValidator_ == nullptr || saveValidator_(draft_.data()));
  }
  [[nodiscard]] std::uint8_t SelectedKeyIndex() const {
    std::uint8_t index = 0U;
    for (std::uint8_t row = 0; row < keyboardRow_; ++row)
      index = static_cast<std::uint8_t>(index + Keyboard[row].keys.size());
    return static_cast<std::uint8_t>(index + keyboardColumn_);
  }

  Ui2ControllerInputState input_{};
  Ui2InputReleaseGate releaseGate_{};
  std::array<char, DraftCapacity + 1U> draft_{};
  std::uint32_t instanceId_ = 0U;
  std::uint8_t length_ = 0U;
  std::uint8_t maximumLength_ = 16U;
  std::uint8_t keyboardRow_ = 0U;
  std::uint8_t keyboardColumn_ = 0U;
  std::uint8_t selectedAction_ = 2U;
  SaveValidator saveValidator_ = nullptr;
  Focus focus_ = Focus::Input;
  bool uppercase_ = true;
  bool active_ = false;
};

} // namespace ui2
