/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Ui2SampleSnapshots.h"
#include "UI2/Views/Sample/UiSampleViews.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ui2 {

struct UiSampleControllerModifiers {
  bool enterHeld = false;
  bool editHeld = false;
};

namespace detail {

template <std::size_t Size>
inline std::string_view SampleCStringView(const std::array<char, Size> &text) {
  const auto end = std::find(text.begin(), text.end(), '\0');
  return {text.data(), static_cast<std::size_t>(end - text.begin())};
}

template <std::size_t Size>
inline void SetSampleText(std::array<char, Size> &destination,
                          std::string_view text) {
  static_assert(Size > 0U);
  destination.fill('\0');
  const std::size_t count = std::min(text.size(), Size - 1U);
  std::copy_n(text.begin(), count, destination.begin());
}

inline UiSampleWaveformMarkerKind
MapSampleMarkerKind(Ui2WaveformMarkerKind kind) {
  switch (kind) {
  case Ui2WaveformMarkerKind::Start:
    return UiSampleWaveformMarkerKind::Start;
  case Ui2WaveformMarkerKind::End:
    return UiSampleWaveformMarkerKind::End;
  case Ui2WaveformMarkerKind::Slice:
    return UiSampleWaveformMarkerKind::Slice;
  case Ui2WaveformMarkerKind::Playhead:
    return UiSampleWaveformMarkerKind::Playhead;
  }
  return UiSampleWaveformMarkerKind::Slice;
}

template <std::size_t Capacity>
inline std::uint8_t CopySampleMarkers(
    const Ui2WaveformMarkersSnapshot<Capacity> &source,
    std::array<UiSampleWaveformMarker, Capacity> &destination) {
  const std::uint8_t count = static_cast<std::uint8_t>(
      std::min<std::size_t>(source.count, destination.size()));
  for (std::uint8_t index = 0; index < count; ++index) {
    const Ui2WaveformMarkerSnapshot marker = source.markers[index];
    destination[index] = {
        marker.x, MapSampleMarkerKind(marker.kind), marker.selected};
  }
  return count;
}

inline bool EqualEditorCapture(const SampleEditorViewUi2Snapshot &left,
                               const SampleEditorViewUi2Snapshot &right) {
  return left.name == right.name && left.start == right.start &&
         left.end == right.end && left.operation == right.operation &&
         left.waveform.size == right.waveform.size &&
         left.waveform.revision == right.waveform.revision &&
         left.markers.markers == right.markers.markers &&
         left.markers.count == right.markers.count &&
         left.focus == right.focus && left.focusDigit == right.focusDigit &&
         left.waveformReady == right.waveformReady &&
         left.playing == right.playing &&
         left.singleCycle == right.singleCycle &&
         left.projectPool == right.projectPool;
}

inline bool EqualSlicesCapture(const SampleSlicesViewUi2Snapshot &left,
                               const SampleSlicesViewUi2Snapshot &right) {
  return left.sliceCount == right.sliceCount && left.slice == right.slice &&
         left.start == right.start && left.zoom == right.zoom &&
         left.waveform.size == right.waveform.size &&
         left.waveform.revision == right.waveform.revision &&
         left.markers.markers == right.markers.markers &&
         left.markers.count == right.markers.count &&
         left.focus == right.focus &&
         left.selectedSlice == right.selectedSlice &&
         left.autoSliceCount == right.autoSliceCount &&
         left.waveformReady == right.waveformReady &&
         left.hasSample == right.hasSample &&
         left.previewActive == right.previewActive &&
         left.previewPlayheadVisible == right.previewPlayheadVisible;
}

} // namespace detail

struct UiSampleEditorControllerState {
  SampleEditorViewUi2Snapshot capture{};
  std::array<UiSampleWaveformMarker, 3> markers{};
  std::array<char, 33> help{};
  RectI16 cursorVisualRect{};
  UiSampleEditorCursor cursor = UiSampleEditorCursor::None;
  UiPowerState power = UiPowerState::BatteryNormal;
  std::uint8_t markerCount = 0;
  std::uint8_t bottomActive = 0xFFU;
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  bool enterDigitFocus = false;

  [[nodiscard]] UiSampleEditorViewData ToViewData() const {
    UiSampleEditorViewData data;
    data.name = detail::SampleCStringView(capture.name);
    data.start = detail::SampleCStringView(capture.start);
    data.end = detail::SampleCStringView(capture.end);
    data.field3Label = "OP";
    data.field3Value = detail::SampleCStringView(capture.operation);
    data.field4Label = "APPLY";
    data.field4Value = {};
    data.help = detail::SampleCStringView(help);
    data.waveformMask = capture.waveformReady ? capture.waveform.Mask()
                                              : std::span<const std::uint8_t>{};
    data.waveformRevision =
        capture.waveformReady ? capture.waveform.revision : 0U;
    data.markers = {markers.data(), markerCount};
    data.cursor = cursor;
    data.focusDigit = std::min<std::uint8_t>(capture.focusDigit, 6U);
    data.enterDigitFocus = enterDigitFocus;
    data.cursorVisualRect = cursorVisualRect;
    data.cursorVisualOverride = cursorVisualOverride;
    data.cursorInkVisible = cursorInkVisible;
    data.waveformReady = capture.waveformReady;
    data.previewActive = capture.playing;
    data.singleCycle = capture.singleCycle;
    data.projectPool = capture.projectPool;
    data.power = power;
    data.bottomActive = bottomActive;
    if (capture.projectPool) {
      data.bottomActions = {"SAVE", "DISCARD", {}, {}};
      data.bottomActionCount = 2;
    } else {
      data.bottomActions = {"SAVE", "SAVE&LOAD", "DISCARD", {}};
      data.bottomActionCount = 3;
    }
    return data;
  }

  bool operator==(const UiSampleEditorControllerState &other) const {
    return detail::EqualEditorCapture(capture, other.capture) &&
           markers == other.markers && help == other.help &&
           cursorVisualRect == other.cursorVisualRect &&
           cursor == other.cursor && power == other.power &&
           markerCount == other.markerCount &&
           bottomActive == other.bottomActive &&
           cursorVisualOverride == other.cursorVisualOverride &&
           cursorInkVisible == other.cursorInkVisible &&
           enterDigitFocus == other.enterDigitFocus;
  }
};

inline UiSampleEditorControllerState MakeUiSampleEditorControllerState(
    const SampleEditorViewUi2Snapshot &snapshot,
    UiPowerState power = UiPowerState::BatteryNormal,
    UiSampleControllerModifiers modifiers = {}) {
  UiSampleEditorControllerState state;
  state.capture = snapshot;
  state.markerCount = detail::CopySampleMarkers(snapshot.markers, state.markers);
  state.power = snapshot.playing ? UiPowerState::Playing : power;
  state.enterDigitFocus =
      modifiers.enterHeld &&
      (snapshot.focus == SampleEditorViewUi2Focus::Start ||
       snapshot.focus == SampleEditorViewUi2Focus::End);

  std::string_view help;
  switch (snapshot.focus) {
  case SampleEditorViewUi2Focus::Name:
    state.cursor = UiSampleEditorCursor::Name;
    help = "ENTER MOVE  EDIT DELETE";
    break;
  case SampleEditorViewUi2Focus::Start:
    state.cursor = UiSampleEditorCursor::Start;
    help = "ENTER+ARROWS ADJUST START";
    break;
  case SampleEditorViewUi2Focus::End:
    state.cursor = UiSampleEditorCursor::End;
    help = "ENTER+ARROWS ADJUST END";
    break;
  case SampleEditorViewUi2Focus::Operation:
    state.cursor = UiSampleEditorCursor::Field3;
    help = "ENTER+UP/DOWN SELECT OP";
    break;
  case SampleEditorViewUi2Focus::Apply:
    state.cursor = UiSampleEditorCursor::Field4;
    help = "ENTER APPLY OPERATION";
    break;
  case SampleEditorViewUi2Focus::Save:
    state.cursor = UiSampleEditorCursor::Save;
    state.bottomActive = 0;
    help = "ENTER SAVE";
    break;
  case SampleEditorViewUi2Focus::SaveAndLoad:
    state.cursor = UiSampleEditorCursor::SaveAndLoad;
    state.bottomActive = snapshot.projectPool ? 0xFFU : 1U;
    help = "ENTER SAVE AND LOAD";
    break;
  case SampleEditorViewUi2Focus::Discard:
    state.cursor = UiSampleEditorCursor::Discard;
    state.bottomActive = snapshot.projectPool ? 1U : 2U;
    help = "ENTER DISCARD";
    break;
  case SampleEditorViewUi2Focus::Waveform:
    state.cursor = UiSampleEditorCursor::Waveform;
    if (modifiers.enterHeld)
      help = "ARROWS MOVE MARKER";
    else if (modifiers.editHeld)
      help = "LEFT/RIGHT MARKER  UP/DOWN ZOOM";
    else
      help = snapshot.singleCycle ? "PLAY LOOP PREVIEW  EDIT ZOOM"
                                  : "PLAY PREVIEW  EDIT ZOOM";
    break;
  case SampleEditorViewUi2Focus::Unknown:
    state.cursor = UiSampleEditorCursor::None;
    break;
  }
  detail::SetSampleText(state.help, help);
  return state;
}

struct UiSampleSlicesControllerState {
  static constexpr std::size_t MarkerCapacity =
      SampleSlicesViewUi2Snapshot::SliceCapacity + 1U;

  SampleSlicesViewUi2Snapshot capture{};
  std::array<UiSampleWaveformMarker, MarkerCapacity> markers{};
  std::array<char, 33> help{};
  std::array<char, 3> autoSliceCount{};
  RectI16 cursorVisualRect{};
  UiSampleSlicesCursor cursor = UiSampleSlicesCursor::None;
  UiPowerState power = UiPowerState::BatteryNormal;
  std::uint8_t markerCount = 0;
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;

  [[nodiscard]] UiSampleSlicesViewData ToViewData() const {
    UiSampleSlicesViewData data;
    data.sliceCount = detail::SampleCStringView(capture.sliceCount);
    data.slice = detail::SampleCStringView(capture.slice);
    data.start = detail::SampleCStringView(capture.start);
    data.zoom = detail::SampleCStringView(capture.zoom);
    data.autoSliceCount = detail::SampleCStringView(autoSliceCount);
    data.help = detail::SampleCStringView(help);
    data.waveformMask = capture.waveformReady ? capture.waveform.Mask()
                                              : std::span<const std::uint8_t>{};
    data.waveformRevision =
        capture.waveformReady ? capture.waveform.revision : 0U;
    data.markers = {markers.data(), markerCount};
    data.selectedMarker = capture.selectedSlice;
    data.cursor = cursor;
    data.cursorVisualRect = cursorVisualRect;
    data.cursorVisualOverride = cursorVisualOverride;
    data.cursorInkVisible = cursorInkVisible;
    data.waveformReady = capture.waveformReady;
    data.hasSample = capture.hasSample;
    data.previewActive = capture.previewActive;
    data.previewPlayheadVisible = capture.previewPlayheadVisible;
    data.power = power;
    return data;
  }

  bool operator==(const UiSampleSlicesControllerState &other) const {
    return detail::EqualSlicesCapture(capture, other.capture) &&
           markers == other.markers && help == other.help &&
           autoSliceCount == other.autoSliceCount &&
           cursorVisualRect == other.cursorVisualRect &&
           cursor == other.cursor && power == other.power &&
           markerCount == other.markerCount &&
           cursorVisualOverride == other.cursorVisualOverride &&
           cursorInkVisible == other.cursorInkVisible;
  }
};

inline UiSampleSlicesControllerState MakeUiSampleSlicesControllerState(
    const SampleSlicesViewUi2Snapshot &snapshot,
    UiPowerState power = UiPowerState::BatteryNormal,
    UiSampleControllerModifiers modifiers = {}) {
  UiSampleSlicesControllerState state;
  state.capture = snapshot;
  state.markerCount = detail::CopySampleMarkers(snapshot.markers, state.markers);
  state.power = snapshot.previewActive ? UiPowerState::Playing : power;
  const std::uint8_t count = std::min<std::uint8_t>(
      snapshot.autoSliceCount,
      static_cast<std::uint8_t>(SampleSlicesViewUi2Snapshot::SliceCapacity));
  state.autoSliceCount = {static_cast<char>('0' + count / 10U),
                          static_cast<char>('0' + count % 10U), '\0'};

  std::string_view help;
  switch (snapshot.focus) {
  case SampleSlicesViewUi2Focus::Waveform:
    state.cursor = UiSampleSlicesCursor::Waveform;
    if (modifiers.enterHeld)
      help = "ARROWS MOVE  LEFT/RIGHT FINE";
    else if (modifiers.editHeld)
      help = "UP/DOWN ZOOM";
    else
      help = "LEFT/RIGHT SELECT  PLAY PREVIEW";
    break;
  case SampleSlicesViewUi2Focus::AutoSliceCount:
    state.cursor = UiSampleSlicesCursor::AutoSliceCount;
    help = "ENTER+UP/DOWN SET COUNT";
    break;
  case SampleSlicesViewUi2Focus::AutoSlice:
    state.cursor = UiSampleSlicesCursor::AutoSlice;
    help = "ENTER APPLY EVEN SLICES";
    break;
  case SampleSlicesViewUi2Focus::Unknown:
    state.cursor = UiSampleSlicesCursor::None;
    break;
  }
  detail::SetSampleText(state.help, help);
  return state;
}

static_assert(std::is_trivially_copyable_v<UiSampleEditorControllerState>);
static_assert(std::is_trivially_copyable_v<UiSampleSlicesControllerState>);
static_assert(sizeof(UiSampleEditorControllerState) <= 1'100U);
static_assert(sizeof(UiSampleSlicesControllerState) <= 1'250U);

} // namespace ui2
