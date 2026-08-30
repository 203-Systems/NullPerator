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
#include <cstring>
#include <type_traits>

namespace ui2 {

enum class Ui2SampleEditorOperation : std::uint8_t { Trim, Normalize };

enum class Ui2SampleEditorCommandType : std::uint8_t {
  None,
  PreviewStart,
  PreviewStop,
  SetStart,
  SetEnd,
  RequestRename,
  RequestApplyOperation,
  RequestSave,
  RequestSaveAndLoad,
  RequestDiscard,
  NavigateBack,
};

// Commands describe intent only. Save/overwrite/trim/normalize/discard may
// require confirmation, progress, rollback, or error UI; those side effects
// deliberately remain outside the controller until those modal states have an
// approved design.
struct Ui2SampleEditorCommand {
  Ui2SampleEditorCommandType type = Ui2SampleEditorCommandType::None;
  Ui2SampleEditorOperation operation = Ui2SampleEditorOperation::Trim;
  std::array<char, PFILENAME_SIZE> path{};
  std::uint32_t value = 0U;
  std::uint32_t start = 0U;
  std::uint32_t end = 0U;
  bool singleCycle = false;
  bool projectPool = false;

  [[nodiscard]] bool HasValue() const {
    return type != Ui2SampleEditorCommandType::None;
  }
};

class Ui2SampleEditorController {
public:
  explicit Ui2SampleEditorController(Ui2SampleWaveformBackend &waveform)
      : waveform_(waveform) {}

  [[nodiscard]] Ui2SampleWaveformLoadResult
  OpenPath(FileSystem &fileSystem, const char *path, bool projectPool) {
    ResetController();
    const auto result = waveform_.LoadPath(fileSystem, path);
    if (result != Ui2SampleWaveformLoadResult::Loaded)
      return result;
    FinishOpen(path, projectPool);
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
    FinishOpen(sampleName, true);
    return result;
  }

  [[nodiscard]] Ui2SampleWaveformLoadResult
  OpenLibrary(FileSystem &fileSystem, const char *relativePath) {
    ResetController();
    const auto result = waveform_.LoadLibrary(fileSystem, relativePath);
    if (result != Ui2SampleWaveformLoadResult::Loaded)
      return result;
    FinishOpen(relativePath, false);
    return result;
  }

  void Close() {
    ResetController();
    waveform_.Reset();
  }

  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] SampleEditorViewUi2Focus Focus() const { return focus_; }
  [[nodiscard]] std::uint32_t Start() const { return start_; }
  [[nodiscard]] std::uint32_t End() const { return end_; }
  [[nodiscard]] std::uint16_t HeldMask() const { return input_.Mask(); }
  [[nodiscard]] Ui2SampleEditorOperation Operation() const {
    return operation_;
  }
  [[nodiscard]] Ui2SampleWaveformBuildResult LastWaveformBuild() const {
    return lastBuild_;
  }

  bool SetFocus(SampleEditorViewUi2Focus focus) {
    if (!FocusAvailable(focus))
      return false;
    focus_ = focus;
    return true;
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
    previewHeld_ = true;
    playing_ = true;
    previewPlayhead_ = sample;
    previewPlayheadVisible_ = true;
  }

  // Audio can stop outside Handle() (end-of-file, page transition, shutdown).
  // Clear the held-preview projection as one operation so a later key-up does
  // not emit a second stop and the top bar cannot remain in PLAYING state.
  void StopPreview() {
    previewHeld_ = false;
    playing_ = false;
    previewPlayhead_ = 0U;
    previewPlayheadVisible_ = false;
  }

  Ui2SampleEditorCommand Handle(TrackerAction action, bool pressed) {
    if (!active_ || !input_.Update(action, pressed))
      return {};

    if (!pressed) {
      if (action == TrackerAction::Play && previewHeld_) {
        Ui2SampleEditorCommand command =
            MakeCommand(Ui2SampleEditorCommandType::PreviewStop);
        StopPreview();
        return command;
      }
      return {};
    }

    if (action == TrackerAction::Play) {
      if (previewHeld_)
        return {};
      StartPreview(start_);
      Ui2SampleEditorCommand command =
          MakeCommand(Ui2SampleEditorCommandType::PreviewStart);
      command.start = start_;
      command.end = end_;
      command.singleCycle = IsSingleCycle();
      return command;
    }

    if (input_.Held(TrackerAction::Shift)) {
      if (action == TrackerAction::Left)
        return MakeCommand(Ui2SampleEditorCommandType::NavigateBack);
      return {};
    }

    const bool edit = input_.Held(TrackerAction::Edit);
    const bool option = input_.Held(TrackerAction::Option);

    // Legacy EDIT is M8 OPTION in the semantic input layer. Zoom is available
    // from every editor focus, as it was in SampleEditorView.
    if (option && (action == TrackerAction::Up ||
                   action == TrackerAction::Down)) {
      const std::int8_t delta = action == TrackerAction::Up ? 1 : -1;
      if (waveform_.AdjustZoom(delta, SelectedMarkerSample()))
        RebuildWaveform();
      return {};
    }

    if (focus_ == SampleEditorViewUi2Focus::Waveform) {
      if (option && action == TrackerAction::Left) {
        selectedMarker_ = 0U;
        CenterSelectedMarker();
        return {};
      }
      if (option && action == TrackerAction::Right) {
        selectedMarker_ = 1U;
        CenterSelectedMarker();
        return {};
      }
      if (edit && IsDirection(action))
        return MoveSelectedMarker(action);
    }

    if ((focus_ == SampleEditorViewUi2Focus::Start ||
         focus_ == SampleEditorViewUi2Focus::End) &&
        IsDirection(action)) {
      if (edit)
        return AdjustFocusedEndpoint(action);
      if (action == TrackerAction::Left && focusDigit_ > 0U)
        --focusDigit_;
      else if (action == TrackerAction::Right && focusDigit_ < 6U)
        ++focusDigit_;
      else if (action == TrackerAction::Up ||
               action == TrackerAction::Down)
        MoveFocus(action == TrackerAction::Down ? 1 : -1);
      return {};
    }

    if (focus_ == SampleEditorViewUi2Focus::Operation &&
        (action == TrackerAction::Left || action == TrackerAction::Right ||
         (edit && (action == TrackerAction::Up ||
                   action == TrackerAction::Down)))) {
      operation_ = operation_ == Ui2SampleEditorOperation::Trim
                       ? Ui2SampleEditorOperation::Normalize
                       : Ui2SampleEditorOperation::Trim;
      return {};
    }

    if (!edit && !option && action == TrackerAction::Up) {
      MoveFocus(-1);
      return {};
    }
    if (!edit && !option && action == TrackerAction::Down) {
      MoveFocus(1);
      return {};
    }
    if (!edit && !option &&
        (action == TrackerAction::Left || action == TrackerAction::Right) &&
        IsBottomFocus()) {
      MoveBottomFocus(action == TrackerAction::Right ? 1 : -1);
      return {};
    }
    if (action != TrackerAction::Edit)
      return {};

    switch (focus_) {
    case SampleEditorViewUi2Focus::Name:
      return FocusAvailable(focus_)
                 ? MakeCommand(Ui2SampleEditorCommandType::RequestRename)
                 : Ui2SampleEditorCommand{};
    case SampleEditorViewUi2Focus::Apply: {
      if (!FocusAvailable(focus_))
        return {};
      Ui2SampleEditorCommand command =
          MakeCommand(Ui2SampleEditorCommandType::RequestApplyOperation);
      command.operation = operation_;
      command.start = start_;
      command.end = end_;
      return command;
    }
    case SampleEditorViewUi2Focus::Save:
      return FocusAvailable(focus_)
                 ? MakeCommand(Ui2SampleEditorCommandType::RequestSave)
                 : Ui2SampleEditorCommand{};
    case SampleEditorViewUi2Focus::SaveAndLoad:
      return FocusAvailable(focus_)
                 ? MakeCommand(
                       Ui2SampleEditorCommandType::RequestSaveAndLoad)
                 : Ui2SampleEditorCommand{};
    case SampleEditorViewUi2Focus::Discard:
      return MakeCommand(Ui2SampleEditorCommandType::RequestDiscard);
    case SampleEditorViewUi2Focus::Start:
    case SampleEditorViewUi2Focus::End:
    case SampleEditorViewUi2Focus::Operation:
    case SampleEditorViewUi2Focus::Waveform:
    case SampleEditorViewUi2Focus::Unknown:
      return {};
    }
    return {};
  }

  [[nodiscard]] SampleEditorViewUi2Snapshot Snapshot() const {
    SampleEditorViewUi2Snapshot snapshot;
    snapshot.name = name_;
    std::snprintf(snapshot.start.data(), snapshot.start.size(), "%07X",
                  static_cast<unsigned>(start_));
    std::snprintf(snapshot.end.data(), snapshot.end.size(), "%07X",
                  static_cast<unsigned>(end_));
    CopyUi2SnapshotText(
        snapshot.operation,
        operation_ == Ui2SampleEditorOperation::Trim ? "TRIM" : "NORMALIZE");
    snapshot.waveform = waveformPacket_;
    snapshot.waveformReady = waveformReady_;
    snapshot.focus = focus_;
    snapshot.focusDigit = focusDigit_;
    snapshot.playing = playing_;
    snapshot.singleCycle = IsSingleCycle();
    snapshot.projectPool = projectPool_;
    snapshot.fileMutationAvailable = waveform_.SupportsEditorTransactions();

    PushMarker(snapshot, start_, Ui2WaveformMarkerKind::Start,
               selectedMarker_ == 0U);
    PushMarker(snapshot, end_, Ui2WaveformMarkerKind::End,
               selectedMarker_ == 1U);
    if (previewPlayheadVisible_)
      PushMarker(snapshot, previewPlayhead_, Ui2WaveformMarkerKind::Playhead,
                 false);
    return snapshot;
  }

private:
  static constexpr std::array<SampleEditorViewUi2Focus, 9> kLibraryFocusOrder{
      SampleEditorViewUi2Focus::Name,
      SampleEditorViewUi2Focus::Waveform,
      SampleEditorViewUi2Focus::Start,
      SampleEditorViewUi2Focus::End,
      SampleEditorViewUi2Focus::Operation,
      SampleEditorViewUi2Focus::Apply,
      SampleEditorViewUi2Focus::Save,
      SampleEditorViewUi2Focus::SaveAndLoad,
      SampleEditorViewUi2Focus::Discard,
  };

  static bool IsDirection(TrackerAction action) {
    return action == TrackerAction::Left || action == TrackerAction::Right ||
           action == TrackerAction::Up || action == TrackerAction::Down;
  }

  [[nodiscard]] bool FocusAvailable(SampleEditorViewUi2Focus focus) const {
    if (focus == SampleEditorViewUi2Focus::Name ||
        focus == SampleEditorViewUi2Focus::Apply ||
        focus == SampleEditorViewUi2Focus::Save)
      return waveform_.SupportsEditorTransactions();
    if (focus == SampleEditorViewUi2Focus::SaveAndLoad)
      return waveform_.SupportsEditorTransactions() && !projectPool_;
    return focus != SampleEditorViewUi2Focus::Unknown;
  }

  [[nodiscard]] bool IsSingleCycle() const {
    // AudioFileStreamer's fixed buffer is measured in interleaved int16
    // samples, not frames. Counting channels here prevents a short stereo WAV
    // from overflowing the 600-sample single-cycle buffer.
    return static_cast<std::uint64_t>(waveform_.FrameCount()) *
               waveform_.ChannelCount() <=
           Ui2SingleCycleMaximumFrames;
  }

  void ResetController() {
    input_ = {};
    name_.fill('\0');
    waveformPacket_ = {};
    start_ = end_ = previewPlayhead_ = 0U;
    focus_ = SampleEditorViewUi2Focus::Waveform;
    operation_ = Ui2SampleEditorOperation::Trim;
    selectedMarker_ = 0U;
    focusDigit_ = 0U;
    projectPool_ = active_ = waveformReady_ = false;
    previewHeld_ = playing_ = previewPlayheadVisible_ = false;
    lastBuild_ = Ui2SampleWaveformBuildResult::NotLoaded;
  }

  void FinishOpen(const char *displayPath, bool projectPool) {
    active_ = true;
    projectPool_ = projectPool;
    start_ = 0U;
    end_ = waveform_.FrameCount() - 1U;
    SetDisplayName(displayPath);
    waveform_.CenterOn(0U);
    RebuildWaveform();
  }

  void SetDisplayName(const char *path) {
    name_.fill('\0');
    if (path == nullptr)
      return;
    const char *leaf = path;
    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
      if (*cursor == '/' || *cursor == '\\')
        leaf = cursor + 1;
    }
    std::size_t length = std::strlen(leaf);
    if (length >= 4U && leaf[length - 4U] == '.' &&
        (leaf[length - 3U] == 'w' || leaf[length - 3U] == 'W') &&
        (leaf[length - 2U] == 'a' || leaf[length - 2U] == 'A') &&
        (leaf[length - 1U] == 'v' || leaf[length - 1U] == 'V'))
      length -= 4U;
    std::memcpy(name_.data(), leaf,
                std::min<std::size_t>(length, name_.size() - 1U));
  }

  void RebuildWaveform() {
    lastBuild_ = waveform_.BuildMask(
        waveformPacket_, Ui2SampleWaveformBackend::EditorMaskHeight);
    waveformReady_ = lastBuild_ == Ui2SampleWaveformBuildResult::Built;
  }

  [[nodiscard]] std::uint32_t SelectedMarkerSample() const {
    return selectedMarker_ == 0U ? start_ : end_;
  }

  void CenterSelectedMarker() {
    if (waveform_.CenterOn(SelectedMarkerSample()))
      RebuildWaveform();
  }

  Ui2SampleEditorCommand MoveSelectedMarker(TrackerAction action) {
    const std::uint32_t span = waveform_.ViewEnd() - waveform_.ViewStart();
    std::int64_t delta =
        action == TrackerAction::Left || action == TrackerAction::Right
            ? std::max<std::uint32_t>(1U, span / 64U)
            : std::max<std::uint32_t>(1U, span / 16U);
    if (action == TrackerAction::Left || action == TrackerAction::Down)
      delta = -delta;
    return SetEndpoint(selectedMarker_ == 0U, delta);
  }

  Ui2SampleEditorCommand AdjustFocusedEndpoint(TrackerAction action) {
    if (action == TrackerAction::Left || action == TrackerAction::Right) {
      if (action == TrackerAction::Left && focusDigit_ > 0U)
        --focusDigit_;
      else if (action == TrackerAction::Right && focusDigit_ < 6U)
        ++focusDigit_;
      return {};
    }
    std::uint32_t step = 1U;
    for (std::uint8_t digit = focusDigit_; digit < 6U; ++digit)
      step *= 16U;
    const std::int64_t delta =
        action == TrackerAction::Up ? static_cast<std::int64_t>(step)
                                    : -static_cast<std::int64_t>(step);
    selectedMarker_ = focus_ == SampleEditorViewUi2Focus::Start ? 0U : 1U;
    return SetEndpoint(selectedMarker_ == 0U, delta);
  }

  Ui2SampleEditorCommand SetEndpoint(bool startEndpoint, std::int64_t delta) {
    const std::int64_t maximum =
        static_cast<std::int64_t>(waveform_.FrameCount() - 1U);
    if (startEndpoint) {
      const std::uint32_t next = static_cast<std::uint32_t>(
          std::clamp<std::int64_t>(static_cast<std::int64_t>(start_) + delta,
                                   0, static_cast<std::int64_t>(end_)));
      if (next == start_)
        return {};
      start_ = next;
      CenterSelectedMarker();
      Ui2SampleEditorCommand command =
          MakeCommand(Ui2SampleEditorCommandType::SetStart);
      command.value = start_;
      return command;
    }
    const std::uint32_t next = static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(static_cast<std::int64_t>(end_) + delta,
                                 static_cast<std::int64_t>(start_), maximum));
    if (next == end_)
      return {};
    end_ = next;
    CenterSelectedMarker();
    Ui2SampleEditorCommand command =
        MakeCommand(Ui2SampleEditorCommandType::SetEnd);
    command.value = end_;
    return command;
  }

  void MoveFocus(int delta) {
    std::array<SampleEditorViewUi2Focus, kLibraryFocusOrder.size()> order{};
    std::size_t count = 0U;
    for (SampleEditorViewUi2Focus candidate : kLibraryFocusOrder) {
      if (FocusAvailable(candidate))
        order[count++] = candidate;
    }
    std::size_t current = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
      if (order[index] == focus_) {
        current = index;
        break;
      }
    }
    const int next = (static_cast<int>(current) + delta +
                      static_cast<int>(count)) %
                     static_cast<int>(count);
    focus_ = order[static_cast<std::size_t>(next)];
    if (focus_ == SampleEditorViewUi2Focus::Start)
      selectedMarker_ = 0U;
    else if (focus_ == SampleEditorViewUi2Focus::End)
      selectedMarker_ = 1U;
  }

  [[nodiscard]] bool IsBottomFocus() const {
    return focus_ == SampleEditorViewUi2Focus::Save ||
           focus_ == SampleEditorViewUi2Focus::SaveAndLoad ||
           focus_ == SampleEditorViewUi2Focus::Discard;
  }

  void MoveBottomFocus(int delta) {
    if (!waveform_.SupportsEditorTransactions()) {
      focus_ = SampleEditorViewUi2Focus::Discard;
      return;
    }
    constexpr std::array<SampleEditorViewUi2Focus, 3> library{
        SampleEditorViewUi2Focus::Save,
        SampleEditorViewUi2Focus::SaveAndLoad,
        SampleEditorViewUi2Focus::Discard};
    constexpr std::array<SampleEditorViewUi2Focus, 2> pool{
        SampleEditorViewUi2Focus::Save, SampleEditorViewUi2Focus::Discard};
    if (projectPool_) {
      const std::size_t current = focus_ == pool[1] ? 1U : 0U;
      focus_ = pool[(static_cast<int>(current) + delta + 2) % 2];
    } else {
      std::size_t current = focus_ == library[1] ? 1U : focus_ == library[2]
                                                            ? 2U
                                                            : 0U;
      focus_ = library[(static_cast<int>(current) + delta + 3) % 3];
    }
  }

  Ui2SampleEditorCommand
  MakeCommand(Ui2SampleEditorCommandType type) const {
    Ui2SampleEditorCommand command;
    command.type = type;
    command.operation = operation_;
    std::snprintf(command.path.data(), command.path.size(), "%s",
                  waveform_.Path());
    command.start = start_;
    command.end = end_;
    command.singleCycle = IsSingleCycle();
    command.projectPool = projectPool_;
    return command;
  }

  void PushMarker(SampleEditorViewUi2Snapshot &snapshot, std::uint32_t sample,
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
  std::array<char, 33> name_{};
  Ui2WaveformSnapshot waveformPacket_{};
  std::uint32_t start_ = 0U;
  std::uint32_t end_ = 0U;
  std::uint32_t previewPlayhead_ = 0U;
  SampleEditorViewUi2Focus focus_ = SampleEditorViewUi2Focus::Waveform;
  Ui2SampleEditorOperation operation_ = Ui2SampleEditorOperation::Trim;
  Ui2SampleWaveformBuildResult lastBuild_ =
      Ui2SampleWaveformBuildResult::NotLoaded;
  std::uint8_t selectedMarker_ = 0U;
  std::uint8_t focusDigit_ = 0U;
  bool projectPool_ = false;
  bool active_ = false;
  bool waveformReady_ = false;
  bool previewHeld_ = false;
  bool playing_ = false;
  bool previewPlayheadVisible_ = false;
};

static_assert(sizeof(Ui2SampleEditorController) <= 1'200U);

} // namespace ui2
