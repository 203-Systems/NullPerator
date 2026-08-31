/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/UI2/Ui2SampleSnapshots.h"
#include "Application/UI2/Ui2SampleWaveformBackend.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace ui2 {

enum class Ui2SampleSlicesCommandType : std::uint8_t {
  None,
  PreviewStart,
  PreviewStop,
  SetSlicePoint,
  AddSlice,
  DeleteSlice,
  SetAutoSliceCount,
  RequestAutoSlice,
  NavigateBack,
};

struct Ui2SampleSlicesCommand {
  Ui2SampleSlicesCommandType type = Ui2SampleSlicesCommandType::None;
  std::array<char, PFILENAME_SIZE> path{};
  std::uint32_t value = 0U;
  std::uint32_t start = 0U;
  std::uint32_t end = 0U;
  std::uint8_t slice = 0U;
  std::uint8_t count = 0U;
  bool singleCycle = false;

  [[nodiscard]] bool HasValue() const {
    return type != Ui2SampleSlicesCommandType::None;
  }
};

// Fixed-capacity slice editor state. Mutating commands are immediately
// reflected in this local projection so cursor/markers remain responsive; the
// application applies the typed command to SampleInstrument and may call
// SynchronizeSlices again if the model clamps or rejects it.
class Ui2SampleSlicesController {
public:
  static constexpr std::size_t SliceCapacity =
      SampleSlicesViewUi2Snapshot::SliceCapacity;

  explicit Ui2SampleSlicesController(Ui2SampleWaveformBackend &waveform)
      : waveform_(waveform) {
    static_assert(SliceCapacity ==
                  SampleSlicesViewUi2Snapshot::SliceCapacity);
  }

  [[nodiscard]] Ui2SampleWaveformLoadResult
  OpenPath(FileSystem &fileSystem, const char *path) {
    ResetController();
    const auto result = waveform_.LoadPath(fileSystem, path);
    if (result != Ui2SampleWaveformLoadResult::Loaded)
      return result;
    FinishOpen();
    return result;
  }

  [[nodiscard]] Ui2SampleWaveformLoadResult
  OpenProjectPool(FileSystem &fileSystem, const char *projectName,
                  const char *sampleName) {
    ResetController();
    const auto result =
        waveform_.LoadProjectPool(fileSystem, projectName, sampleName);
    if (result != Ui2SampleWaveformLoadResult::Loaded)
      return result;
    FinishOpen();
    return result;
  }

  [[nodiscard]] Ui2SampleWaveformLoadResult
  OpenLibrary(FileSystem &fileSystem, const char *relativePath) {
    ResetController();
    const auto result = waveform_.LoadLibrary(fileSystem, relativePath);
    if (result != Ui2SampleWaveformLoadResult::Loaded)
      return result;
    FinishOpen();
    return result;
  }

  void Close() {
    ResetController();
    waveform_.Reset();
  }

  void SynchronizeSlices(const std::array<std::uint32_t, SliceCapacity> &points,
                         std::uint16_t definedMask) {
    slicePoints_ = points;
    definedMask_ = definedMask;
    previewableMask_ = definedMask;
    ClampSlices();
    CenterSelectedSlice();
  }

  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] std::uint8_t SelectedSlice() const { return selectedSlice_; }
  [[nodiscard]] std::uint8_t AutoSliceCount() const {
    return autoSliceCount_;
  }
  [[nodiscard]] std::uint16_t HeldMask() const { return input_.Mask(); }
  [[nodiscard]] SampleSlicesViewUi2Focus Focus() const { return focus_; }
  [[nodiscard]] std::uint16_t DefinedMask() const { return definedMask_; }
  [[nodiscard]] const std::array<std::uint32_t, SliceCapacity> &
  SlicePoints() const {
    return slicePoints_;
  }
  [[nodiscard]] Ui2SampleWaveformBuildResult LastWaveformBuild() const {
    return lastBuild_;
  }

  void SetFocus(SampleSlicesViewUi2Focus focus) { focus_ = focus; }

  bool PanView(std::int16_t columns) {
    if (!waveform_.PanColumns(columns))
      return false;
    RebuildWaveform();
    return true;
  }

  void SetPreviewPlayhead(std::uint32_t sample, bool visible) {
    previewPlayhead_ = sample;
    previewPlayheadVisible_ = visible && active_;
  }

  void StartPreview(std::uint32_t sample) {
    if (!active_)
      return;
    previewHeld_ = true;
    previewActive_ = true;
    previewPlayhead_ = sample;
    previewPlayheadVisible_ = true;
  }

  // Preview ownership also ends on page transitions and application-level
  // stops, not only on the matching PLAY release.
  void StopPreview() {
    previewHeld_ = false;
    previewActive_ = false;
    previewPlayhead_ = 0U;
    previewPlayheadVisible_ = false;
  }

  Ui2SampleSlicesCommand Handle(TrackerAction action, bool pressed) {
    const bool repeatedPress = pressed && input_.Held(action);
    if (!active_ || !input_.Update(action, pressed))
      return {};
    if (!pressed) {
      if (action == TrackerAction::Play && previewHeld_) {
        Ui2SampleSlicesCommand command =
            MakeCommand(Ui2SampleSlicesCommandType::PreviewStop);
        StopPreview();
        return command;
      }
      return {};
    }

    if (action == TrackerAction::Play) {
      if (repeatedPress || previewHeld_ || waveform_.FrameCount() == 0U)
        return {};
      if (IsDefined(selectedSlice_) && !IsPreviewable(selectedSlice_))
        return {};
      const std::uint32_t start = SelectedSliceStart();
      StartPreview(start);
      Ui2SampleSlicesCommand command =
          MakeCommand(Ui2SampleSlicesCommandType::PreviewStart);
      command.start = start;
      command.end = SliceEnd(selectedSlice_, start);
      command.singleCycle = IsSingleCycle();
      return command;
    }

    if (input_.Held(TrackerAction::Shift)) {
      if (action == TrackerAction::Left)
        return MakeCommand(Ui2SampleSlicesCommandType::NavigateBack);
      return {};
    }

    const bool edit = input_.Held(TrackerAction::Edit);
    const bool option = input_.Held(TrackerAction::Option);
    if (option && (action == TrackerAction::Up ||
                   action == TrackerAction::Down)) {
      const std::int8_t delta = action == TrackerAction::Up ? 1 : -1;
      if (waveform_.AdjustZoom(delta, SelectedSliceStart()))
        RebuildWaveform();
      return {};
    }

    if (focus_ == SampleSlicesViewUi2Focus::Waveform) {
      if (edit && IsDirection(action))
        return MoveSelectedSlice(action);
      if (!edit && !option && action == TrackerAction::Left) {
        SelectPrevious();
        return {};
      }
      if (!edit && !option && action == TrackerAction::Right) {
        SelectNext();
        return {};
      }
    }

    if (focus_ == SampleSlicesViewUi2Focus::AutoSliceCount) {
      const bool horizontal = !edit && !option &&
                              (action == TrackerAction::Left ||
                               action == TrackerAction::Right);
      const bool verticalEdit = edit &&
                                (action == TrackerAction::Up ||
                                 action == TrackerAction::Down);
      if (horizontal || verticalEdit) {
        const int delta = action == TrackerAction::Left ||
                                  action == TrackerAction::Down
                              ? -1
                              : 1;
        const std::uint8_t next = static_cast<std::uint8_t>(std::clamp<int>(
            static_cast<int>(autoSliceCount_) + delta, 1,
            static_cast<int>(SliceCapacity)));
        if (next == autoSliceCount_)
          return {};
        autoSliceCount_ = next;
        Ui2SampleSlicesCommand command =
            MakeCommand(Ui2SampleSlicesCommandType::SetAutoSliceCount);
        command.count = autoSliceCount_;
        return command;
      }
    }

    if (!edit && !option && action == TrackerAction::Up) {
      MoveFocus(-1);
      return {};
    }
    if (!edit && !option && action == TrackerAction::Down) {
      MoveFocus(1);
      return {};
    }
    if (action == TrackerAction::Edit &&
        focus_ == SampleSlicesViewUi2Focus::AutoSlice) {
      // Replacing existing slice points is destructive. Until the approved
      // confirmation flow is connected, expose this as unavailable instead
      // of emitting a request that the application silently discards.
      if (definedMask_ != 0U)
        return {};
      Ui2SampleSlicesCommand command =
          MakeCommand(Ui2SampleSlicesCommandType::RequestAutoSlice);
      command.count = autoSliceCount_;
      return command;
    }
    return {};
  }

  // Explicit ADD/DELETE integration hooks for the approved bottom action bar.
  // Their final physical key chord still needs product approval, so Handle()
  // preserves the existing legacy selection/move/auto-slice semantics instead
  // of inventing a gesture.
  Ui2SampleSlicesCommand AddSelectedAt(std::uint32_t start) {
    if (!active_ || waveform_.FrameCount() == 0U)
      return {};
    start = std::min(start, waveform_.FrameCount() - 1U);
    slicePoints_[selectedSlice_] = start;
    definedMask_ |= static_cast<std::uint16_t>(1U << selectedSlice_);
    previewableMask_ |= static_cast<std::uint16_t>(1U << selectedSlice_);
    CenterSelectedSlice();
    Ui2SampleSlicesCommand command =
        MakeCommand(Ui2SampleSlicesCommandType::AddSlice);
    command.value = start;
    return command;
  }

  Ui2SampleSlicesCommand DeleteSelected() {
    if (!active_ || !IsDefined(selectedSlice_))
      return {};
    definedMask_ &= static_cast<std::uint16_t>(~(1U << selectedSlice_));
    previewableMask_ &=
        static_cast<std::uint16_t>(~(1U << selectedSlice_));
    slicePoints_[selectedSlice_] = SelectedSliceStartFallback();
    CenterSelectedSlice();
    return MakeCommand(Ui2SampleSlicesCommandType::DeleteSlice);
  }

  // Call after an unopposed RequestAutoSlice or after the approved replacement
  // confirmation returns YES. This mirrors the legacy even-slice algorithm.
  void ApplyEvenSlices(std::uint8_t count) {
    if (!active_ || waveform_.FrameCount() == 0U)
      return;
    count = static_cast<std::uint8_t>(std::clamp<int>(
        count, 1, static_cast<int>(SliceCapacity)));
    slicePoints_.fill(0U);
    definedMask_ = 0U;
    previewableMask_ = 0U;
    if (count > 1U) {
      for (std::uint8_t index = 0U; index < count; ++index) {
        slicePoints_[index] = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(waveform_.FrameCount()) * index) /
            count);
        definedMask_ |= static_cast<std::uint16_t>(1U << index);
        previewableMask_ |= static_cast<std::uint16_t>(1U << index);
      }
    }
    CenterSelectedSlice();
  }

  [[nodiscard]] SampleSlicesViewUi2Snapshot Snapshot() const {
    SampleSlicesViewUi2Snapshot snapshot;
    const std::uint8_t activeCount = DefinedCount();
    std::snprintf(snapshot.slice.data(), snapshot.slice.size(), "%02u / %02u",
                  activeCount == 0U ? 0U
                                    : static_cast<unsigned>(selectedSlice_ + 1U),
                  static_cast<unsigned>(activeCount));
    std::snprintf(snapshot.start.data(), snapshot.start.size(), "%07X",
                  static_cast<unsigned>(SelectedSliceStart()));
    const std::uint32_t zoom = std::uint32_t{1} << waveform_.ZoomLevel();
    std::snprintf(snapshot.zoom.data(), snapshot.zoom.size(), "%uX",
                  static_cast<unsigned>(zoom));
    snapshot.waveform = waveformPacket_;
    snapshot.focus = focus_;
    snapshot.selectedSlice = selectedSlice_;
    snapshot.autoSliceCount = autoSliceCount_;
    snapshot.waveformReady = waveformReady_;
    snapshot.hasSample = active_ && waveform_.Ready();
    snapshot.previewActive = previewActive_;
    snapshot.previewPlayheadVisible = previewPlayheadVisible_;
    snapshot.autoSliceApplyAvailable = definedMask_ == 0U;

    for (std::uint8_t index = 0U; index < SliceCapacity; ++index) {
      if (!IsDefined(index))
        continue;
      PushMarker(snapshot, slicePoints_[index], Ui2WaveformMarkerKind::Slice,
                 index == selectedSlice_);
    }
    if (previewPlayheadVisible_)
      PushMarker(snapshot, previewPlayhead_, Ui2WaveformMarkerKind::Playhead,
                 false);
    return snapshot;
  }

private:
  static bool IsDirection(TrackerAction action) {
    return action == TrackerAction::Left || action == TrackerAction::Right ||
           action == TrackerAction::Up || action == TrackerAction::Down;
  }

  [[nodiscard]] bool IsSingleCycle() const {
    return static_cast<std::uint64_t>(waveform_.FrameCount()) *
               waveform_.ChannelCount() <=
           Ui2SingleCycleMaximumFrames;
  }

  void ResetController() {
    input_ = {};
    waveformPacket_ = {};
    slicePoints_.fill(0U);
    definedMask_ = 0U;
    previewableMask_ = 0U;
    previewPlayhead_ = 0U;
    selectedSlice_ = 0U;
    autoSliceCount_ = 4U;
    focus_ = SampleSlicesViewUi2Focus::Waveform;
    lastBuild_ = Ui2SampleWaveformBuildResult::NotLoaded;
    active_ = waveformReady_ = previewHeld_ = previewActive_ = false;
    previewPlayheadVisible_ = false;
  }

  void FinishOpen() {
    active_ = true;
    waveform_.CenterOn(0U);
    RebuildWaveform();
  }

  void RebuildWaveform() {
    lastBuild_ = waveform_.BuildMask(
        waveformPacket_, Ui2SampleWaveformBackend::SlicesMaskHeight);
    waveformReady_ = lastBuild_ == Ui2SampleWaveformBuildResult::Built;
  }

  [[nodiscard]] bool IsDefined(std::uint8_t index) const {
    return index < SliceCapacity &&
           (definedMask_ & static_cast<std::uint16_t>(1U << index)) != 0U;
  }

  [[nodiscard]] bool IsPreviewable(std::uint8_t index) const {
    return index < SliceCapacity &&
           (previewableMask_ & static_cast<std::uint16_t>(1U << index)) != 0U;
  }

  void ClampSlices() {
    if (waveform_.FrameCount() == 0U) {
      definedMask_ = 0U;
      previewableMask_ = 0U;
      slicePoints_.fill(0U);
      return;
    }
    for (std::uint8_t index = 0U; index < SliceCapacity; ++index) {
      // SampleInstrument permits the exclusive sample-size endpoint as stored
      // data but refuses that zero-length slice at playback. Remember the
      // distinction before clamping markers to the last visible frame.
      if (slicePoints_[index] >= waveform_.FrameCount())
        previewableMask_ &=
            static_cast<std::uint16_t>(~(1U << index));
      slicePoints_[index] =
          std::min(slicePoints_[index], waveform_.FrameCount() - 1U);
    }
  }

  [[nodiscard]] std::uint8_t DefinedCount() const {
    std::uint8_t count = 0U;
    for (std::uint8_t index = 0U; index < SliceCapacity; ++index)
      count += IsDefined(index) ? 1U : 0U;
    return count;
  }

  [[nodiscard]] std::uint32_t SelectedSliceStartFallback() const {
    if (waveform_.FrameCount() == 0U)
      return 0U;
    for (int index = static_cast<int>(selectedSlice_) - 1; index >= 0;
         --index) {
      if (IsDefined(static_cast<std::uint8_t>(index)))
        return slicePoints_[static_cast<std::size_t>(index)];
    }
    return 0U;
  }

  [[nodiscard]] std::uint32_t SelectedSliceStart() const {
    return IsDefined(selectedSlice_) ? slicePoints_[selectedSlice_]
                                     : SelectedSliceStartFallback();
  }

  [[nodiscard]] std::uint32_t SliceEnd(std::uint8_t index,
                                       std::uint32_t start) const {
    if (waveform_.FrameCount() == 0U)
      return 0U;
    for (std::uint8_t candidate = static_cast<std::uint8_t>(index + 1U);
         candidate < SliceCapacity; ++candidate) {
      if (IsDefined(candidate) && slicePoints_[candidate] > start) {
        // SampleInstrument renders up to, but not including, the next slice
        // point. Preview commands use an inclusive end for their playhead.
        return IsPreviewable(candidate) ? slicePoints_[candidate] - 1U
                                        : waveform_.FrameCount() - 1U;
      }
    }
    return waveform_.FrameCount() - 1U;
  }

  void SelectPrevious() {
    if (selectedSlice_ > 0U) {
      --selectedSlice_;
      CenterSelectedSlice();
    }
  }

  void SelectNext() {
    if (selectedSlice_ + 1U < SliceCapacity) {
      ++selectedSlice_;
      CenterSelectedSlice();
    }
  }

  void CenterSelectedSlice() {
    if (waveform_.CenterOn(SelectedSliceStart()))
      RebuildWaveform();
  }

  Ui2SampleSlicesCommand MoveSelectedSlice(TrackerAction action) {
    const std::uint32_t span = waveform_.ViewEnd() - waveform_.ViewStart();
    std::int64_t delta =
        action == TrackerAction::Left || action == TrackerAction::Right
            ? (waveform_.ZoomLevel() >= waveform_.MaxZoomLevel()
                   ? 1U
                   : std::max<std::uint32_t>(1U, span / 64U))
            : std::max<std::uint32_t>(1U, span / 16U);
    if (action == TrackerAction::Left || action == TrackerAction::Down)
      delta = -delta;
    const std::uint32_t current = SelectedSliceStart();
    const std::uint32_t next = static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(static_cast<std::int64_t>(current) + delta,
                                 0,
                                 static_cast<std::int64_t>(
                                     waveform_.FrameCount() - 1U)));
    if (next == current && IsDefined(selectedSlice_))
      return {};
    slicePoints_[selectedSlice_] = next;
    definedMask_ |= static_cast<std::uint16_t>(1U << selectedSlice_);
    previewableMask_ |= static_cast<std::uint16_t>(1U << selectedSlice_);
    CenterSelectedSlice();
    Ui2SampleSlicesCommand command =
        MakeCommand(Ui2SampleSlicesCommandType::SetSlicePoint);
    command.value = next;
    return command;
  }

  void MoveFocus(int delta) {
    constexpr std::array<SampleSlicesViewUi2Focus, 3> order{
        SampleSlicesViewUi2Focus::Waveform,
        SampleSlicesViewUi2Focus::AutoSliceCount,
        SampleSlicesViewUi2Focus::AutoSlice};
    std::size_t current = focus_ == order[1] ? 1U : focus_ == order[2] ? 2U
                                                                        : 0U;
    focus_ = order[(static_cast<int>(current) + delta + 3) % 3];
  }

  Ui2SampleSlicesCommand
  MakeCommand(Ui2SampleSlicesCommandType type) const {
    Ui2SampleSlicesCommand command;
    command.type = type;
    std::snprintf(command.path.data(), command.path.size(), "%s",
                  waveform_.Path());
    command.slice = selectedSlice_;
    command.count = autoSliceCount_;
    command.start = SelectedSliceStart();
    command.end = SliceEnd(selectedSlice_, command.start);
    command.singleCycle = IsSingleCycle();
    return command;
  }

  void PushMarker(SampleSlicesViewUi2Snapshot &snapshot, std::uint32_t sample,
                  Ui2WaveformMarkerKind kind, bool selected) const {
    if (waveform_.ViewEnd() <= waveform_.ViewStart() ||
        sample < waveform_.ViewStart() || sample >= waveform_.ViewEnd())
      return;
    snapshot.markers.Push(
        Ui2WaveformX(sample, waveform_.ViewStart(), waveform_.ViewEnd()), kind,
        selected);
  }

  Ui2SampleWaveformBackend &waveform_;
  Ui2ControllerInputState input_{};
  Ui2WaveformSnapshot waveformPacket_{};
  std::array<std::uint32_t, SliceCapacity> slicePoints_{};
  std::uint16_t definedMask_ = 0U;
  std::uint16_t previewableMask_ = 0U;
  std::uint32_t previewPlayhead_ = 0U;
  std::uint8_t selectedSlice_ = 0U;
  std::uint8_t autoSliceCount_ = 4U;
  SampleSlicesViewUi2Focus focus_ = SampleSlicesViewUi2Focus::Waveform;
  Ui2SampleWaveformBuildResult lastBuild_ =
      Ui2SampleWaveformBuildResult::NotLoaded;
  bool active_ = false;
  bool waveformReady_ = false;
  bool previewHeld_ = false;
  bool previewActive_ = false;
  bool previewPlayheadVisible_ = false;
};

static_assert(sizeof(Ui2SampleSlicesController) <= 1'200U);

} // namespace ui2
