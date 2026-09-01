/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"

#include <cstdint>
#include <type_traits>

namespace ui2 {

enum class Ui2RecordField : std::uint8_t { Source };
enum class Ui2RecordCommandType : std::uint8_t {
  None,
  SetSource,
  ToggleRecording,
};

struct Ui2RecordCommand {
  Ui2RecordCommandType type = Ui2RecordCommandType::None;
  std::int16_t value = 0;
  [[nodiscard]] constexpr bool HasValue() const {
    return type != Ui2RecordCommandType::None;
  }
};

class Ui2RecordController {
public:
  [[nodiscard]] constexpr Ui2RecordField SelectedField() const {
    return field_;
  }
  [[nodiscard]] constexpr bool Available() const { return available_; }
  constexpr void SetAvailable(bool available) {
    if (available_ == available)
      return;
    available_ = available;
    input_ = {};
  }
  constexpr void Synchronize(std::uint8_t source) {
    source_ = source < 3U ? source : 0U;
  }

  Ui2RecordCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed))
      return {};
    if (!available_ || !pressed)
      return {};
    if (input_.Held(TrackerAction::Shift) || input_.Held(TrackerAction::Option))
      return {};
    if (action == TrackerAction::Play)
      return {.type = Ui2RecordCommandType::ToggleRecording};

    if (action != TrackerAction::Left && action != TrackerAction::Right)
      return {};

    const int sign = action == TrackerAction::Left ? -1 : 1;
    source_ = static_cast<std::uint8_t>((source_ + sign + 3) % 3);
    return {.type = Ui2RecordCommandType::SetSource, .value = source_};
  }

private:
  Ui2ControllerInputState input_{};
  Ui2RecordField field_ = Ui2RecordField::Source;
  std::uint8_t source_ = 0U;
  bool available_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2RecordController>);
static_assert(sizeof(Ui2RecordController) <= 16U);

} // namespace ui2
