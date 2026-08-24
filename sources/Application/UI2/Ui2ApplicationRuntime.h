/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Animation/UiAnimatedRect.h"
#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/UiEngine.h"
#include "UI2/Views/Phrase/UiPhraseView.h"
#include "UI2/Views/Song/UiSongView.h"
#include "UI2/Views/Table/UiTableView.h"

#include <array>
#include <cstdint>

class AppWindow;

namespace ui2 {

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
  enum class RuntimePage : std::uint8_t { None, Song, Phrase, Table };

  struct SongFrameState {
    std::array<char, 17> name{};
    std::array<char, 6> elapsed{};
    std::array<std::array<std::uint8_t, 8>, 16> rows{};
    std::array<std::array<char, 5>, 8> notes{};
    std::array<std::int8_t, 8> playbackRows{-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<std::uint8_t, 2> vuLevelTop{153, 153};
    std::uint8_t rowOffset = 0;
    std::uint8_t editRow = 0;
    std::uint8_t editTrack = 0;
    RectI16 cursorVisualRect{};
    bool cursorVisualOverride = false;
    bool cursorInkVisible = true;
    bool playing = false;
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
    std::int8_t selectedTrack = 0;
    UiPhraseHeader activeHeader = UiPhraseHeader::None;
    PhraseContext context = PhraseContext::Hidden;
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
    std::int8_t selectedTrack = 0;
    UiTableHeader activeHeader = UiTableHeader::None;
    PhraseContext context = PhraseContext::Hidden;
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
    UiPowerState power = UiPowerState::BatteryNormal;

    bool operator==(const TableFrameState &) const = default;
  };

  static UiSongViewData ViewDataFor(const SongFrameState &state);
  static UiPhraseViewData ViewDataFor(const PhraseFrameState &state);
  static UiTableViewData ViewDataFor(const TableFrameState &state);
  void CaptureSong(AppWindow &window, SongFrameState &state);
  void CapturePhrase(AppWindow &window, PhraseFrameState &state);
  void CaptureTable(AppWindow &window, TableFrameState &state);
  [[nodiscard]] PresentResult PresentSong(AppWindow &window,
                                          std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentPhrase(AppWindow &window,
                                            std::uint32_t nowMs);
  [[nodiscard]] PresentResult PresentTable(AppWindow &window,
                                           std::uint32_t nowMs);

  UiEngineStorage engineStorage_{};
  UiEngine engine_;
  UiFrameScene scene_{};
  SongFrameState previousSong_{};
  SongFrameState currentSong_{};
  PhraseFrameState previousPhrase_{};
  PhraseFrameState currentPhrase_{};
  TableFrameState previousTable_{};
  TableFrameState currentTable_{};
  UiCursorAnimatorSet cursors_{};
  RectI16 cursorTarget_{};
  RectI16 topMetaTarget_{};
  RectI16 bottomTrackTarget_{};
  bool cursorTargetValid_ = false;
  bool topMetaTargetValid_ = false;
  bool bottomTrackTargetValid_ = false;
  bool previousValid_ = false;
  RuntimePage activePage_ = RuntimePage::None;
};

} // namespace ui2
