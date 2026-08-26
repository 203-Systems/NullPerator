/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"

#include <cstdint>

namespace ui2 {

enum class Ui2MixerCommandType : std::uint8_t {
  None,
  SelectChannel,
  AdjustVolume,
  StartPlayback,
  ToggleMute,
  ToggleSolo,
  UnmuteAll,
  ReturnToSong,
};

struct Ui2MixerCommand {
  Ui2MixerCommandType type = Ui2MixerCommandType::None;
  std::uint8_t channel = 0U;
  std::int8_t delta = 0;
};

class Ui2MixerController {
public:
  static constexpr std::uint8_t ChannelCount = 9U;

  [[nodiscard]] std::uint8_t SelectedChannel() const { return selected_; }
  constexpr void Synchronize(std::uint8_t channel) {
    selected_ = channel < ChannelCount ? channel : 0U;
  }

  Ui2MixerCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed) || !pressed)
      return {};
    if (action == TrackerAction::Play &&
        input_.Held(TrackerAction::Option)) {
      if (selected_ >= 8U && !input_.Held(TrackerAction::Shift))
        return {};
      return {input_.Held(TrackerAction::Shift)
                  ? Ui2MixerCommandType::UnmuteAll
                  : Ui2MixerCommandType::ToggleSolo,
              selected_};
    }
    if (action == TrackerAction::Shift &&
        input_.Held(TrackerAction::Option)) {
      if (selected_ >= 8U)
        return {};
      return {Ui2MixerCommandType::ToggleMute, selected_};
    }
    if (input_.Held(TrackerAction::Shift)) {
      if (action == TrackerAction::Up)
        return {Ui2MixerCommandType::ReturnToSong, selected_};
      return {};
    }
    if (input_.Held(TrackerAction::Option)) {
      return {};
    }
    if (input_.Held(TrackerAction::Edit)) {
      if (action == TrackerAction::Up || action == TrackerAction::Right)
        return {Ui2MixerCommandType::AdjustVolume, selected_, 1};
      if (action == TrackerAction::Down || action == TrackerAction::Left)
        return {Ui2MixerCommandType::AdjustVolume, selected_, -1};
      return {};
    }
    if (input_.AnyModifier())
      return {};
    if (action == TrackerAction::Left && selected_ > 0U) {
      --selected_;
      return {Ui2MixerCommandType::SelectChannel, selected_};
    }
    if (action == TrackerAction::Right && selected_ + 1U < ChannelCount) {
      ++selected_;
      return {Ui2MixerCommandType::SelectChannel, selected_};
    }
    if (action == TrackerAction::Play)
      return {Ui2MixerCommandType::StartPlayback, selected_};
    return {};
  }

private:
  Ui2ControllerInputState input_{};
  std::uint8_t selected_ = 0U;
};

} // namespace ui2
