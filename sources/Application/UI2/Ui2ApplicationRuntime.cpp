/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2ApplicationRuntime.h"

#include <algorithm>
#include <memory>

namespace ui2 {
namespace {

constexpr std::uint16_t kSongCursorDurationMs = 120;
constexpr std::uint16_t kChainCursorDurationMs = 120;
constexpr std::uint16_t kPhraseCursorDurationMs = 120;
constexpr std::uint16_t kGrooveCursorDurationMs = 120;
constexpr std::uint16_t kListCursorDurationMs = 120;
constexpr std::uint16_t kDialogCursorDurationMs = 110;
constexpr std::uint32_t kSavingAnimationStepMs = 140;

} // namespace

void UiApplicationRuntime::ApplyThemeColors(
    const std::array<std::uint32_t, UiPalette::kUserColorCount> &colors) {
  std::array<Rgb888, UiPalette::kUserColorCount> unpacked{};
  for (std::size_t index = 0; index < colors.size(); ++index) {
    const std::uint32_t packed = colors[index];
    unpacked[index] = {
        static_cast<std::uint8_t>((packed >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((packed >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(packed & 0xFFU)};
  }
  engine_.Palette().SetUserColors(unpacked);
  Invalidate();
}

UiApplicationRuntime::PowerFrameState
UiApplicationRuntime::CapturePowerState(IUiApplicationStateSource &source,
                                        bool playing) {
  if (!batterySampleGate_.ShouldSample(playing, frameNowMs_)) {
    return playing ? PowerFrameState{.power = UiPowerState::Playing}
                   : cachedPower_;
  }

  // Treat an unavailable/error sample as invalid cached state. That preserves
  // the 1 Hz read ceiling while ensuring Web never renders a fabricated 0%.
  cachedPower_ = {};
  const UiApplicationBatteryState battery = source.ReadBattery();
  if (!battery.available)
    return cachedPower_;

  cachedPower_.batteryPercent = std::min<std::uint8_t>(
      battery.percentage, static_cast<std::uint8_t>(100));
  cachedPower_.batteryPercentValid = true;
  if (battery.charging)
    cachedPower_.power = UiPowerState::Charging;
  else if (battery.percentage <= 15U)
    cachedPower_.power = UiPowerState::BatteryLow;
  else if (battery.percentage >= 80U)
    cachedPower_.power = UiPowerState::BatteryHigh;
  return cachedPower_;
}

UiPowerState
UiApplicationRuntime::CurrentPowerState(IUiApplicationStateSource &source,
                                        bool playing) {
  if (source.PersistenceSaving())
    return static_cast<UiPowerState>(
        static_cast<std::uint8_t>(UiPowerState::Saving) +
        (frameNowMs_ / kSavingAnimationStepMs) % 4U);
  if (source.NavigationHeld())
    return UiPowerState::Navigation;
  return CapturePowerState(source, playing).power;
}

void UiApplicationRuntime::UpdateNavigationCursor(
    UiNavCursorModel &cursor, IUiApplicationStateSource &source,
    UiNavTarget target, std::uint32_t nowMs) {
  cursor = {};
  if (!source.NavigationHeld()) {
    navigationCursorTargetValid_ = false;
    return;
  }

  const RectI16 targetRect = UiChromeRenderer::NavTargetRect(target);
  if (!navigationCursorTargetValid_) {
    cursors_.Snap(UiCursorRole::ChromeNavigation, targetRect, nowMs);
    navigationCursorTarget_ = targetRect;
    navigationCursorTargetValid_ = true;
  } else if (targetRect != navigationCursorTarget_) {
    cursors_.Retarget(UiCursorRole::ChromeNavigation, targetRect, nowMs,
                      kSongCursorDurationMs);
    navigationCursorTarget_ = targetRect;
  }
  cursor.selectionRect = cursors_.Sample(UiCursorRole::ChromeNavigation, nowMs);
  cursor.selectionOverride = true;
  cursor.inkVisible = !cursors_.Active(UiCursorRole::ChromeNavigation, nowMs);
}

void UiApplicationRuntime::RenderFullScene() {
  UiFrameRenderer::RenderStatic(scene_, engine_.Surface(), engine_.Palette());
}

void UiApplicationRuntime::ActivatePage(RuntimePage page) {
  switch (page) {
  case RuntimePage::Song:
    std::construct_at(&frames_.song);
    break;
  case RuntimePage::Chain:
    std::construct_at(&frames_.chain);
    break;
  case RuntimePage::Phrase:
    std::construct_at(&frames_.phrase);
    break;
  case RuntimePage::Table:
    std::construct_at(&frames_.table);
    break;
  case RuntimePage::Instrument:
    std::construct_at(&frames_.instrument);
    break;
  case RuntimePage::Project:
    std::construct_at(&frames_.project);
    break;
  case RuntimePage::Device:
    std::construct_at(&frames_.device);
    break;
  case RuntimePage::Theme:
    std::construct_at(&frames_.theme);
    break;
  case RuntimePage::Font:
    std::construct_at(&frames_.font);
    break;
  case RuntimePage::Browser:
    std::construct_at(&frames_.browser);
    break;
  case RuntimePage::Groove:
    std::construct_at(&frames_.groove);
    break;
  case RuntimePage::Mixer:
    std::construct_at(&frames_.mixer);
    break;
  case RuntimePage::SampleEditor:
    std::construct_at(&frames_.sampleEditor);
    break;
  case RuntimePage::SampleSlices:
    std::construct_at(&frames_.sampleSlices);
    break;
  case RuntimePage::Record:
    std::construct_at(&frames_.record);
    break;
  case RuntimePage::None:
    break;
  }
  previousValid_ = false;
  dialogPreviousValid_ = false;
  cursorTargetValid_ = false;
  topMetaTargetValid_ = false;
  bottomTrackTargetValid_ = false;
  dialogCursorTargetValid_ = false;
  activePage_ = page;
}

void UiApplicationRuntime::CaptureDialog(IUiApplicationStateSource &source) {
  currentDialog_ = DialogFrameState{};
  if (!source.HasDialog()) {
    dialogCursorTargetValid_ = false;
    return;
  }
  currentDialog_.snapshot = source.DialogSnapshot();
  currentDialog_.instanceId = source.DialogInstanceId();
  currentDialog_.active = true;
  if (currentDialog_.snapshot.kind != UiDialogKind::Rename) {
    dialogCursorTargetValid_ = false;
    return;
  }

  const RectI16 cursorTarget =
      UiDialogView::CursorTargetRect(currentDialog_.snapshot.ToViewData());
  if (cursorTarget.Empty()) {
    dialogCursorTargetValid_ = false;
    currentDialog_.snapshot.cursorVisualRect = {};
    currentDialog_.snapshot.cursorVisualOverride = false;
    currentDialog_.snapshot.cursorInkVisible = true;
    return;
  }
  if (!dialogCursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Navigation, cursorTarget, frameNowMs_);
    dialogCursorTarget_ = cursorTarget;
    dialogCursorTargetValid_ = true;
  } else if (cursorTarget != dialogCursorTarget_) {
    cursors_.Retarget(UiCursorRole::Navigation, cursorTarget, frameNowMs_,
                      kDialogCursorDurationMs);
    dialogCursorTarget_ = cursorTarget;
  }
  currentDialog_.snapshot.cursorVisualRect =
      cursors_.Sample(UiCursorRole::Navigation, frameNowMs_);
  currentDialog_.snapshot.cursorVisualOverride = !cursorTarget.Empty();
  currentDialog_.snapshot.cursorInkVisible =
      !cursors_.Active(UiCursorRole::Navigation, frameNowMs_);
}

bool UiApplicationRuntime::DialogChanged() const {
  return !dialogPreviousValid_ || currentDialog_ != previousDialog_;
}

bool UiApplicationRuntime::RequiresFullRebuild() const {
  if (!previousValid_ || !dialogPreviousValid_)
    return true;
  if (currentDialog_.active != previousDialog_.active)
    return true;
  return currentDialog_.active &&
         (currentDialog_.instanceId != previousDialog_.instanceId ||
          currentDialog_.snapshot.kind != previousDialog_.snapshot.kind);
}

bool UiApplicationRuntime::FullScreenDialogActive() const {
  return currentDialog_.active &&
         (currentDialog_.snapshot.kind == UiDialogKind::FullScreen ||
          currentDialog_.snapshot.kind == UiDialogKind::Rename);
}

bool UiApplicationRuntime::CanCommitHiddenBaseWithoutRender() const {
  return previousValid_ && dialogPreviousValid_ && FullScreenDialogActive() &&
         currentDialog_ == previousDialog_;
}

UiBuildStatus UiApplicationRuntime::ApplyDialog() {
  if (!currentDialog_.active)
    return UiBuildStatus::Built;
  return UiDialogView::Apply(currentDialog_.snapshot.ToViewData(), scene_);
}

void UiApplicationRuntime::RenderDialogDelta() {
  if (!currentDialog_.active)
    return;
  if (!DialogChanged()) {
    // A non-fullscreen dialog can remain unchanged while playback or input
    // redraws the base page underneath it. Repaint its bounded damage region
    // after the base delta so the overlay cannot be partially erased.
    UiFrameRenderer::RenderRegion(
        scene_, engine_.Surface(), engine_.Palette(),
        UiDialogView::DamageRect(currentDialog_.snapshot.kind));
    return;
  }
  UiDialogView::RenderDelta(previousDialog_.snapshot.ToViewData(),
                            currentDialog_.snapshot.ToViewData(), scene_,
                            engine_.Surface(), engine_.Palette());
}

void UiApplicationRuntime::CommitDialog() {
  previousDialog_ = currentDialog_;
  dialogPreviousValid_ = true;
}

PresentResult UiApplicationRuntime::Present(IUiApplicationStateSource &source) {
  const RuntimePage page = source.ActivePage();
  if (page == RuntimePage::None)
    return PresentResult::Deferred;
  const UiTextCaseMode textCase = source.TextCase();
  if (scene_.textCase != textCase) {
    scene_.textCase = textCase;
    previousValid_ = false;
    dialogPreviousValid_ = false;
  }
  const std::uint32_t nowMs = source.NowMs();
  frameNowMs_ = nowMs;
  if (page != activePage_) {
    ActivatePage(page);
  }
  CaptureDialog(source);
  switch (page) {
  case RuntimePage::Song:
    return PresentSong(source, nowMs);
  case RuntimePage::Chain:
    return PresentChain(source, nowMs);
  case RuntimePage::Phrase:
    return PresentPhrase(source, nowMs);
  case RuntimePage::Table:
    return PresentTable(source, nowMs);
  case RuntimePage::Instrument:
    return PresentInstrument(source, nowMs);
  case RuntimePage::Project:
    return PresentProject(source, nowMs);
  case RuntimePage::Device:
    return PresentDevice(source, nowMs);
  case RuntimePage::Theme:
    return PresentTheme(source, nowMs);
  case RuntimePage::Font:
    return PresentFont(source, nowMs);
  case RuntimePage::Browser:
    return PresentBrowser(source, nowMs);
  case RuntimePage::Groove:
    return PresentGroove(source, nowMs);
  case RuntimePage::Mixer:
    return PresentMixer(source, nowMs);
  case RuntimePage::SampleEditor:
    return PresentSampleEditor(source, nowMs);
  case RuntimePage::SampleSlices:
    return PresentSampleSlices(source, nowMs);
  case RuntimePage::Record:
    return PresentRecord(source, nowMs);
  case RuntimePage::None:
    return PresentResult::Deferred;
  }
  return PresentResult::Deferred;
}

PresentResult
UiApplicationRuntime::PresentSong(IUiApplicationStateSource &source,
                                  std::uint32_t nowMs) {
  SongFrameState &current = frames_.song.current;
  SongFrameState &previous = frames_.song.previous;
  const UiApplicationActivityState activity = source.CaptureSong(current);
  current.power = CurrentPowerState(source, activity.active);
  UpdateNavigationCursor(current.navCursor, source, UiNavTarget::Song, nowMs);
  const RectI16 target =
      UiSongView::CursorTargetRect(current.editTrack, current.editRow);
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kSongCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = true;
  current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  bottomTrackTargetValid_ = false;
  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }

  const UiSongViewData data = ViewDataFor(current);
  if (UiSongView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiSongViewData previousData = ViewDataFor(previous);
      UiSongView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                              engine_.Palette());
    }
    RenderDialogDelta();
  }

  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiSongViewData UiApplicationRuntime::ViewDataFor(const SongFrameState &state) {
  UiSongViewData data;
  data.name = state.name.data();
  data.elapsed = state.elapsed.data();
  data.rows = state.rows;
  for (std::size_t track = 0; track < data.notes.size(); ++track) {
    data.notes[track] = state.notes[track].data();
  }
  data.playbackRows = state.playbackRows;
  data.queuedRows = state.queuedRows;
  data.mutedTracks = state.mutedTracks;
  data.vuLevelTop = state.vuLevelTop;
  data.rowOffset = state.rowOffset;
  data.editRow = state.editRow;
  data.editTrack = state.editTrack;
  data.cursorVisualRect = state.cursorVisualRect;
  data.selectionVisualRect = state.selectionVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.adjustmentFocus = state.adjustmentFocus;
  data.modeFocus = state.modeFocus;
  data.selectionActive = state.selectionActive;
  data.selectionNextExpansionAll = state.selectionNextExpansionAll;
  data.clipboardReady = state.clipboardReady;
  data.clipboardPasted = state.clipboardPasted;
  data.clipboardWidth = state.clipboardWidth;
  data.clipboardHeight = state.clipboardHeight;
  data.playing = state.playing;
  data.liveMode = state.liveMode;
  data.power = state.power;
  data.navCursor = state.navCursor;
  return data;
}

UiChainViewData
UiApplicationRuntime::ViewDataFor(const ChainFrameState &state) {
  UiChainViewData data;
  data.number = state.number.data();
  data.elapsed = state.elapsed.data();
  data.phrases = state.phrases;
  data.transposes = state.transposes;
  for (std::size_t track = 0; track < state.trackNotes.size(); ++track) {
    data.trackNotes[track] = state.trackNotes[track].data();
  }
  data.vuLevelTop = state.vuLevelTop;
  data.editRow = state.editRow;
  data.editColumn = state.editColumn;
  data.selectedTrack = state.selectedTrack;
  data.cursorVisualRect = state.cursorVisualRect;
  data.selectionVisualRect = state.selectionVisualRect;
  data.topMetaVisualRect = state.topMetaVisualRect;
  data.bottomTrackVisualRect = state.bottomTrackVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.topMetaVisualOverride = state.topMetaVisualOverride;
  data.bottomTrackVisualOverride = state.bottomTrackVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.topMetaInkVisible = state.topMetaInkVisible;
  data.bottomTrackInkVisible = state.bottomTrackInkVisible;
  data.numberFocus = state.numberFocus;
  data.adjustmentFocus = state.adjustmentFocus;
  data.selectionActive = state.selectionActive;
  data.selectionNextExpansionAll = state.selectionNextExpansionAll;
  data.clipboardReady = state.clipboardReady;
  data.clipboardPasted = state.clipboardPasted;
  data.clipboardWidth = state.clipboardWidth;
  data.clipboardHeight = state.clipboardHeight;
  data.playbackRows = state.playbackRows;
  data.mutedTracks = state.mutedTracks;
  data.power = state.power;
  data.navCursor = state.navCursor;
  return data;
}

PresentResult
UiApplicationRuntime::PresentChain(IUiApplicationStateSource &source,
                                   std::uint32_t nowMs) {
  ChainFrameState &current = frames_.chain.current;
  ChainFrameState &previous = frames_.chain.previous;
  const UiApplicationActivityState activity = source.CaptureChain(current);
  current.power = CurrentPowerState(source, activity.active);
  UpdateNavigationCursor(current.navCursor, source, UiNavTarget::Chain, nowMs);
  if (current.numberFocus) {
    const UiTopBarModel top{.title = "CHAIN", .meta = current.number.data()};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(current.selectedTrack);
    if (!topMetaTargetValid_) {
      cursors_.Snap(UiCursorRole::TopMeta, topTarget, nowMs);
      topMetaTarget_ = topTarget;
      topMetaTargetValid_ = true;
    } else if (topTarget != topMetaTarget_) {
      cursors_.Retarget(UiCursorRole::TopMeta, topTarget, nowMs,
                        kChainCursorDurationMs);
      topMetaTarget_ = topTarget;
    }
    if (!bottomTrackTargetValid_) {
      cursors_.Snap(UiCursorRole::BottomTrack, bottomTarget, nowMs);
      bottomTrackTarget_ = bottomTarget;
      bottomTrackTargetValid_ = true;
    } else if (bottomTarget != bottomTrackTarget_) {
      cursors_.Retarget(UiCursorRole::BottomTrack, bottomTarget, nowMs,
                        kChainCursorDurationMs);
      bottomTrackTarget_ = bottomTarget;
    }
    current.topMetaVisualRect = cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    current.topMetaVisualOverride = true;
    current.bottomTrackVisualOverride = true;
    current.topMetaInkVisible = !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    const UiChainViewData capture = ViewDataFor(current);
    const RectI16 target = UiChainView::CursorTargetRect(capture);
    if (!cursorTargetValid_) {
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
      cursorTarget_ = target;
      cursorTargetValid_ = true;
    } else if (target != cursorTarget_) {
      cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                        kChainCursorDurationMs);
      cursorTarget_ = target;
    }
    current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
    current.cursorVisualOverride = true;
    current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  }

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiChainViewData data = ViewDataFor(current);
  if (UiChainView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiChainViewData previousData = ViewDataFor(previous);
      UiChainView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                               engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiPhraseViewData
UiApplicationRuntime::ViewDataFor(const PhraseFrameState &state) {
  UiPhraseViewData data;
  data.number = state.number.data();
  data.elapsed = state.elapsed.data();
  for (std::size_t row = 0; row < state.rows.size(); ++row) {
    data.rows[row] = {
        state.rows[row].note.data(), state.rows[row].instrument.data(),
        state.rows[row].fx1.data(),  state.rows[row].parameter1.data(),
        state.rows[row].fx2.data(),  state.rows[row].parameter2.data()};
  }
  for (std::size_t track = 0; track < state.trackNotes.size(); ++track) {
    data.trackNotes[track] = state.trackNotes[track].data();
  }
  data.cursorBottom.kind = UiBottomBarKind::Hidden;
  if (state.context == UiPhraseContext::Instrument) {
    data.cursorBottom.kind = UiBottomBarKind::Context;
    data.cursorBottom.context.firstLineCount = 2;
    data.cursorBottom.context.firstLine[0] = {.text = state.contextLead.data(),
                                              .color =
                                                  UiColorToken::TextColored,
                                              .x = 9};
    data.cursorBottom.context.firstLine[1] = {.text = state.contextTail.data(),
                                              .color = UiColorToken::TextNormal,
                                              .x = 94,
                                              .userData = true};
  } else if (state.context == UiPhraseContext::Fx) {
    data.cursorBottom.kind = UiBottomBarKind::Context;
    data.cursorBottom.context.firstLineCount =
        state.contextTail[0] == '\0' ? 1 : 2;
    data.cursorBottom.context.firstLine[0] = {.text = state.contextLead.data(),
                                              .color =
                                                  UiColorToken::CursorPrimary,
                                              .x = 9};
    if (data.cursorBottom.context.firstLineCount == 2) {
      data.cursorBottom.context.firstLine[1] = {
          .text = state.contextTail.data(),
          .color = UiColorToken::TextNormal,
          .x = static_cast<std::int16_t>(
              9 + UiFont5x7::TextWidth(std::strlen(state.contextLead.data())) +
              7)};
    }
    if (state.contextDescription[0] != '\0') {
      data.cursorBottom.context.secondLineCount = 1;
      data.cursorBottom.context.secondLine[0] = {
          .text = state.contextDescription.data(),
          .color = UiColorToken::TextNormal,
          .x = 9};
    }
  }
  data.editRow = state.editRow;
  data.editColumn = state.editColumn;
  data.editDigit = state.editDigit;
  data.selectedTrack = state.selectedTrack;
  data.activeHeader = state.activeHeader;
  data.cursorVisualRect = state.cursorVisualRect;
  data.selectionVisualRect = state.selectionVisualRect;
  data.topMetaVisualRect = state.topMetaVisualRect;
  data.bottomTrackVisualRect = state.bottomTrackVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.topMetaVisualOverride = state.topMetaVisualOverride;
  data.bottomTrackVisualOverride = state.bottomTrackVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.topMetaInkVisible = state.topMetaInkVisible;
  data.bottomTrackInkVisible = state.bottomTrackInkVisible;
  data.enterDigitFocus = state.enterDigitFocus;
  data.numberFocus = state.numberFocus;
  data.adjustmentFocus = state.adjustmentFocus;
  data.selectionActive = state.selectionActive;
  data.selectionNextExpansionAll = state.selectionNextExpansionAll;
  data.clipboardReady = state.clipboardReady;
  data.clipboardPasted = state.clipboardPasted;
  data.clipboardWidth = state.clipboardWidth;
  data.clipboardHeight = state.clipboardHeight;
  data.playbackRows = state.playbackRows;
  data.mutedTracks = state.mutedTracks;
  data.power = state.power;
  data.navCursor = state.navCursor;
  return data;
}

PresentResult
UiApplicationRuntime::PresentPhrase(IUiApplicationStateSource &source,
                                    std::uint32_t nowMs) {
  PhraseFrameState &current = frames_.phrase.current;
  PhraseFrameState &previous = frames_.phrase.previous;
  const UiApplicationActivityState activity = source.CapturePhrase(current);
  current.power = CurrentPowerState(source, activity.active);
  UpdateNavigationCursor(current.navCursor, source, UiNavTarget::Phrase, nowMs);
  if (current.numberFocus) {
    const UiTopBarModel top{
        .title = "PHRASE", .meta = current.number.data(), .metaX = 85};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(current.selectedTrack);
    if (!topMetaTargetValid_) {
      cursors_.Snap(UiCursorRole::TopMeta, topTarget, nowMs);
      topMetaTarget_ = topTarget;
      topMetaTargetValid_ = true;
    } else if (topTarget != topMetaTarget_) {
      cursors_.Retarget(UiCursorRole::TopMeta, topTarget, nowMs,
                        kPhraseCursorDurationMs);
      topMetaTarget_ = topTarget;
    }
    if (!bottomTrackTargetValid_) {
      cursors_.Snap(UiCursorRole::BottomTrack, bottomTarget, nowMs);
      bottomTrackTarget_ = bottomTarget;
      bottomTrackTargetValid_ = true;
    } else if (bottomTarget != bottomTrackTarget_) {
      cursors_.Retarget(UiCursorRole::BottomTrack, bottomTarget, nowMs,
                        kPhraseCursorDurationMs);
      bottomTrackTarget_ = bottomTarget;
    }
    current.topMetaVisualRect = cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    current.topMetaVisualOverride = true;
    current.bottomTrackVisualOverride = true;
    current.topMetaInkVisible = !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    const UiPhraseViewData capture = ViewDataFor(current);
    const RectI16 target = UiPhraseView::CursorTargetRect(capture);
    if (!cursorTargetValid_) {
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
      cursorTarget_ = target;
      cursorTargetValid_ = true;
    } else if (target != cursorTarget_) {
      cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                        kPhraseCursorDurationMs);
      cursorTarget_ = target;
    }
    current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
    current.cursorVisualOverride = true;
    current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  }

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiPhraseViewData data = ViewDataFor(current);
  if (UiPhraseView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiPhraseViewData previousData = ViewDataFor(previous);
      UiPhraseView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                                engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiTableViewData
UiApplicationRuntime::ViewDataFor(const TableFrameState &state) {
  UiTableViewData data;
  data.number = state.number.data();
  data.elapsed = state.elapsed.data();
  for (std::size_t row = 0; row < state.rows.size(); ++row) {
    data.rows[row] = {
        state.rows[row].fx1.data(), state.rows[row].parameter1.data(),
        state.rows[row].fx2.data(), state.rows[row].parameter2.data(),
        state.rows[row].fx3.data(), state.rows[row].parameter3.data()};
  }
  for (std::size_t track = 0; track < state.trackNotes.size(); ++track) {
    data.trackNotes[track] = state.trackNotes[track].data();
  }
  data.cursorBottom.kind = UiBottomBarKind::Hidden;
  if (state.context == UiPhraseContext::Fx) {
    data.cursorBottom.kind = UiBottomBarKind::Context;
    data.cursorBottom.context.firstLineCount =
        state.contextTail[0] == '\0' ? 1 : 2;
    data.cursorBottom.context.firstLine[0] = {.text = state.contextLead.data(),
                                              .color =
                                                  UiColorToken::CursorPrimary,
                                              .x = 9};
    if (data.cursorBottom.context.firstLineCount == 2) {
      data.cursorBottom.context.firstLine[1] = {
          .text = state.contextTail.data(),
          .color = UiColorToken::TextNormal,
          .x = static_cast<std::int16_t>(
              9 + UiFont5x7::TextWidth(std::strlen(state.contextLead.data())) +
              7)};
    }
    if (state.contextDescription[0] != '\0') {
      data.cursorBottom.context.secondLineCount = 1;
      data.cursorBottom.context.secondLine[0] = {
          .text = state.contextDescription.data(),
          .color = UiColorToken::TextNormal,
          .x = 9};
    }
  }
  data.editRow = state.editRow;
  data.editColumn = state.editColumn;
  data.editDigit = state.editDigit;
  data.selectedTrack = state.selectedTrack;
  data.activeHeader = state.activeHeader;
  data.cursorVisualRect = state.cursorVisualRect;
  data.selectionVisualRect = state.selectionVisualRect;
  data.topMetaVisualRect = state.topMetaVisualRect;
  data.bottomTrackVisualRect = state.bottomTrackVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.topMetaVisualOverride = state.topMetaVisualOverride;
  data.bottomTrackVisualOverride = state.bottomTrackVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.topMetaInkVisible = state.topMetaInkVisible;
  data.bottomTrackInkVisible = state.bottomTrackInkVisible;
  data.enterDigitFocus = state.enterDigitFocus;
  data.numberFocus = state.numberFocus;
  data.adjustmentFocus = state.adjustmentFocus;
  data.selectionActive = state.selectionActive;
  data.selectionNextExpansionAll = state.selectionNextExpansionAll;
  data.clipboardReady = state.clipboardReady;
  data.clipboardPasted = state.clipboardPasted;
  data.clipboardWidth = state.clipboardWidth;
  data.clipboardHeight = state.clipboardHeight;
  data.playbackRows = state.playbackRows;
  data.automationPlaybackRows = state.automationPlaybackRows;
  data.selectedTrackMuted = state.selectedTrackMuted;
  data.power = state.power;
  data.navCursor = state.navCursor;
  return data;
}

PresentResult
UiApplicationRuntime::PresentTable(IUiApplicationStateSource &source,
                                   std::uint32_t nowMs) {
  TableFrameState &current = frames_.table.current;
  TableFrameState &previous = frames_.table.previous;
  const UiApplicationActivityState activity = source.CaptureTable(current);
  current.power = CurrentPowerState(source, activity.active);
  UpdateNavigationCursor(current.navCursor, source,
                         current.number[0] == 'I' ? UiNavTarget::InstrumentTable
                                                  : UiNavTarget::PhraseTable,
                         nowMs);
  if (current.numberFocus) {
    const UiTopBarModel top{.title = "TABLE", .meta = current.number.data()};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(current.selectedTrack);
    if (!topMetaTargetValid_) {
      cursors_.Snap(UiCursorRole::TopMeta, topTarget, nowMs);
      topMetaTarget_ = topTarget;
      topMetaTargetValid_ = true;
    } else if (topTarget != topMetaTarget_) {
      cursors_.Retarget(UiCursorRole::TopMeta, topTarget, nowMs,
                        kPhraseCursorDurationMs);
      topMetaTarget_ = topTarget;
    }
    if (!bottomTrackTargetValid_) {
      cursors_.Snap(UiCursorRole::BottomTrack, bottomTarget, nowMs);
      bottomTrackTarget_ = bottomTarget;
      bottomTrackTargetValid_ = true;
    } else if (bottomTarget != bottomTrackTarget_) {
      cursors_.Retarget(UiCursorRole::BottomTrack, bottomTarget, nowMs,
                        kPhraseCursorDurationMs);
      bottomTrackTarget_ = bottomTarget;
    }
    current.topMetaVisualRect = cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    current.topMetaVisualOverride = true;
    current.bottomTrackVisualOverride = true;
    current.topMetaInkVisible = !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    const UiTableViewData capture = ViewDataFor(current);
    const RectI16 target = UiTableView::CursorTargetRect(capture);
    if (!cursorTargetValid_) {
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
      cursorTarget_ = target;
      cursorTargetValid_ = true;
    } else if (target != cursorTarget_) {
      cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                        kPhraseCursorDurationMs);
      cursorTarget_ = target;
    }
    current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
    current.cursorVisualOverride = true;
    current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  }
  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiTableViewData data = ViewDataFor(current);
  if (UiTableView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiTableViewData previousData = ViewDataFor(previous);
      UiTableView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                               engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiInstrumentViewData
UiApplicationRuntime::ViewDataFor(const InstrumentFrameState &state) {
  UiInstrumentViewData data;
  data.number = state.number.data();
  data.elapsed = state.elapsed.data();
  data.name = state.name.data();
  data.kind = state.kind;
  data.fieldCount = state.fieldCount;
  for (std::size_t index = 0; index < state.fieldCount; ++index) {
    data.fields[index] = {state.fields[index].label.data(),
                          state.fields[index].value.data(),
                          state.fields[index].y, state.fields[index].userData};
  }
  data.operatorCount = state.operatorCount;
  for (std::size_t index = 0; index < state.operatorCount; ++index) {
    data.operators[index] = {state.operators[index].label.data(),
                             state.operators[index].op1.data(),
                             state.operators[index].op2.data()};
  }
  for (std::size_t track = 0; track < state.trackNotes.size(); ++track)
    data.trackNotes[track] = state.trackNotes[track].data();
  data.selectedField = state.selectedField;
  data.selectedOperator = state.selectedOperator;
  data.nameAction = state.nameAction;
  data.selectedTrack = state.selectedTrack;
  data.cursor = state.cursor;
  data.cursorVisualRect = state.cursorVisualRect;
  data.topMetaVisualRect = state.topMetaVisualRect;
  data.bottomTrackVisualRect = state.bottomTrackVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.topMetaVisualOverride = state.topMetaVisualOverride;
  data.bottomTrackVisualOverride = state.bottomTrackVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.topMetaInkVisible = state.topMetaInkVisible;
  data.bottomTrackInkVisible = state.bottomTrackInkVisible;
  data.numberFocus = state.numberFocus;
  data.enterSubfieldFocus = state.enterSubfieldFocus;
  data.adjustmentFocus = state.adjustmentFocus;
  data.adjustmentNote = state.adjustmentNote;
  data.fieldBottom = state.fieldBottom;
  data.fieldOptionCurrent = state.fieldOptionCurrent;
  data.fieldOptions = state.fieldOptions;
  data.fieldOptionWrap = state.fieldOptionWrap;
  data.adjustmentFineStep = state.adjustmentFineStep;
  data.adjustmentCoarseStep = state.adjustmentCoarseStep;
  data.selectedSubfield = state.selectedSubfield;
  data.subfieldTextOffset = state.subfieldTextOffset;
  data.scrollOffset = state.scrollOffset;
  data.power = state.power;
  data.navCursor = state.navCursor;
  return data;
}

PresentResult
UiApplicationRuntime::PresentInstrument(IUiApplicationStateSource &source,
                                        std::uint32_t nowMs) {
  InstrumentFrameState &current = frames_.instrument.current;
  InstrumentFrameState &previous = frames_.instrument.previous;
  const std::int16_t previousScroll =
      previousValid_ ? previous.scrollOffset : 0;
  const UiApplicationActivityState activity = source.CaptureInstrument(current);
  current.power = CurrentPowerState(source, activity.active);
  UpdateNavigationCursor(current.navCursor, source, UiNavTarget::Instrument,
                         nowMs);
  if (current.numberFocus) {
    current.scrollOffset = previousScroll;
    const UiTopBarModel top{.title = "INST", .meta = current.number.data()};
    const RectI16 topTarget = UiChromeRenderer::MetaTargetRect(top);
    const RectI16 bottomTarget =
        UiChromeRenderer::BottomTrackTargetRect(current.selectedTrack);
    if (!topMetaTargetValid_) {
      cursors_.Snap(UiCursorRole::TopMeta, topTarget, nowMs);
      topMetaTarget_ = topTarget;
      topMetaTargetValid_ = true;
    } else if (topTarget != topMetaTarget_) {
      cursors_.Retarget(UiCursorRole::TopMeta, topTarget, nowMs,
                        kPhraseCursorDurationMs);
      topMetaTarget_ = topTarget;
    }
    if (!bottomTrackTargetValid_) {
      cursors_.Snap(UiCursorRole::BottomTrack, bottomTarget, nowMs);
      bottomTrackTarget_ = bottomTarget;
      bottomTrackTargetValid_ = true;
    } else if (bottomTarget != bottomTrackTarget_) {
      cursors_.Retarget(UiCursorRole::BottomTrack, bottomTarget, nowMs,
                        kPhraseCursorDurationMs);
      bottomTrackTarget_ = bottomTarget;
    }
    current.topMetaVisualRect = cursors_.Sample(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackVisualRect =
        cursors_.Sample(UiCursorRole::BottomTrack, nowMs);
    current.topMetaVisualOverride = true;
    current.bottomTrackVisualOverride = true;
    current.topMetaInkVisible = !cursors_.Active(UiCursorRole::TopMeta, nowMs);
    current.bottomTrackInkVisible =
        !cursors_.Active(UiCursorRole::BottomTrack, nowMs);
  } else {
    topMetaTargetValid_ = false;
    bottomTrackTargetValid_ = false;
    UiInstrumentViewData capture = ViewDataFor(current);
    current.scrollOffset =
        UiInstrumentView::RevealCursor(previousScroll, capture);
    capture.scrollOffset = current.scrollOffset;
    const RectI16 target = UiInstrumentView::CursorTargetRect(capture);
    const bool scrollChanged =
        previousValid_ && current.scrollOffset != previousScroll;
    if (scrollChanged && cursorTargetValid_) {
      RectI16 rebased = cursors_.Sample(UiCursorRole::Content, nowMs);
      rebased.y = static_cast<std::int16_t>(
          rebased.y + current.scrollOffset - previousScroll);
      cursors_.Snap(UiCursorRole::Content, rebased, nowMs);
    }
    if (!cursorTargetValid_) {
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
      cursorTarget_ = target;
      cursorTargetValid_ = true;
    } else if (target != cursorTarget_ || scrollChanged) {
      cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                        kPhraseCursorDurationMs);
      cursorTarget_ = target;
    }
    current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
    current.cursorVisualOverride = !target.Empty();
    current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);
  }

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiInstrumentViewData data = ViewDataFor(current);
  if (UiInstrumentView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiInstrumentViewData previousData = ViewDataFor(previous);
      UiInstrumentView::RenderDelta(previousData, data, scene_,
                                    engine_.Surface(), engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiProjectViewData
UiApplicationRuntime::ViewDataFor(const ProjectFrameState &state) {
  UiProjectViewData data;
  data.name = state.name.data();
  data.tempo = state.tempo.data();
  data.transpose = state.transpose.data();
  data.scale = state.scale.data();
  data.root = state.root.data();
  for (std::size_t index = 0; index < state.selectorOptions.size(); ++index)
    data.selectorOptions[index] = state.selectorOptions[index].data();
  data.selectorCount = state.selectorCount;
  data.selectorCurrent = state.selectorCurrent;
  data.selectorWrap = state.selectorWrap;
  data.enterHeld = state.enterHeld;
  data.cursor = state.cursor;
  data.nameAction = state.nameAction;
  data.sampleAction = state.sampleAction;
  data.renderOption = state.renderOption;
  data.cursorVisualRect = state.cursorVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.scrollOffset = state.scrollOffset;
  data.power = state.power;
  data.navCursor = state.navCursor;
  return data;
}

PresentResult
UiApplicationRuntime::PresentProject(IUiApplicationStateSource &source,
                                     std::uint32_t nowMs) {
  ProjectFrameState &current = frames_.project.current;
  ProjectFrameState &previous = frames_.project.previous;
  const std::int16_t previousScroll =
      previousValid_ ? previous.scrollOffset : 0;
  const UiApplicationActivityState activity = source.CaptureProject(current);
  current.power = CurrentPowerState(source, activity.active);
  UpdateNavigationCursor(current.navCursor, source, UiNavTarget::Project,
                         nowMs);
  current.scrollOffset =
      UiProjectView::RevealCursor(previousScroll, current.cursor);
  const RectI16 target = UiProjectView::CursorTargetRect(current.cursor);
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = !target.Empty();
  current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiProjectViewData data = ViewDataFor(current);
  if (UiProjectView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      UiProjectView::RenderDelta(ViewDataFor(previous), data, scene_,
                                 engine_.Surface(), engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiDeviceViewData
UiApplicationRuntime::ViewDataFor(const DeviceFrameState &state) {
  UiDeviceViewData data;
  data.midiDevice = state.midiDevice.data();
  data.midiSync = state.midiSync.data();
  data.lineOut = state.lineOut.data();
  data.resampler = state.resampler.data();
  data.volume = state.volume.data();
  data.brightness = state.brightness.data();
  data.theme = state.theme.data();
  data.font = state.font.data();
  data.version = state.version.data();
  for (std::size_t index = 0; index < state.selectorOptions.size(); ++index)
    data.selectorOptions[index] = state.selectorOptions[index].data();
  data.selectorCount = state.selectorCount;
  data.selectorCurrent = state.selectorCurrent;
  data.selectorWrap = state.selectorWrap;
  data.enterHeld = state.enterHeld;
  data.showLineOut = state.showLineOut;
  data.showVolume = state.showVolume;
  data.showTheme = state.showTheme;
  data.showFont = state.showFont;
  data.showUpdateFirmware = state.showUpdateFirmware;
  data.batteryPercentValid = state.batteryPercentValid;
  data.batteryPercent = state.batteryPercent;
  data.cursor = state.cursor;
  data.cursorVisualRect = state.cursorVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.scrollOffset = state.scrollOffset;
  data.power = state.power;
  return data;
}

PresentResult
UiApplicationRuntime::PresentDevice(IUiApplicationStateSource &source,
                                    std::uint32_t nowMs) {
  DeviceFrameState &current = frames_.device.current;
  DeviceFrameState &previous = frames_.device.previous;
  const std::int16_t previousScroll =
      previousValid_ ? previous.scrollOffset : 0;
  const UiApplicationActivityState activity = source.CaptureDevice(current);
  const PowerFrameState power = CapturePowerState(source, activity.active);
  current.power = power.power;
  current.batteryPercent = power.batteryPercent;
  current.batteryPercentValid = power.batteryPercentValid;
  UiDeviceViewData capture = ViewDataFor(current);
  current.scrollOffset = UiDeviceView::RevealCursor(previousScroll, capture);
  capture.scrollOffset = current.scrollOffset;
  const RectI16 target = UiDeviceView::CursorTargetRect(capture);
  const bool scrollChanged =
      previousValid_ && current.scrollOffset != previousScroll;
  if (scrollChanged && cursorTargetValid_) {
    RectI16 rebased = cursors_.Sample(UiCursorRole::Content, nowMs);
    rebased.y = static_cast<std::int16_t>(rebased.y + current.scrollOffset -
                                          previousScroll);
    cursors_.Snap(UiCursorRole::Content, rebased, nowMs);
  }
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_ || scrollChanged) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = !target.Empty();
  current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiDeviceViewData data = ViewDataFor(current);
  if (UiDeviceView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiDeviceViewData previousData = ViewDataFor(previous);
      UiDeviceView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                                engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiThemeViewData
UiApplicationRuntime::ViewDataFor(const ThemeFrameState &state) {
  return state.view.ToViewData();
}

PresentResult
UiApplicationRuntime::PresentTheme(IUiApplicationStateSource &source,
                                   std::uint32_t nowMs) {
  ThemeFrameState &current = frames_.theme.current;
  ThemeFrameState &previous = frames_.theme.previous;
  const std::int16_t previousScroll =
      previousValid_ ? previous.view.scrollOffset : 0;
  const UiApplicationActivityState activity = source.CaptureTheme(current);
  if (current.colorsValid &&
      (!previousValid_ || !previous.colorsValid ||
       current.colors != previous.colors)) {
    ApplyThemeColors(current.colors);
  }
  current.view.power = CurrentPowerState(source, activity.active);
  UiThemeViewData capture = ViewDataFor(current);
  current.view.scrollOffset =
      UiThemeView::RevealCursor(previousScroll, capture);
  capture.scrollOffset = current.view.scrollOffset;
  const RectI16 target = UiThemeView::CursorTargetRect(capture);
  const bool scrollChanged =
      previousValid_ && current.view.scrollOffset != previousScroll;
  if (scrollChanged && cursorTargetValid_) {
    RectI16 rebased = cursors_.Sample(UiCursorRole::Content, nowMs);
    rebased.y = static_cast<std::int16_t>(
        rebased.y + current.view.scrollOffset - previousScroll);
    cursors_.Snap(UiCursorRole::Content, rebased, nowMs);
  }
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_ || scrollChanged) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.view.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.view.cursorVisualOverride = !target.Empty();
  current.view.cursorInkVisible =
      current.view.cursorInkVisible &&
      !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiThemeViewData data = ViewDataFor(current);
  if (UiThemeView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      UiThemeView::RenderDelta(ViewDataFor(previous), data, scene_,
                               engine_.Surface(), engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiFontViewData UiApplicationRuntime::ViewDataFor(const FontFrameState &state) {
  return state.ToViewData();
}

PresentResult
UiApplicationRuntime::PresentFont(IUiApplicationStateSource &source,
                                  std::uint32_t nowMs) {
  FontFrameState &current = frames_.font.current;
  FontFrameState &previous = frames_.font.previous;
  const UiApplicationActivityState activity = source.CaptureFont(current);
  current.power = CurrentPowerState(source, activity.active);
  const RectI16 target = UiFontView::CursorTargetRect(current.cursor);
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = true;
  current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiFontViewData data = ViewDataFor(current);
  if (UiFontView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built)
    return PresentResult::Failed;
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive())
      UiFontView::RenderDelta(ViewDataFor(previous), data, scene_,
                              engine_.Surface(), engine_.Palette());
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiBrowserViewData
UiApplicationRuntime::ViewDataFor(const BrowserFrameState &state) {
  UiBrowserViewData data = state.snapshot.ViewData(state.power);
  data.cursorVisualRect = state.cursorVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  return data;
}

PresentResult
UiApplicationRuntime::PresentBrowser(IUiApplicationStateSource &source,
                                     std::uint32_t nowMs) {
  BrowserFrameState &current = frames_.browser.current;
  BrowserFrameState &previous = frames_.browser.previous;
  const UiApplicationActivityState activity = source.CaptureBrowser(current);
  current.power = CurrentPowerState(source, activity.active);
  const RectI16 target =
      current.snapshot.hasSelection
          ? UiBrowserView::CursorTargetRect(current.snapshot.selectedRow)
          : RectI16{};
  if (target.Empty()) {
    // An empty browser has no rendered cursor. End any in-flight transition
    // immediately so hidden geometry cannot keep the frame state changing
    // after RenderDelta correctly produces no pixel damage.
    if (!cursorTargetValid_ || target != cursorTarget_)
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = !target.Empty();
  current.cursorInkVisible = current.snapshot.hasSelection &&
                             !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiBrowserViewData data = ViewDataFor(current);
  if (UiBrowserView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiBrowserViewData previousData = ViewDataFor(previous);
      UiBrowserView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                                 engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiGrooveViewData
UiApplicationRuntime::ViewDataFor(const GrooveFrameState &state) {
  UiGrooveViewData data;
  data.number = state.number.data();
  data.steps = state.steps;
  data.editRow = state.editRow;
  data.playbackRow = state.playbackRow;
  data.selectedTrackMuted = state.selectedTrackMuted;
  data.cursorVisualRect = state.cursorVisualRect;
  data.selectionVisualRect = state.selectionVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  data.selectionActive = state.selectionActive;
  data.selectionNextExpansionAll = state.selectionNextExpansionAll;
  data.clipboardReady = state.clipboardReady;
  data.clipboardPasted = state.clipboardPasted;
  data.interpolationCompleted = state.interpolationCompleted;
  data.clipboardWidth = state.clipboardWidth;
  data.clipboardHeight = state.clipboardHeight;
  data.power = state.power;
  data.navCursor = state.navCursor;
  return data;
}

PresentResult
UiApplicationRuntime::PresentGroove(IUiApplicationStateSource &source,
                                    std::uint32_t nowMs) {
  GrooveFrameState &current = frames_.groove.current;
  GrooveFrameState &previous = frames_.groove.previous;
  const UiApplicationActivityState activity = source.CaptureGroove(current);
  current.power = CurrentPowerState(source, activity.active);
  UpdateNavigationCursor(current.navCursor, source, UiNavTarget::Groove, nowMs);
  const RectI16 target = UiGrooveView::CursorTargetRect(current.editRow);
  if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kGrooveCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = true;
  current.cursorInkVisible = !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiGrooveViewData data = ViewDataFor(current);
  if (UiGrooveView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiGrooveViewData previousData = ViewDataFor(previous);
      UiGrooveView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                                engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiMixerViewData
UiApplicationRuntime::ViewDataFor(const MixerFrameState &state) {
  UiMixerViewData data;
  data.vuLevelTop = state.vuLevelTop;
  for (std::size_t channel = 0; channel < data.volumes.size(); ++channel) {
    data.volumes[channel] = state.volumes[channel].data();
  }
  data.selectedChannel = state.selectedChannel;
  data.power = state.power;
  data.navCursor = state.navCursor;
  return data;
}

PresentResult
UiApplicationRuntime::PresentMixer(IUiApplicationStateSource &source,
                                   std::uint32_t nowMs) {
  MixerFrameState &current = frames_.mixer.current;
  MixerFrameState &previous = frames_.mixer.previous;
  const UiApplicationActivityState activity = source.CaptureMixer(current);
  current.power = CurrentPowerState(source, activity.active);
  UpdateNavigationCursor(current.navCursor, source, UiNavTarget::Mixer, nowMs);
  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged()) {
    return engine_.PresentDirty();
  }
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiMixerViewData data = ViewDataFor(current);
  if (UiMixerView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      const UiMixerViewData previousData = ViewDataFor(previous);
      UiMixerView::RenderDelta(previousData, data, scene_, engine_.Surface(),
                               engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiSampleEditorViewData
UiApplicationRuntime::ViewDataFor(const SampleEditorFrameState &state) {
  return state.ToViewData();
}

PresentResult
UiApplicationRuntime::PresentSampleEditor(IUiApplicationStateSource &source,
                                          std::uint32_t nowMs) {
  SampleEditorFrameState &current = frames_.sampleEditor.current;
  SampleEditorFrameState &previous = frames_.sampleEditor.previous;
  const UiApplicationActivityState activity =
      source.CaptureSampleEditor(current);
  current.power = CurrentPowerState(source, activity.active);
  const RectI16 target =
      UiSampleEditorView::CursorTargetRect(ViewDataFor(current));
  if (target.Empty()) {
    if (!cursorTargetValid_ || target != cursorTarget_)
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = !target.Empty();
  current.cursorInkVisible = current.cursorInkVisible && !target.Empty() &&
                             !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || !(current == previous);
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiSampleEditorViewData data = ViewDataFor(current);
  if (UiSampleEditorView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      UiSampleEditorView::RenderDelta(ViewDataFor(previous), data, scene_,
                                      engine_.Surface(), engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiSampleSlicesViewData
UiApplicationRuntime::ViewDataFor(const SampleSlicesFrameState &state) {
  return state.ToViewData();
}

PresentResult
UiApplicationRuntime::PresentSampleSlices(IUiApplicationStateSource &source,
                                          std::uint32_t nowMs) {
  SampleSlicesFrameState &current = frames_.sampleSlices.current;
  SampleSlicesFrameState &previous = frames_.sampleSlices.previous;
  const UiApplicationActivityState activity =
      source.CaptureSampleSlices(current);
  current.power = CurrentPowerState(source, activity.active);
  const RectI16 target =
      UiSampleSlicesView::CursorTargetRect(ViewDataFor(current));
  if (target.Empty()) {
    if (!cursorTargetValid_ || target != cursorTarget_)
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = !target.Empty();
  current.cursorInkVisible = current.cursorInkVisible && !target.Empty() &&
                             !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || !(current == previous);
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiSampleSlicesViewData data = ViewDataFor(current);
  if (UiSampleSlicesView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      UiSampleSlicesView::RenderDelta(ViewDataFor(previous), data, scene_,
                                      engine_.Surface(), engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

UiRecordViewData
UiApplicationRuntime::ViewDataFor(const RecordFrameState &state) {
  UiRecordViewData data = state.snapshot.ViewData(state.power);
  data.cursorVisualRect = state.cursorVisualRect;
  data.cursorVisualOverride = state.cursorVisualOverride;
  data.cursorInkVisible = state.cursorInkVisible;
  return data;
}

PresentResult
UiApplicationRuntime::PresentRecord(IUiApplicationStateSource &source,
                                    std::uint32_t nowMs) {
  RecordFrameState &current = frames_.record.current;
  RecordFrameState &previous = frames_.record.previous;
  const UiApplicationActivityState activity = source.CaptureRecord(current);
  current.power = CurrentPowerState(source, activity.active);
  const UiRecordViewData capture = ViewDataFor(current);
  const RectI16 target = UiRecordView::CursorTargetRect(capture.focus);
  if (target.Empty()) {
    if (!cursorTargetValid_ || target != cursorTarget_)
      cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (!cursorTargetValid_) {
    cursors_.Snap(UiCursorRole::Content, target, nowMs);
    cursorTarget_ = target;
    cursorTargetValid_ = true;
  } else if (target != cursorTarget_) {
    cursors_.Retarget(UiCursorRole::Content, target, nowMs,
                      kListCursorDurationMs);
    cursorTarget_ = target;
  }
  current.cursorVisualRect = cursors_.Sample(UiCursorRole::Content, nowMs);
  current.cursorVisualOverride = !target.Empty();
  current.cursorInkVisible = capture.cursorInkVisible && !target.Empty() &&
                             !cursors_.Active(UiCursorRole::Content, nowMs);

  const bool baseChanged = !previousValid_ || current != previous;
  if (!baseChanged && !DialogChanged())
    return engine_.PresentDirty();
  if (CanCommitHiddenBaseWithoutRender()) {
    previous = current;
    return engine_.PresentDirty();
  }
  const UiRecordViewData data = ViewDataFor(current);
  if (UiRecordView::Build(data, engine_.Palette(), scene_) !=
          UiBuildStatus::Built ||
      ApplyDialog() != UiBuildStatus::Built) {
    return PresentResult::Failed;
  }
  if (RequiresFullRebuild()) {
    RenderFullScene();
  } else {
    if (baseChanged && !FullScreenDialogActive()) {
      UiRecordView::RenderDelta(ViewDataFor(previous), data, scene_,
                                engine_.Surface(), engine_.Palette());
    }
    RenderDialogDelta();
  }
  const PresentResult result = engine_.PresentDirty();
  if (result == PresentResult::Presented) {
    previous = current;
    previousValid_ = true;
    CommitDialog();
  }
  return result;
}

} // namespace ui2
