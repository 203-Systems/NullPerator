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
  OpenRecord,
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

  Ui2MixerCommand Handle(TrackerAction action, bool pressed) {
    if (!input_.Update(action, pressed) || !pressed)
      return {};
    if (input_.Held(TrackerAction::Nav)) {
      if (action == TrackerAction::Up)
        return {Ui2MixerCommandType::ReturnToSong, selected_};
      return {};
    }
    if (input_.Held(TrackerAction::Edit)) {
      if (action == TrackerAction::Play)
        return {Ui2MixerCommandType::OpenRecord, selected_};
      return {};
    }
    if (input_.Held(TrackerAction::Enter)) {
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
