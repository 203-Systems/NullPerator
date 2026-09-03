/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Views/Record/UiRecordView.h"

#include <array>
#include <cstdint>
#include <type_traits>

enum class RecordViewUi2Focus : std::uint8_t { Source, Unknown };

enum class RecordViewUi2State : std::uint8_t {
  Idle,
  Recording,
  Saving,
};

// Fixed-capacity application-thread state for the Record page. ViewData()
// returns a short-lived renderer projection whose string_views borrow this
// snapshot, so the snapshot must outlive scene construction.
struct RecordViewUi2Snapshot {
  std::array<char, 17> source{};
  std::array<char, 6> elapsed{};
  RecordViewUi2Focus focus = RecordViewUi2Focus::Unknown;
  RecordViewUi2State state = RecordViewUi2State::Idle;
  std::uint8_t sourceIndex = 0;
  std::uint8_t savingPercent = 0;
  std::uint16_t meterSafeWidth = 0;
  std::uint16_t meterWarningWidth = 0;
  bool meterAvailable = false;
  bool recordingAvailable = false;
  bool sourceSelectable = true;

  bool operator==(const RecordViewUi2Snapshot &) const = default;

  [[nodiscard]] ui2::UiRecordViewData
  ViewData(ui2::UiPowerState power =
               ui2::UiPowerState::BatteryNormal) const {
    ui2::UiRecordViewData data;
    data.source = source.data();
    data.elapsed = elapsed.data();
    data.savingPercent = savingPercent;
    data.meterAvailable = meterAvailable;
    data.sourceSelectable = sourceSelectable;
    data.safeWidth = meterAvailable ? meterSafeWidth : 0U;
    data.warningWidth = meterAvailable ? meterWarningWidth : 0U;
    data.power = power;

    switch (focus) {
    case RecordViewUi2Focus::Source:
      data.focus = ui2::UiRecordFocus::Source;
      break;
    case RecordViewUi2Focus::Unknown:
      data.focus = ui2::UiRecordFocus::None;
      data.cursorInkVisible = false;
      break;
    }

    if (!recordingAvailable) {
      data.state = ui2::UiRecordState::Unavailable;
      data.focus = ui2::UiRecordFocus::None;
      data.cursorInkVisible = false;
    } else {
      switch (state) {
      case RecordViewUi2State::Idle:
        data.state = ui2::UiRecordState::Armed;
        break;
      case RecordViewUi2State::Recording:
        data.state = ui2::UiRecordState::Recording;
        break;
      case RecordViewUi2State::Saving:
        data.state = ui2::UiRecordState::Saving;
        break;
      }
    }
    return data;
  }
};

static_assert(std::is_trivially_copyable_v<RecordViewUi2Snapshot>);
static_assert(std::is_trivially_destructible_v<RecordViewUi2Snapshot>);
static_assert(sizeof(RecordViewUi2Snapshot) <= 64U);
