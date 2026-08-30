/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Views/Ui2SampleSnapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

enum class SampleEditorViewUi2Focus : std::uint8_t {
  Name,
  Start,
  End,
  Operation,
  Apply,
  Save,
  SaveAndLoad,
  Discard,
  Waveform,
  Unknown,
};

// Fixed-capacity application-thread capture for UI2. It owns every displayed
// string and waveform byte; no renderer retains Variable, GraphField, or file
// data owned by the legacy view.
struct SampleEditorViewUi2Snapshot {
  std::array<char, 33> name{};
  std::array<char, 8> start{};
  std::array<char, 8> end{};
  std::array<char, 17> operation{};
  Ui2WaveformSnapshot waveform{};
  Ui2WaveformMarkersSnapshot<3> markers{};
  SampleEditorViewUi2Focus focus = SampleEditorViewUi2Focus::Unknown;
  std::uint8_t focusDigit = 0;
  bool waveformReady = false;
  bool playing = false;
  bool singleCycle = false;
  bool projectPool = false;
  // This remains fail-closed until the complete file mutation transaction is
  // implemented and connected through the application command handlers.
  bool fileMutationAvailable = false;
};

enum class SampleSlicesViewUi2Focus : std::uint8_t {
  Waveform,
  AutoSliceCount,
  AutoSlice,
  Unknown,
};

struct SampleSlicesViewUi2Snapshot {
  static constexpr std::size_t SliceCapacity = 16;

  std::array<char, 8> slice{};
  std::array<char, 8> start{};
  std::array<char, 8> zoom{};
  Ui2WaveformSnapshot waveform{};
  Ui2WaveformMarkersSnapshot<SliceCapacity + 1> markers{};
  SampleSlicesViewUi2Focus focus = SampleSlicesViewUi2Focus::Unknown;
  std::uint8_t selectedSlice = 0;
  std::uint8_t autoSliceCount = 0;
  bool waveformReady = false;
  bool hasSample = false;
  bool previewActive = false;
  bool previewPlayheadVisible = false;
  bool autoSliceApplyAvailable = true;
};

static_assert(std::is_trivially_copyable_v<SampleEditorViewUi2Snapshot>);
static_assert(std::is_trivially_copyable_v<SampleSlicesViewUi2Snapshot>);
static_assert(sizeof(SampleEditorViewUi2Snapshot) <= 1'000U);
static_assert(sizeof(SampleSlicesViewUi2Snapshot) <= 1'100U);
