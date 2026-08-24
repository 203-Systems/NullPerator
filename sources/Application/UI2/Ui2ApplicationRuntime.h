/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"
#include "Application/Views/Ui2BrowserSnapshot.h"
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
#include "UI2/Views/Song/UiSongView.h"
#include "UI2/Views/Table/UiTableView.h"

#include <array>
#include <cstdint>
#include <type_traits>

class AppWindow;

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
    const bool due = !initialized || wasPlaying ||
                     static_cast<std::uint32_t>(nowMs - lastSampleMs) >=
                         IntervalMs;
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

  [[nodiscard]] bool Supports(const AppWindow &window) const;
  [[nodiscard]] PresentResult Present(AppWindow &window);
  void Invalidate() {
    previousValid_ = false;
    dialogPreviousValid_ = false;
    cursorTargetValid_ = false;
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
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

  enum class RuntimePage : std::uint8_t {
    None,
    Song,
    Chain,
    Phrase,
    Table,
    Instrument,
    Device,
    Browser,
    Groove,
    Mixer
  };

  struct SongFrameState {
    std::array<char, 21> name{};
    std::array<char, 6> elapsed{};
    std::array<std::array<std::uint8_t, 8>, 16> rows{};
    std::array<std::array<char, 5>, 8> notes{};
    std::array<std::int8_t, 8> playbackRows{-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<std::uint8_t, 2> vuLevelTop{153, 153};
    std::uint8_t rowOffset = 0;
    std::uint8_t editRow = 0;
    std::uint8_t editTrack = 0;
    RectI16 cursorVisualRect{};
    RectI16 selectionVisualRect{};
    bool cursorVisualOverride = false;
    bool cursorInkVisible = true;
    bool playing = false;
    bool liveMode = false;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const SongFrameState &) const = default;
  };

  struct PhraseRowState {
    std::array<char, 5> note{};
    std::array<char, 4> instrument{};
    std::array<char, 4> fx1{};
    std::array<char, 5> parameter1{};
    std::array<char, 4> fx2{};
    std::array<char, 5> parameter2{};
    bool operator==(const PhraseRowState &) const = default;
  };

  struct ChainFrameState {
    std::array<char, 3> number{};
    std::array<char, 6> elapsed{};
    std::array<std::uint8_t, 16> phrases{};
    std::array<std::uint8_t, 16> transposes{};
    std::array<std::array<char, 5>, 8> trackNotes{};
    std::array<std::uint8_t, 2> vuLevelTop{148, 148};
    std::uint8_t editRow = 0;
    std::uint8_t editColumn = 0;
    std::int8_t selectedTrack = 0;
    RectI16 cursorVisualRect{};
    RectI16 selectionVisualRect{};
    RectI16 topMetaVisualRect{};
    RectI16 bottomTrackVisualRect{};
    bool cursorVisualOverride = false;
    bool topMetaVisualOverride = false;
    bool bottomTrackVisualOverride = false;
    bool cursorInkVisible = true;
    bool topMetaInkVisible = true;
    bool bottomTrackInkVisible = true;
    bool numberFocus = false;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const ChainFrameState &) const = default;
  };

  enum class PhraseContext : std::uint8_t { Hidden, Instrument, Fx };

  struct PhraseFrameState {
    std::array<char, 3> number{};
    std::array<char, 6> elapsed{};
    std::array<PhraseRowState, 16> rows{};
    std::array<std::array<char, 5>, 8> trackNotes{};
    std::array<char, 24> contextLead{};
    std::array<char, 24> contextTail{};
    std::array<char, 33> contextDescription{};
    std::uint8_t editRow = 0;
    std::uint8_t editColumn = 0;
    std::uint8_t editDigit = 3;
    std::int8_t selectedTrack = 0;
    UiPhraseHeader activeHeader = UiPhraseHeader::None;
    PhraseContext context = PhraseContext::Hidden;
    RectI16 cursorVisualRect{};
    RectI16 selectionVisualRect{};
    RectI16 topMetaVisualRect{};
    RectI16 bottomTrackVisualRect{};
    bool cursorVisualOverride = false;
    bool topMetaVisualOverride = false;
    bool bottomTrackVisualOverride = false;
    bool cursorInkVisible = true;
    bool topMetaInkVisible = true;
    bool bottomTrackInkVisible = true;
    bool enterDigitFocus = false;
    bool numberFocus = false;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const PhraseFrameState &) const = default;
  };

  struct TableRowState {
    std::array<char, 4> fx1{};
    std::array<char, 5> parameter1{};
    std::array<char, 4> fx2{};
    std::array<char, 5> parameter2{};
    std::array<char, 4> fx3{};
    std::array<char, 5> parameter3{};
    bool operator==(const TableRowState &) const = default;
  };

  struct TableFrameState {
    std::array<char, 4> number{};
    std::array<char, 6> elapsed{};
    std::array<TableRowState, 16> rows{};
    std::array<std::array<char, 5>, 8> trackNotes{};
    std::array<char, 24> contextLead{};
    std::array<char, 24> contextTail{};
    std::array<char, 33> contextDescription{};
    std::uint8_t editRow = 0;
    std::uint8_t editColumn = 0;
    std::uint8_t editDigit = 3;
    std::int8_t selectedTrack = 0;
    UiTableHeader activeHeader = UiTableHeader::None;
    PhraseContext context = PhraseContext::Hidden;
    RectI16 cursorVisualRect{};
    RectI16 selectionVisualRect{};
    RectI16 topMetaVisualRect{};
    RectI16 bottomTrackVisualRect{};
    bool cursorVisualOverride = false;
    bool topMetaVisualOverride = false;
    bool bottomTrackVisualOverride = false;
    bool cursorInkVisible = true;
    bool topMetaInkVisible = true;
    bool bottomTrackInkVisible = true;
    bool enterDigitFocus = false;
    bool numberFocus = false;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const TableFrameState &) const = default;
  };

  struct GrooveFrameState {
    std::array<char, 3> number{};
    std::array<std::uint8_t, 16> steps{};
    std::uint8_t editRow = 0;
    RectI16 cursorVisualRect{};
    bool cursorVisualOverride = false;
    bool cursorInkVisible = true;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const GrooveFrameState &) const = default;
  };

  struct InstrumentFieldState {
    std::array<char, 16> label{};
    std::array<char, 24> value{};
    std::int16_t y = 0;
    bool operator==(const InstrumentFieldState &) const = default;
  };

  struct InstrumentOperatorState {
    std::array<char, 16> label{};
    std::array<char, 8> op1{};
    std::array<char, 8> op2{};
    bool operator==(const InstrumentOperatorState &) const = default;
  };

  struct InstrumentFrameState {
    std::array<char, 3> number{};
    std::array<char, 6> elapsed{};
    std::array<char, 17> name{};
    std::array<InstrumentFieldState, 16> fields{};
    std::array<InstrumentOperatorState, 6> operators{};
    std::array<std::array<char, 5>, 8> trackNotes{};
    std::uint8_t fieldCount = 0;
    std::uint8_t operatorCount = 0;
    std::uint8_t selectedField = 0;
    std::uint8_t selectedOperator = 0;
    std::uint8_t nameAction = 0;
    std::int8_t selectedTrack = 0;
    UiInstrumentKind kind = UiInstrumentKind::None;
    UiInstrumentCursor cursor = UiInstrumentCursor::None;
    RectI16 cursorVisualRect{};
    RectI16 topMetaVisualRect{};
    RectI16 bottomTrackVisualRect{};
    bool cursorVisualOverride = false;
    bool topMetaVisualOverride = false;
    bool bottomTrackVisualOverride = false;
    bool cursorInkVisible = true;
    bool topMetaInkVisible = true;
    bool bottomTrackInkVisible = true;
    bool numberFocus = false;
    std::int16_t scrollOffset = 0;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const InstrumentFrameState &) const = default;
  };

  struct MixerFrameState {
    std::array<std::array<std::uint8_t, 2>, 9> vuLevelTop{};
    std::array<std::array<char, 4>, 9> volumes{};
    std::int8_t selectedChannel = 0;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const MixerFrameState &) const = default;
  };

  struct DeviceFrameState {
    std::array<char, 24> midiDevice{};
    std::array<char, 24> midiSync{};
    std::array<char, 24> lineOut{};
    std::array<char, 24> remoteUi{};
    std::array<char, 24> resampler{};
    std::array<char, 8> volume{};
    std::array<char, 8> brightness{};
    std::array<char, 24> theme{};
    std::array<char, 41> font{};
    std::array<char, 32> version{};
    std::array<std::array<char, 24>, 8> selectorOptions{};
    std::uint8_t selectorCount = 0;
    std::uint8_t selectorCurrent = 0;
    std::uint8_t batteryPercent = 0;
    UiDeviceCursor cursor = UiDeviceCursor::MidiDevice;
    RectI16 cursorVisualRect{};
    bool cursorVisualOverride = false;
    bool cursorInkVisible = true;
    bool selectorWrap = false;
    bool showLineOut = false;
    bool showVolume = true;
    bool showTheme = true;
    bool showFont = true;
    bool showUpdateFirmware = false;
    bool batteryPercentValid = false;
    std::int16_t scrollOffset = 0;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const DeviceFrameState &) const = default;
  };

  struct BrowserFrameState {
    Ui2BrowserSnapshot snapshot{};
    RectI16 cursorVisualRect{};
    bool cursorVisualOverride = false;
    bool cursorInkVisible = true;
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const BrowserFrameState &) const = default;
  };

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
  static_assert(std::is_trivially_copyable_v<DeviceFrameState> &&
                std::is_trivially_destructible_v<DeviceFrameState>);
  static_assert(std::is_trivially_copyable_v<BrowserFrameState> &&
                std::is_trivially_destructible_v<BrowserFrameState>);
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
    FramePair<DeviceFrameState> device;
    FramePair<BrowserFrameState> browser;
    FramePair<GrooveFrameState> groove;
    FramePair<MixerFrameState> mixer;

    FrameStorage() noexcept {}
    ~FrameStorage() noexcept {}
  };

  static_assert(sizeof(FrameStorage) < 2'100);

  static UiSongViewData ViewDataFor(const SongFrameState &state);
  static UiChainViewData ViewDataFor(const ChainFrameState &state);
  static UiPhraseViewData ViewDataFor(const PhraseFrameState &state);
  static UiTableViewData ViewDataFor(const TableFrameState &state);
  static UiInstrumentViewData
  ViewDataFor(const InstrumentFrameState &state);
  static UiDeviceViewData ViewDataFor(const DeviceFrameState &state);
  static UiBrowserViewData ViewDataFor(const BrowserFrameState &state);
  static UiGrooveViewData ViewDataFor(const GrooveFrameState &state);
  static UiMixerViewData ViewDataFor(const MixerFrameState &state);
  void CaptureSong(AppWindow &window, SongFrameState &state);
  void CaptureChain(AppWindow &window, ChainFrameState &state);
  void CapturePhrase(AppWindow &window, PhraseFrameState &state);
  void CaptureTable(AppWindow &window, TableFrameState &state);
  void CaptureInstrument(AppWindow &window, InstrumentFrameState &state);
  void CaptureDevice(AppWindow &window, DeviceFrameState &state);
  void CaptureBrowser(AppWindow &window, BrowserFrameState &state);
  void CaptureGroove(AppWindow &window, GrooveFrameState &state);
  void CaptureMixer(AppWindow &window, MixerFrameState &state);
  [[nodiscard]] PowerFrameState CapturePowerState(bool playing);
  [[nodiscard]] UiPowerState CurrentPowerState(bool playing);
  void CaptureDialog(AppWindow &window);
  void ActivatePage(RuntimePage page);
  [[nodiscard]] bool DialogChanged() const;
  [[nodiscard]] bool RequiresFullRebuild() const;
  [[nodiscard]] bool FullScreenDialogActive() const;
  [[nodiscard]] bool CanCommitHiddenBaseWithoutRender() const;
  [[nodiscard]] UiBuildStatus ApplyDialog();
  void RenderDialogDelta();
  void CommitDialog();
  [[nodiscard]] PresentResult PresentSong(AppWindow &window,
                                          std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentChain(AppWindow &window,
                                           std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentPhrase(AppWindow &window,
                                            std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentTable(AppWindow &window,
                                           std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentInstrument(AppWindow &window,
                                                std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentDevice(AppWindow &window,
                                            std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentBrowser(AppWindow &window,
                                             std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentGroove(AppWindow &window,
                                            std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentMixer(AppWindow &window);

  UiEngineStorage engineStorage_{};
  UiEngine engine_;
  UiFrameScene scene_{};
  FrameStorage frames_{};
  UiCursorAnimatorSet cursors_{};
  RectI16 cursorTarget_{};
  RectI16 topMetaTarget_{};
  RectI16 bottomTrackTarget_{};
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
  RuntimePage activePage_ = RuntimePage::None;
};

} // namespace ui2
