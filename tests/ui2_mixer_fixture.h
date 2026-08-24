#pragma once

#include "UI2/Views/Mixer/UiMixerView.h"

namespace ui2::test {

inline UiMixerViewData ApprovedMixerFixture() {
  UiMixerViewData data;
  data.vuLevelTop = {{{70, 65},
                      {28, 33},
                      {51, 47},
                      {14, 19},
                      {40, 37},
                      {61, 56},
                      {34, 39},
                      {77, 71},
                      {21, 33}}};
  data.volumes = {"99", "92", "86", "80", "74", "68", "62", "56", "60"};
  data.selectedChannel = 0;
  data.power = UiPowerState::BatteryNormal;
  return data;
}

} // namespace ui2::test
