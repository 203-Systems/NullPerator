/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace ui2 {

enum class Ui2RecordField : std::uint8_t { Source, LineGain, MicGain, Count };
enum class Ui2RecordCommandType : std::uint8_t {
  None,
  SetSource,
  SetLineGain,
  SetMicGain,
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
  constexpr Ui2RecordController(std::int8_t lineGainMinimum = 0,
                                std::int8_t lineGainMaximum = 0,
                                std::int8_t micGainMinimum = 0,
                                std::int8_t micGainMaximum = 0) {
    SetGainRanges(lineGainMinimum, lineGainMaximum, micGainMinimum,
                  micGainMaximum);
  }

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
  constexpr void SetGainRanges(std::int8_t lineGainMinimum,
                               std::int8_t lineGainMaximum,
                               std::int8_t micGainMinimum,
                               std::int8_t micGainMaximum) {
    lineGainMinimum_ = std::min(lineGainMinimum, lineGainMaximum);
    lineGainMaximum_ = std::max(lineGainMinimum, lineGainMaximum);
    micGainMinimum_ = std::min(micGainMinimum, micGainMaximum);
    micGainMaximum_ = std::max(micGainMinimum, micGainMaximum);
    lineGain_ = std::clamp(lineGain_, lineGainMinimum_, lineGainMaximum_);
    micGain_ = std::clamp(micGain_, micGainMinimum_, micGainMaximum_);
  }
  constexpr void Synchronize(std::uint8_t source, std::int8_t lineGain,
                             std::int8_t micGain) {
    source_ = source < 4U ? source : 0U;
    lineGain_ = std::clamp(lineGain, lineGainMinimum_, lineGainMaximum_);
    micGain_ = std::clamp(micGain, micGainMinimum_, micGainMaximum_);
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

    const bool enter = input_.Held(TrackerAction::Enter);
    if (!enter && action == TrackerAction::Up) {
      field_ = field_ == Ui2RecordField::Source
                   ? Ui2RecordField::MicGain
                   : static_cast<Ui2RecordField>(
                         static_cast<std::uint8_t>(field_) - 1U);
      return {};
    }
    if (!enter && action == TrackerAction::Down) {
      field_ = static_cast<Ui2RecordField>(
          (static_cast<std::uint8_t>(field_) + 1U) %
          static_cast<std::uint8_t>(Ui2RecordField::Count));
      return {};
    }
    const bool horizontal =
        action == TrackerAction::Left || action == TrackerAction::Right;
    const bool verticalEdit = enter &&
                              (action == TrackerAction::Up ||
                               action == TrackerAction::Down);
    if (!horizontal && !verticalEdit)
      return {};

    const int sign = action == TrackerAction::Left ||
                             action == TrackerAction::Down
                         ? -1
                         : 1;
    if (field_ == Ui2RecordField::Source) {
      source_ = static_cast<std::uint8_t>((source_ + sign + 4) % 4);
      return {.type = Ui2RecordCommandType::SetSource, .value = source_};
    }
    const int step = verticalEdit ? 2 : 1;
    std::int8_t &gain =
        field_ == Ui2RecordField::LineGain ? lineGain_ : micGain_;
    const std::int8_t minimum = field_ == Ui2RecordField::LineGain
                                    ? lineGainMinimum_
                                    : micGainMinimum_;
    const std::int8_t maximum = field_ == Ui2RecordField::LineGain
                                    ? lineGainMaximum_
                                    : micGainMaximum_;
    const std::int8_t adjusted = static_cast<std::int8_t>(
        std::clamp<int>(gain + sign * step, minimum, maximum));
    if (adjusted == gain)
      return {};
    gain = adjusted;
    return {.type = field_ == Ui2RecordField::LineGain
                        ? Ui2RecordCommandType::SetLineGain
                        : Ui2RecordCommandType::SetMicGain,
            .value = gain};
  }

private:
  Ui2ControllerInputState input_{};
  Ui2RecordField field_ = Ui2RecordField::Source;
  std::uint8_t source_ = 1U;
  std::int8_t lineGain_ = 0;
  std::int8_t micGain_ = 0;
  std::int8_t lineGainMinimum_ = 0;
  std::int8_t lineGainMaximum_ = 0;
  std::int8_t micGainMinimum_ = 0;
  std::int8_t micGainMaximum_ = 0;
  bool available_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2RecordController>);
static_assert(sizeof(Ui2RecordController) <= 16U);

} // namespace ui2
