#pragma once

#include "UI2/Views/Chain/UiChainView.h"

namespace ui2::test {

inline UiChainViewData ApprovedChainFixture() {
  UiChainViewData data;
  data.number = "00";
  data.phrases = {0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3A, 0xFF,
                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  data.transposes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
  data.trackNotes = {"D3", "C4", "--", "F2", "A3", "D#3", "C3", "G2"};
  data.vuLevelTop = {14, 34};
  data.editRow = 0;
  data.power = UiPowerState::BatteryNormal;
  return data;
}

} // namespace ui2::test
