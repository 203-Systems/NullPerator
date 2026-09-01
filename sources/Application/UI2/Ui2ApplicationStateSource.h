/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Persistency/PersistenceConstants.h"
#include "Application/UI2/Ui2SampleAdapters.h"
#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"
#include "Application/Views/Ui2BrowserSnapshot.h"
#include "Application/Views/Ui2RecordSnapshot.h"
#include "UI2/Views/Chain/UiChainView.h"
#include "UI2/Views/Device/UiDeviceView.h"
#include "UI2/Views/Font/UiFontView.h"
#include "UI2/Views/Instrument/UiInstrumentView.h"
#include "UI2/Views/Mixer/UiMixerView.h"
#include "UI2/Views/Phrase/UiPhraseView.h"
#include "UI2/Views/Project/UiProjectView.h"
#include "UI2/Views/Table/UiTableView.h"
#include "UI2/Views/Theme/UiThemeView.h"

#include <array>
#include <cstdint>

namespace ui2 {

// Pages understood by the shared application renderer. This intentionally is
// not the legacy ViewType enum: state producers may be native UI2 controllers,
// test fixtures, or the compatibility adapter for the current AppWindow.
enum class UiApplicationPage : std::uint8_t {
  None,
  Song,
  Chain,
  Phrase,
  Table,
  Instrument,
  Project,
  Device,
  Theme,
  Font,
  Browser,
  Groove,
  Mixer,
  SampleEditor,
  SampleSlices,
  Record
};

struct UiApplicationBatteryState {
  std::uint8_t percentage = 0;
  bool available = false;
  bool charging = false;
};

// Capture methods return whether activity on the page should replace the
// normal battery presentation (playback or a sample preview).
struct UiApplicationActivityState {
  bool active = false;
};

struct UiSongFrameState {
  std::array<char, 21> name{};
  std::array<char, 6> elapsed{};
  std::array<std::array<std::uint8_t, 8>, 16> rows{};
  std::array<std::array<char, 5>, 8> notes{};
  std::array<std::int8_t, 8> playbackRows{-1, -1, -1, -1, -1, -1, -1, -1};
  std::array<std::int8_t, 8> queuedRows{-1, -1, -1, -1, -1, -1, -1, -1};
  std::array<bool, 8> mutedTracks{};
  std::array<std::uint8_t, 2> vuLevelTop{153, 153};
  std::uint8_t rowOffset = 0;
  std::uint8_t editRow = 0;
  std::uint8_t editTrack = 0;
  RectI16 cursorVisualRect{};
  RectI16 selectionVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  bool adjustmentFocus = false;
  bool modeFocus = false;
  bool selectionActive = false;
  bool selectionNextExpansionAll = false;
  bool clipboardReady = false;
  bool clipboardPasted = false;
  std::uint8_t clipboardWidth = 0;
  std::uint8_t clipboardHeight = 0;
  bool navigationHeld = false;
  bool playing = false;
  bool liveMode = false;
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiSongFrameState &) const = default;
};

struct UiPhraseRowFrameState {
  std::array<char, 5> note{};
  std::array<char, 4> instrument{};
  std::array<char, 4> fx1{};
  std::array<char, 5> parameter1{};
  std::array<char, 4> fx2{};
  std::array<char, 5> parameter2{};
  bool operator==(const UiPhraseRowFrameState &) const = default;
};

struct UiChainFrameState {
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
  bool adjustmentFocus = false;
  bool selectionActive = false;
  bool selectionNextExpansionAll = false;
  bool clipboardReady = false;
  bool clipboardPasted = false;
  std::uint8_t clipboardWidth = 0;
  std::uint8_t clipboardHeight = 0;
  bool navigationHeld = false;
  std::array<std::int8_t, 8> playbackRows{-1, -1, -1, -1, -1, -1, -1, -1};
  std::array<bool, 8> mutedTracks{};
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiChainFrameState &) const = default;
};

enum class UiPhraseContext : std::uint8_t { Hidden, Instrument, Fx };

struct UiPhraseFrameState {
  std::array<char, 3> number{};
  std::array<char, 6> elapsed{};
  std::array<UiPhraseRowFrameState, 16> rows{};
  std::array<std::array<char, 5>, 8> trackNotes{};
  std::array<char, 24> contextLead{};
  std::array<char, 24> contextTail{};
  std::array<char, 33> contextDescription{};
  std::uint8_t editRow = 0;
  std::uint8_t editColumn = 0;
  std::uint8_t editDigit = 3;
  std::int8_t selectedTrack = 0;
  UiPhraseHeader activeHeader = UiPhraseHeader::None;
  UiPhraseContext context = UiPhraseContext::Hidden;
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
  bool adjustmentFocus = false;
  bool selectionActive = false;
  bool selectionNextExpansionAll = false;
  bool clipboardReady = false;
  bool clipboardPasted = false;
  std::uint8_t clipboardWidth = 0;
  std::uint8_t clipboardHeight = 0;
  bool navigationHeld = false;
  std::array<std::int8_t, 8> playbackRows{-1, -1, -1, -1, -1, -1, -1, -1};
  std::array<bool, 8> mutedTracks{};
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiPhraseFrameState &) const = default;
};

struct UiTableRowFrameState {
  std::array<char, 4> fx1{};
  std::array<char, 5> parameter1{};
  std::array<char, 4> fx2{};
  std::array<char, 5> parameter2{};
  std::array<char, 4> fx3{};
  std::array<char, 5> parameter3{};
  bool operator==(const UiTableRowFrameState &) const = default;
};

struct UiTableFrameState {
  std::array<char, 4> number{};
  std::array<char, 6> elapsed{};
  std::array<UiTableRowFrameState, 16> rows{};
  std::array<std::array<char, 5>, 8> trackNotes{};
  std::array<char, 24> contextLead{};
  std::array<char, 24> contextTail{};
  std::array<char, 33> contextDescription{};
  std::uint8_t editRow = 0;
  std::uint8_t editColumn = 0;
  std::uint8_t editDigit = 3;
  std::int8_t selectedTrack = 0;
  UiTableHeader activeHeader = UiTableHeader::None;
  UiPhraseContext context = UiPhraseContext::Hidden;
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
  bool adjustmentFocus = false;
  bool selectionActive = false;
  bool selectionNextExpansionAll = false;
  bool clipboardReady = false;
  bool clipboardPasted = false;
  std::uint8_t clipboardWidth = 0;
  std::uint8_t clipboardHeight = 0;
  bool navigationHeld = false;
  std::array<std::int8_t, 3> playbackRows{-1, -1, -1};
  std::array<std::int8_t, 3> automationPlaybackRows{-1, -1, -1};
  bool selectedTrackMuted = false;
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiTableFrameState &) const = default;
};

struct UiGrooveFrameState {
  std::array<char, 3> number{};
  std::array<std::uint8_t, 16> steps{};
  std::array<std::array<char, 5>, 8> trackNotes{};
  std::uint8_t editRow = 0;
  std::int8_t playbackRow = -1;
  bool selectedTrackMuted = false;
  RectI16 cursorVisualRect{};
  RectI16 selectionVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  bool selectionActive = false;
  bool selectionNextExpansionAll = false;
  bool clipboardReady = false;
  bool clipboardPasted = false;
  bool interpolationCompleted = false;
  std::uint8_t clipboardWidth = 0;
  std::uint8_t clipboardHeight = 0;
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiGrooveFrameState &) const = default;
};

struct UiInstrumentFieldFrameState {
  std::array<char, 16> label{};
  std::array<char, 24> value{};
  std::int16_t y = 0;
  bool userData = false;
  bool operator==(const UiInstrumentFieldFrameState &) const = default;
};

struct UiInstrumentOperatorFrameState {
  std::array<char, 16> label{};
  std::array<char, 8> op1{};
  std::array<char, 8> op2{};
  bool operator==(const UiInstrumentOperatorFrameState &) const = default;
};

struct UiInstrumentFrameState {
  std::array<char, 3> number{};
  std::array<char, 6> elapsed{};
  std::array<char, 17> name{};
  std::array<UiInstrumentFieldFrameState, kUiInstrumentMaximumFields> fields{};
  std::array<UiInstrumentOperatorFrameState, 6> operators{};
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
  bool enterSubfieldFocus = false;
  bool adjustmentFocus = false;
  bool adjustmentNote = false;
  UiInstrumentFieldBottom fieldBottom = UiInstrumentFieldBottom::Hidden;
  std::uint8_t fieldOptionCurrent = 0;
  UiInstrumentFieldOptions fieldOptions = UiInstrumentFieldOptions::None;
  bool fieldOptionWrap = false;
  std::uint8_t adjustmentFineStep = 1;
  std::uint8_t adjustmentCoarseStep = 10;
  std::uint8_t selectedSubfield = 0;
  std::uint8_t subfieldTextOffset = 0;
  std::int16_t scrollOffset = 0;
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiInstrumentFrameState &) const = default;
};

struct UiMixerFrameState {
  std::array<std::array<std::uint8_t, 2>, 9> vuLevelTop{};
  std::array<std::array<char, 4>, 9> volumes{};
  std::int8_t selectedChannel = 0;
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiMixerFrameState &) const = default;
};

struct UiProjectFrameState {
  std::array<char, MAX_PROJECT_NAME_LENGTH + 1> name{};
  std::array<char, 16> tempo{};
  std::array<char, 8> transpose{};
  std::array<char, 24> scale{};
  std::array<char, 8> root{};
  std::array<std::array<char, 24>, 5> selectorOptions{};
  std::uint8_t selectorCount = 0;
  std::uint8_t selectorCurrent = 0;
  UiProjectCursor cursor = UiProjectCursor::Name;
  std::uint8_t nameAction = 0;
  std::uint8_t sampleAction = 0;
  std::uint8_t renderOption = 0;
  bool selectorWrap = false;
  bool enterHeld = false;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  std::int16_t scrollOffset = 0;
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiProjectFrameState &) const = default;
};

struct UiDeviceFrameState {
  std::array<char, 24> midiDevice{};
  std::array<char, 24> midiSync{};
  std::array<char, 24> lineOut{};
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
  bool enterHeld = false;
  bool showMidiDevice = true;
  bool showLineOut = false;
  bool showVolume = true;
  bool showBrightness = true;
  bool showTheme = true;
  bool showFont = true;
  bool showUpdateFirmware = false;
  bool batteryPercentValid = false;
  std::int16_t scrollOffset = 0;
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiDeviceFrameState &) const = default;
};

struct UiBrowserFrameState {
  Ui2BrowserSnapshot snapshot{};
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;

  bool operator==(const UiBrowserFrameState &) const = default;
};

struct UiThemeFrameState {
  UiThemeViewState view{};
  std::array<std::uint32_t, UiPalette::kUserColorCount> colors{};
  bool colorsValid = false;

  bool operator==(const UiThemeFrameState &) const = default;
};

using UiFontFrameState = UiFontViewState;

struct UiRecordFrameState {
  RecordViewUi2Snapshot snapshot{};
  RectI16 cursorVisualRect{};
  UiPowerState power = UiPowerState::BatteryNormal;
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;

  bool operator==(const UiRecordFrameState &) const = default;
};

using UiSampleEditorFrameState = UiSampleEditorControllerState;
using UiSampleSlicesFrameState = UiSampleSlicesControllerState;

// Synchronization boundary between mutable application/controller state and
// the retained UI2 renderer. Implementations capture one owned, fixed-capacity
// frame packet on the application thread; no legacy View or model reference is
// exposed to UiApplicationRuntime.
class IUiApplicationStateSource {
public:
  virtual ~IUiApplicationStateSource() = default;

  [[nodiscard]] virtual UiApplicationPage ActivePage() const = 0;
  [[nodiscard]] virtual std::uint32_t NowMs() const = 0;
  [[nodiscard]] virtual UiApplicationBatteryState ReadBattery() const = 0;
  [[nodiscard]] virtual bool PersistenceSaving() const { return false; }
  [[nodiscard]] virtual bool NavigationHeld() const { return false; }
  [[nodiscard]] virtual UiTextCaseMode TextCase() const {
    return UiTextCaseMode::Upper;
  }

  [[nodiscard]] virtual bool HasDialog() const = 0;
  [[nodiscard]] virtual Ui2DialogSnapshot DialogSnapshot() const = 0;
  [[nodiscard]] virtual std::uint32_t DialogInstanceId() const = 0;

  [[nodiscard]] virtual UiApplicationActivityState
  CaptureSong(UiSongFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureChain(UiChainFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CapturePhrase(UiPhraseFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureTable(UiTableFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureInstrument(UiInstrumentFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureProject(UiProjectFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureDevice(UiDeviceFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureTheme(UiThemeFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureFont(UiFontFrameState &state) {
    state = {};
    return {};
  }
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureBrowser(UiBrowserFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureGroove(UiGrooveFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureMixer(UiMixerFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureSampleEditor(UiSampleEditorFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureSampleSlices(UiSampleSlicesFrameState &state) = 0;
  [[nodiscard]] virtual UiApplicationActivityState
  CaptureRecord(UiRecordFrameState &state) = 0;
};

} // namespace ui2
