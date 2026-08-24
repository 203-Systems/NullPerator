/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Ui2ApplicationStateSource.h"
#include "UI2/Animation/UiAnimatedRect.h"
#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/UiEngine.h"
#include "UI2/Views/Browser/UiBrowserView.h"
#include "UI2/Views/Chain/UiChainView.h"
#include "UI2/Views/Device/UiDeviceView.h"
#include "UI2/Views/Dialog/UiDialogView.h"
#include "UI2/Views/Groove/UiGrooveView.h"
#include "UI2/Views/Instrument/UiInstrumentView.h"
#include "UI2/Views/Mixer/UiMixerView.h"
#include "UI2/Views/Phrase/UiPhraseView.h"
#include "UI2/Views/Project/UiProjectView.h"
#include "UI2/Views/Record/UiRecordView.h"
#include "UI2/Views/Sample/UiSampleViews.h"
#include "UI2/Views/Song/UiSongView.h"
#include "UI2/Views/Table/UiTableView.h"
#include "UI2/Views/Theme/UiThemeView.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace ui2 {

namespace detail {

// UI2 is presented at roughly 30 Hz, but an ESP32 battery sample performs ADC
// and board-I/O reads. Keep the scheduling policy tiny and deterministic so
// every page can share one sample without adding a timer or heap state.
struct UiBatterySampleGate {
  static constexpr std::uint32_t IntervalMs = 1'000U;

  [[nodiscard]] bool ShouldSample(bool playing, std::uint32_t nowMs) {
    if (playing) {
      wasPlaying = true;
      return false;
    }
    const bool due =
        !initialized || wasPlaying ||
        static_cast<std::uint32_t>(nowMs - lastSampleMs) >= IntervalMs;
    wasPlaying = false;
    if (due) {
      initialized = true;
      lastSampleMs = nowMs;
    }
    return due;
  }

  std::uint32_t lastSampleMs = 0;
  bool initialized = false;
  bool wasPlaying = false;
};

static_assert(std::is_trivially_copyable_v<UiBatterySampleGate>);
static_assert(sizeof(UiBatterySampleGate) <= 8U);

} // namespace detail

// Platform-neutral application-thread controller. Both WASM and firmware feed
// the same PicoTracker model into the same UI2 scene and raster pipeline; only
// IUiPresenter differs between targets.
class UiApplicationRuntime final {
public:
  explicit UiApplicationRuntime(IUiPresenter &presenter)
      : engine_(engineStorage_, presenter) {}

  [[nodiscard]] bool Supports(const IUiApplicationStateSource &source) const;
  [[nodiscard]] PresentResult Present(IUiApplicationStateSource &source);
  void Invalidate() {
    previousValid_ = false;
    dialogPreviousValid_ = false;
    cursorTargetValid_ = false;
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    dialogCursorTargetValid_ = false;
    activePage_ = RuntimePage::None;
  }

  // Integer-only approximation of the legacy -60 dB..0 dB meter mapping.
  // VU fill is deliberately not part of the pixel-exact golden contract.
  static constexpr std::uint8_t VuTopFromAmplitude(std::uint16_t amplitude) {
    if (amplitude < 33U)
      return 153U;
    if (amplitude >= 32700U)
      return 0U;
    std::uint8_t exponent = 0;
    std::uint16_t value = amplitude;
    while (value > 1U) {
      value >>= 1U;
      ++exponent;
    }
    const std::uint32_t base = 1U << exponent;
    const std::uint32_t fraction =
        ((static_cast<std::uint32_t>(amplitude) - base) * 16U) / base;
    const std::uint32_t steps =
        (static_cast<std::uint32_t>(exponent - 5U) * 16U) + fraction;
    const std::uint32_t active = (steps * 153U) / 160U;
    return static_cast<std::uint8_t>(153U - active);
  }

private:
  struct PowerFrameState {
    UiPowerState power = UiPowerState::BatteryNormal;
    std::uint8_t batteryPercent = 0;
    bool batteryPercentValid = false;
  };

  using RuntimePage = UiApplicationPage;
  using SongFrameState = UiSongFrameState;
  using ChainFrameState = UiChainFrameState;
  using PhraseFrameState = UiPhraseFrameState;
  using TableFrameState = UiTableFrameState;
  using GrooveFrameState = UiGrooveFrameState;
  using InstrumentFrameState = UiInstrumentFrameState;
  using MixerFrameState = UiMixerFrameState;
  using ProjectFrameState = UiProjectFrameState;
  using DeviceFrameState = UiDeviceFrameState;
  using BrowserFrameState = UiBrowserFrameState;
  using ThemeFrameState = UiThemeFrameState;
  using RecordFrameState = UiRecordFrameState;
  using SampleEditorFrameState = UiSampleEditorFrameState;
  using SampleSlicesFrameState = UiSampleSlicesFrameState;

  struct DialogFrameState {
    Ui2DialogSnapshot snapshot{};
    std::uint32_t instanceId = 0;
    bool active = false;

    bool operator==(const DialogFrameState &) const = default;
  };

  template <typename State> struct FramePair {
    State previous{};
    State current{};
  };

  static_assert(std::is_trivially_copyable_v<SongFrameState> &&
                std::is_trivially_destructible_v<SongFrameState>);
  static_assert(std::is_trivially_copyable_v<ChainFrameState> &&
                std::is_trivially_destructible_v<ChainFrameState>);
  static_assert(std::is_trivially_copyable_v<PhraseFrameState> &&
                std::is_trivially_destructible_v<PhraseFrameState>);
  static_assert(std::is_trivially_copyable_v<TableFrameState> &&
                std::is_trivially_destructible_v<TableFrameState>);
  static_assert(std::is_trivially_copyable_v<GrooveFrameState> &&
                std::is_trivially_destructible_v<GrooveFrameState>);
  static_assert(std::is_trivially_copyable_v<InstrumentFrameState> &&
                std::is_trivially_destructible_v<InstrumentFrameState>);
  static_assert(std::is_trivially_copyable_v<MixerFrameState> &&
                std::is_trivially_destructible_v<MixerFrameState>);
  static_assert(std::is_trivially_copyable_v<ProjectFrameState> &&
                std::is_trivially_destructible_v<ProjectFrameState>);
  static_assert(std::is_trivially_copyable_v<DeviceFrameState> &&
                std::is_trivially_destructible_v<DeviceFrameState>);
  static_assert(std::is_trivially_copyable_v<BrowserFrameState> &&
                std::is_trivially_destructible_v<BrowserFrameState>);
  static_assert(std::is_trivially_copyable_v<ThemeFrameState> &&
                std::is_trivially_destructible_v<ThemeFrameState>);
  static_assert(std::is_trivially_copyable_v<RecordFrameState> &&
                std::is_trivially_destructible_v<RecordFrameState>);
  static_assert(std::is_trivially_copyable_v<SampleEditorFrameState> &&
                std::is_trivially_destructible_v<SampleEditorFrameState>);
  static_assert(std::is_trivially_copyable_v<SampleSlicesFrameState> &&
                std::is_trivially_destructible_v<SampleSlicesFrameState>);
  static_assert(std::is_trivially_copyable_v<DialogFrameState> &&
                std::is_trivially_destructible_v<DialogFrameState>);

  // Exactly one member is active, selected by activePage_. Every frame state
  // is fixed-capacity and trivially destructible, so changing page can begin
  // the next pair's lifetime in place without heap allocation.
  union FrameStorage {
    FramePair<SongFrameState> song;
    FramePair<ChainFrameState> chain;
    FramePair<PhraseFrameState> phrase;
    FramePair<TableFrameState> table;
    FramePair<InstrumentFrameState> instrument;
    FramePair<ProjectFrameState> project;
    FramePair<DeviceFrameState> device;
    FramePair<ThemeFrameState> theme;
    FramePair<BrowserFrameState> browser;
    FramePair<GrooveFrameState> groove;
    FramePair<MixerFrameState> mixer;
    FramePair<SampleEditorFrameState> sampleEditor;
    FramePair<SampleSlicesFrameState> sampleSlices;
    FramePair<RecordFrameState> record;

    FrameStorage() noexcept {}
    ~FrameStorage() noexcept {}
  };

  // The slice page owns a pair of compressed 222x78 waveform packets. This is
  // the largest retained page, but remains a fixed, allocation-free block.
  static_assert(sizeof(FrameStorage) < 2'700);

  static UiSongViewData ViewDataFor(const SongFrameState &state);
  static UiChainViewData ViewDataFor(const ChainFrameState &state);
  static UiPhraseViewData ViewDataFor(const PhraseFrameState &state);
  static UiTableViewData ViewDataFor(const TableFrameState &state);
  static UiInstrumentViewData ViewDataFor(const InstrumentFrameState &state);
  static UiProjectViewData ViewDataFor(const ProjectFrameState &state);
  static UiDeviceViewData ViewDataFor(const DeviceFrameState &state);
  static UiThemeViewData ViewDataFor(const ThemeFrameState &state);
  static UiBrowserViewData ViewDataFor(const BrowserFrameState &state);
  static UiGrooveViewData ViewDataFor(const GrooveFrameState &state);
  static UiMixerViewData ViewDataFor(const MixerFrameState &state);
  static UiSampleEditorViewData
  ViewDataFor(const SampleEditorFrameState &state);
  static UiSampleSlicesViewData
  ViewDataFor(const SampleSlicesFrameState &state);
  static UiRecordViewData ViewDataFor(const RecordFrameState &state);
  [[nodiscard]] PowerFrameState
  CapturePowerState(IUiApplicationStateSource &source, bool playing);
  [[nodiscard]] UiPowerState
  CurrentPowerState(IUiApplicationStateSource &source, bool playing);
  void CaptureDialog(IUiApplicationStateSource &source);
  void ActivatePage(RuntimePage page);
  [[nodiscard]] bool DialogChanged() const;
  [[nodiscard]] bool RequiresFullRebuild() const;
  [[nodiscard]] bool FullScreenDialogActive() const;
  [[nodiscard]] bool CanCommitHiddenBaseWithoutRender() const;
  [[nodiscard]] UiBuildStatus ApplyDialog();
  void RenderDialogDelta();
  void CommitDialog();
  [[nodiscard]] PresentResult PresentSong(IUiApplicationStateSource &source,
                                          std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentChain(IUiApplicationStateSource &source,
                                           std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentPhrase(IUiApplicationStateSource &source,
                                            std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentTable(IUiApplicationStateSource &source,
                                           std::uint32_t nowMs);
  [[nodiscard]] PresentResult
  PresentInstrument(IUiApplicationStateSource &source, std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentProject(IUiApplicationStateSource &source,
                                             std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentDevice(IUiApplicationStateSource &source,
                                            std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentTheme(IUiApplicationStateSource &source,
                                           std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentBrowser(IUiApplicationStateSource &source,
                                             std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentGroove(IUiApplicationStateSource &source,
                                            std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentMixer(IUiApplicationStateSource &source);
  [[nodiscard]] PresentResult
  PresentSampleEditor(IUiApplicationStateSource &source, std::uint32_t nowMs);
  [[nodiscard]] PresentResult
  PresentSampleSlices(IUiApplicationStateSource &source, std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentRecord(IUiApplicationStateSource &source,
                                            std::uint32_t nowMs);

  UiEngineStorage engineStorage_{};
  UiEngine engine_;
  UiFrameScene scene_{};
  FrameStorage frames_{};
  UiCursorAnimatorSet cursors_{};
  RectI16 cursorTarget_{};
  RectI16 topMetaTarget_{};
  RectI16 bottomTrackTarget_{};
  RectI16 dialogCursorTarget_{};
  DialogFrameState previousDialog_{};
  DialogFrameState currentDialog_{};
  PowerFrameState cachedPower_{};
  detail::UiBatterySampleGate batterySampleGate_{};
  std::uint32_t frameNowMs_ = 0;
  bool cursorTargetValid_ = false;
  bool topMetaTargetValid_ = false;
  bool bottomTrackTargetValid_ = false;
  bool previousValid_ = false;
  bool dialogPreviousValid_ = false;
  bool dialogCursorTargetValid_ = false;
  RuntimePage activePage_ = RuntimePage::None;
};

} // namespace ui2
