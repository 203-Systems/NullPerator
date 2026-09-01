#pragma once

#include "UI2/Views/Groove/UiGrooveView.h"

namespace ui2::test {

inline UiGrooveViewData ApprovedGrooveFixture() {
  UiGrooveViewData data;
  data.number = "00";
  data.steps.fill(0xFFU);
  data.steps[0] = 0x06;
  data.steps[1] = 0x06;
  data.trackNotes = {"D3", "C4", "--", "F2", "A3", "D#3", "C3", "G2"};
  data.editRow = 0;
  data.power = UiPowerState::BatteryNormal;
  return data;
}

} // namespace ui2::test
