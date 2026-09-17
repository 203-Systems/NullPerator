/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/UI2/Ui2SampleSnapshots.h"
#include "Application/UI2/Ui2SampleWaveformBackend.h"
#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"

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
  SetSliceCount,
  AddSlice,
  DeleteSlice,
  RequestAutoSlice,
  ReplaceAutoSlices,
  OperationUnavailable,
  NavigateBack,
};

enum class Ui2SampleSlicesFailure : std::uint8_t {
  None,
  AddLocked,
  DeleteLocked,
  MoveLimit,
};

struct Ui2SampleSlicesCommand {
  Ui2SampleSlicesCommandType type = Ui2SampleSlicesCommandType::None;
  std::array<char, PFILENAME_SIZE> path{};
  std::uint32_t value = 0U;
  std::uint32_t start = 0U;
  std::uint32_t end = 0U;
  std::uint8_t slice = 0U;
  std::uint8_t count = 0U;
  Ui2SampleSlicesFailure failure = Ui2SampleSlicesFailure::None;
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
    static_assert(SliceCapacity == SampleSlicesViewUi2Snapshot::SliceCapacity);
  }

  [[nodiscard]] Ui2SampleWaveformLoadResult OpenPath(FileSystem &fileSystem,
                                                     const char *path) {
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
    ClampSelection();
    CenterSelectedSlice();
  }

  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] std::uint8_t SelectedSlice() const { return selectedSlice_; }
  [[nodiscard]] std::uint16_t HeldMask() const { return input_.Mask(); }
  [[nodiscard]] SampleSlicesViewUi2Focus Focus() const { return focus_; }
  [[nodiscard]] std::uint16_t DefinedMask() const { return definedMask_; }
  [[nodiscard]] bool DialogActive() const { return dialogActive_; }
  [[nodiscard]] std::uint32_t DialogInstanceId() const {
    return dialogInstanceId_;
  }
  [[nodiscard]] const std::array<std::uint32_t, SliceCapacity> &
  SlicePoints() const {
    return slicePoints_;
  }
  [[nodiscard]] Ui2SampleWaveformBuildResult LastWaveformBuild() const {
    return lastBuild_;
  }

  void SetFocus(SampleSlicesViewUi2Focus focus) { focus_ = focus; }

  Ui2SampleSlicesCommand HandleDialog(TrackerAction action, bool pressed) {
    if (!dialogActive_ || !dialogInput_.Update(action, pressed) ||
        !dialogReleaseGate_.Update(action, pressed) || !pressed)
      return {};
    if (action == TrackerAction::Left || action == TrackerAction::Right) {
      dialogSelectedAction_ = action == TrackerAction::Right ? 1U : 0U;
      return {};
    }
    if (action != TrackerAction::Enter)
      return {};
    const bool replace = dialogSelectedAction_ == 1U;
    dialogActive_ = false;
    dialogInput_ = {};
    dialogReleaseGate_.Reset();
    if (!replace)
      return {};
    Ui2SampleSlicesCommand command =
        MakeCommand(Ui2SampleSlicesCommandType::ReplaceAutoSlices);
    command.count = AvailableCount();
    return command;
  }

  [[nodiscard]] Ui2DialogSnapshot DialogSnapshot() const {
    Ui2DialogSnapshot snapshot;
    snapshot.kind = UiDialogKind::Message;
    snapshot.SetTitle("SPACE SLICES EVENLY?");
    std::array<char, 32> label{};
    std::snprintf(label.data(), label.size(), "REPOSITION %u SLICES",
                  AvailableCount());
    snapshot.SetLabel(label.data());
    snapshot.PushAction(UiDialogAction::Cancel);
    snapshot.PushAction(UiDialogAction::Replace);
    snapshot.SetSelectedAction(dialogSelectedAction_, true);
    return snapshot;
  }

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
    previewActive_ = true;
    previewPlayhead_ = sample;
    previewPlayheadVisible_ = true;
  }

  // Preview ownership also ends on page transitions and application-level
  // stops, as well as on a second PLAY tap.
  [[nodiscard]] bool IsPreviewing() const { return previewActive_; }

  void StopPreview() {
    previewActive_ = false;
    previewPlayhead_ = 0U;
    previewPlayheadVisible_ = false;
  }

  Ui2SampleSlicesCommand Handle(TrackerAction action, bool pressed) {
    const bool repeatedPress = pressed && input_.Held(action);
    if (!active_ || !input_.Update(action, pressed))
      return {};
    if (!pressed)
      return {};

    if (action == TrackerAction::Play) {
      if (repeatedPress)
        return {};
      if (previewActive_) {
        const auto command =
            MakeCommand(Ui2SampleSlicesCommandType::PreviewStop);
        StopPreview();
        return command;
      }
      if (waveform_.FrameCount() == 0U)
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
      if (action == TrackerAction::Enter &&
          (focus_ == SampleSlicesViewUi2Focus::Waveform ||
           focus_ == SampleSlicesViewUi2Focus::Start)) {
        if (IsDefined(selectedSlice_))
          return DeleteSelected();
        return MakeFailure(Ui2SampleSlicesFailure::DeleteLocked);
      }
      if (action == TrackerAction::Left)
        return MakeCommand(Ui2SampleSlicesCommandType::NavigateBack);
      return {};
    }

    const bool enter = input_.Held(TrackerAction::Enter);
    const bool option = input_.Held(TrackerAction::Option);
    if (option &&
        (action == TrackerAction::Up || action == TrackerAction::Down)) {
      const std::int8_t delta = action == TrackerAction::Up ? 1 : -1;
      if (waveform_.AdjustZoom(delta, SelectedSliceStart()))
        RebuildWaveform();
      return {};
    }

    if (focus_ == SampleSlicesViewUi2Focus::Waveform) {
      if (!option &&
          (action == TrackerAction::Left || action == TrackerAction::Right)) {
        if (enter)
          return ChangeSliceCount(action == TrackerAction::Right ? 1 : -1);
        if (action == TrackerAction::Left)
          SelectPrevious();
        else
          SelectNext();
        return {};
      }
    }

    if (focus_ == SampleSlicesViewUi2Focus::Zoom && !option &&
        (action == TrackerAction::Left || action == TrackerAction::Right ||
         (enter &&
          (action == TrackerAction::Up || action == TrackerAction::Down)))) {
      const std::int8_t delta =
          action == TrackerAction::Right || action == TrackerAction::Up ? 1
                                                                        : -1;
      if (waveform_.AdjustZoom(delta, SelectedSliceStart()))
        RebuildWaveform();
      return {};
    }

    if (focus_ == SampleSlicesViewUi2Focus::Start && IsDirection(action)) {
      if (enter) {
        if (action == TrackerAction::Left || action == TrackerAction::Right) {
          focusDigit_ = static_cast<std::uint8_t>(std::clamp<int>(
              focusDigit_ + (action == TrackerAction::Right ? 1 : -1), 0, 6));
          return {};
        }
        const std::int64_t step = std::int64_t{1} << (4U * (6U - focusDigit_));
        return MoveSliceBy(action == TrackerAction::Up ? step : -step);
      }
      if (!option &&
          (action == TrackerAction::Left || action == TrackerAction::Right))
        return MoveSliceBy(action == TrackerAction::Right ? 1 : -1);
    }

    if (!enter && !option && action == TrackerAction::Up) {
      MoveFocus(-1);
      return {};
    }
    if (!enter && !option && action == TrackerAction::Down) {
      MoveFocus(1);
      return {};
    }
    if (action == TrackerAction::Enter &&
        focus_ == SampleSlicesViewUi2Focus::AutoSlice && !repeatedPress) {
      if (definedMask_ != 0U) {
        RequestAutoSliceConfirmation(TrackerAction::Enter);
        return {};
      }
      Ui2SampleSlicesCommand command =
          MakeCommand(Ui2SampleSlicesCommandType::RequestAutoSlice);
      command.count = AvailableCount();
      return command;
    }
    return {};
  }

  Ui2SampleSlicesCommand AddSelectedAt(std::uint32_t start) {
    if (!active_ || waveform_.FrameCount() == 0U)
      return {};
    if (IsDefined(selectedSlice_))
      return MakeFailure(Ui2SampleSlicesFailure::AddLocked);
    start = std::min(start, waveform_.FrameCount() - 1U);
    // Legacy projects encode a zero first boundary implicitly whenever a
    // later boundary exists. The first ADD therefore splits the whole sample,
    // rather than dropping everything before the new midpoint.
    if (definedMask_ == 0U) {
      if (start == 0U)
        return MakeFailure(Ui2SampleSlicesFailure::AddLocked);
      selectedSlice_ = 1U;
      slicePoints_[0] = 0U;
      definedMask_ = previewableMask_ = 1U;
    }
    for (std::uint8_t index = 0U; index < SliceCapacity; ++index) {
      if (IsDefined(index) && slicePoints_[index] == start)
        return MakeFailure(Ui2SampleSlicesFailure::AddLocked);
    }
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
    if (!active_)
      return {};
    if (!IsDefined(selectedSlice_))
      return MakeFailure(Ui2SampleSlicesFailure::DeleteLocked);
    std::array<std::uint32_t, SliceCapacity> compacted{};
    std::uint16_t compactedMask = 0U;
    std::uint16_t compactedPreviewMask = 0U;
    std::uint8_t count = 0U;
    for (std::uint8_t index = 0U; index < SliceCapacity; ++index) {
      if (index == selectedSlice_ || !IsDefined(index))
        continue;
      compacted[count] = slicePoints_[index];
      compactedMask |= static_cast<std::uint16_t>(1U << count);
      if (IsPreviewable(index))
        compactedPreviewMask |= static_cast<std::uint16_t>(1U << count);
      ++count;
    }
    slicePoints_ = compacted;
    definedMask_ = compactedMask;
    previewableMask_ = compactedPreviewMask;
    if (count == 0U)
      selectedSlice_ = 0U;
    else if (selectedSlice_ >= count)
      selectedSlice_ = static_cast<std::uint8_t>(count - 1U);
    CenterSelectedSlice();
    return MakeCommand(Ui2SampleSlicesCommandType::DeleteSlice);
  }

  // Call after an unopposed RequestAutoSlice or after the approved replacement
  // confirmation returns YES. This mirrors the legacy even-slice algorithm.
  void ApplyEvenSlices(std::uint8_t count) {
    if (!active_ || waveform_.FrameCount() == 0U)
      return;
    count = static_cast<std::uint8_t>(
        std::clamp<int>(count, 1,
                        static_cast<int>(std::min<std::uint32_t>(
                            SliceCapacity, waveform_.FrameCount()))));
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
    ClampSelection();
    CenterSelectedSlice();
  }

  [[nodiscard]] SampleSlicesViewUi2Snapshot Snapshot() const {
    SampleSlicesViewUi2Snapshot snapshot;
    const std::uint8_t activeCount = AvailableCount();
    snapshot.sliceCount = activeCount;
    std::snprintf(
        snapshot.slice.data(), snapshot.slice.size(), "%02u / %02u",
        activeCount == 0U ? 0U : static_cast<unsigned>(selectedSlice_ + 1U),
        static_cast<unsigned>(activeCount));
    std::snprintf(snapshot.start.data(), snapshot.start.size(), "%07X",
                  static_cast<unsigned>(SelectedSliceStart()));
    const std::uint32_t zoom = std::uint32_t{1} << waveform_.ZoomLevel();
    std::snprintf(snapshot.zoom.data(), snapshot.zoom.size(), "%uX",
                  static_cast<unsigned>(zoom));
    snapshot.waveform = waveformPacket_;
    snapshot.focus = focus_;
    snapshot.selectedSlice = selectedSlice_;
    snapshot.zoomLevel = waveform_.ZoomLevel();
    snapshot.maxZoomLevel = waveform_.MaxZoomLevel();
    snapshot.focusDigit = focusDigit_;
    snapshot.definedMask = definedMask_;
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
    dialogInput_ = {};
    dialogReleaseGate_.Reset();
    waveformPacket_ = {};
    slicePoints_.fill(0U);
    definedMask_ = 0U;
    previewableMask_ = 0U;
    previewPlayhead_ = 0U;
    selectedSlice_ = 0U;
    focusDigit_ = 6U;
    focus_ = SampleSlicesViewUi2Focus::Waveform;
    lastBuild_ = Ui2SampleWaveformBuildResult::NotLoaded;
    active_ = waveformReady_ = previewActive_ = false;
    previewPlayheadVisible_ = false;
    dialogActive_ = false;
    dialogSelectedAction_ = 0U;
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
        previewableMask_ &= static_cast<std::uint16_t>(~(1U << index));
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
    return IsDefined(selectedSlice_) ? slicePoints_[selectedSlice_] : 0U;
  }

  [[nodiscard]] std::uint32_t SuggestedSliceStart() const {
    if (waveform_.FrameCount() == 0U)
      return 0U;
    std::uint32_t lower = 0U;
    for (int index = static_cast<int>(selectedSlice_) - 1; index >= 0;
         --index) {
      if (IsDefined(static_cast<std::uint8_t>(index))) {
        lower = slicePoints_[static_cast<std::size_t>(index)];
        break;
      }
    }
    std::uint32_t upper = waveform_.FrameCount();
    for (std::uint8_t index = static_cast<std::uint8_t>(selectedSlice_ + 1U);
         index < SliceCapacity; ++index) {
      if (IsDefined(index)) {
        upper = slicePoints_[index];
        break;
      }
    }
    if (upper <= lower + 1U)
      return lower;
    return lower + (upper - lower) / 2U;
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

  [[nodiscard]] std::uint8_t AvailableCount() const {
    return active_ && waveform_.FrameCount() > 0U
               ? std::max<std::uint8_t>(1U, DefinedCount())
               : 0U;
  }

  void ClampSelection() {
    const auto count = AvailableCount();
    selectedSlice_ =
        count == 0U ? 0U : std::min<std::uint8_t>(selectedSlice_, count - 1U);
  }

  Ui2SampleSlicesCommand ChangeSliceCount(int delta) {
    const std::uint8_t count = AvailableCount();
    if (count == 0U || (delta < 0 && count == 1U) ||
        (delta > 0 && count == SliceCapacity))
      return {};
    if (delta > 0) {
      // Split the longest interval; keep all existing boundaries intact.
      std::uint8_t split = 0U;
      std::uint32_t largest = 0U;
      for (std::uint8_t index = 0U; index < count; ++index) {
        const auto end = index + 1U < count ? slicePoints_[index + 1U]
                                            : waveform_.FrameCount();
        if (end > slicePoints_[index] && end - slicePoints_[index] > largest) {
          largest = end - slicePoints_[index];
          split = index;
        }
      }
      if (largest < 2U)
        return {};
      for (std::uint8_t index = count; index > split + 1U; --index)
        slicePoints_[index] = slicePoints_[index - 1U];
      slicePoints_[split + 1U] = slicePoints_[split] + largest / 2U;
      if (selectedSlice_ > split)
        ++selectedSlice_;
      definedMask_ = static_cast<std::uint16_t>((1U << (count + 1U)) - 1U);
    } else {
      slicePoints_[count - 1U] = 0U;
      definedMask_ =
          count == 2U ? 0U
                      : static_cast<std::uint16_t>((1U << (count - 1U)) - 1U);
      if (count == 2U)
        slicePoints_.fill(0U);
    }
    previewableMask_ = definedMask_;
    ClampSelection();
    CenterSelectedSlice();
    auto command = MakeCommand(Ui2SampleSlicesCommandType::SetSliceCount);
    command.count = AvailableCount();
    return command;
  }

  void SelectPrevious() {
    if (selectedSlice_ > 0U) {
      --selectedSlice_;
      CenterSelectedSlice();
    }
  }

  void SelectNext() {
    if (selectedSlice_ + 1U < AvailableCount()) {
      ++selectedSlice_;
      CenterSelectedSlice();
    }
  }

  void CenterSelectedSlice() {
    if (waveform_.CenterOn(SelectedSliceStart()))
      RebuildWaveform();
  }

  Ui2SampleSlicesCommand MoveSliceBy(std::int64_t delta) {
    const std::uint32_t current = SelectedSliceStart();
    const std::uint32_t next =
        static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(current) + delta, 0,
            static_cast<std::int64_t>(waveform_.FrameCount() - 1U)));
    if (next == current && IsDefined(selectedSlice_))
      return MakeFailure(Ui2SampleSlicesFailure::MoveLimit);
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
    constexpr std::array<SampleSlicesViewUi2Focus, 4> order{
        SampleSlicesViewUi2Focus::Waveform, SampleSlicesViewUi2Focus::Start,
        SampleSlicesViewUi2Focus::Zoom, SampleSlicesViewUi2Focus::AutoSlice};
    const auto current =
        std::find(order.begin(), order.end(), focus_) - order.begin();
    focus_ = order[std::clamp<int>(static_cast<int>(current) + delta, 0, 3)];
  }

  Ui2SampleSlicesCommand MakeCommand(Ui2SampleSlicesCommandType type) const {
    Ui2SampleSlicesCommand command;
    command.type = type;
    std::snprintf(command.path.data(), command.path.size(), "%s",
                  waveform_.Path());
    command.slice = selectedSlice_;
    command.count = AvailableCount();
    command.start = SelectedSliceStart();
    command.end = SliceEnd(selectedSlice_, command.start);
    command.singleCycle = IsSingleCycle();
    return command;
  }

  Ui2SampleSlicesCommand MakeFailure(Ui2SampleSlicesFailure failure) const {
    Ui2SampleSlicesCommand command =
        MakeCommand(Ui2SampleSlicesCommandType::OperationUnavailable);
    command.failure = failure;
    return command;
  }

  void RequestAutoSliceConfirmation(TrackerAction trigger) {
    dialogInput_ = {};
    dialogReleaseGate_.Reset();
    if (TrackerActionIsValid(trigger))
      input_.Update(trigger, false);
    dialogReleaseGate_.BlockUntilRelease(trigger);
    dialogSelectedAction_ = 0U;
    dialogActive_ = true;
    ++dialogInstanceId_;
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
  Ui2ControllerInputState dialogInput_{};
  Ui2InputReleaseGate dialogReleaseGate_{};
  Ui2WaveformSnapshot waveformPacket_{};
  std::array<std::uint32_t, SliceCapacity> slicePoints_{};
  std::uint16_t definedMask_ = 0U;
  std::uint16_t previewableMask_ = 0U;
  std::uint32_t previewPlayhead_ = 0U;
  std::uint8_t selectedSlice_ = 0U;
  std::uint8_t focusDigit_ = 6U;
  SampleSlicesViewUi2Focus focus_ = SampleSlicesViewUi2Focus::Waveform;
  Ui2SampleWaveformBuildResult lastBuild_ =
      Ui2SampleWaveformBuildResult::NotLoaded;
  bool active_ = false;
  bool waveformReady_ = false;
  bool previewActive_ = false;
  bool previewPlayheadVisible_ = false;
  bool dialogActive_ = false;
  std::uint8_t dialogSelectedAction_ = 0U;
  std::uint32_t dialogInstanceId_ = 0U;
};

static_assert(sizeof(Ui2SampleSlicesController) <= 1'200U);

} // namespace ui2
