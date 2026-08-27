/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Session/TrackerApplicationSession.h"
#include "Application/UI2/Controllers/Ui2TrackerControllerHub.h"

#include <array>
#include <cstdint>

namespace ui2 {

// Model mutation boundary for the native UI2 grid controllers. It stores only
// controller navigation and last-value registers; song data remains owned by
// Project/TrackerSessionState and audio transport remains owned by Player.
class Ui2TrackerSessionModelPort final : public IUi2TrackerModelPort {
public:
  explicit Ui2TrackerSessionModelPort(TrackerApplicationSession &session);

  [[nodiscard]] Ui2TrackerGridSessionState LoadGridSession() const override;
  void StoreGridNavigation(const Ui2TrackerGridNavigationState &state) override;
  void ApplyGridCommand(const Ui2TrackerCommand &command) override;
  [[nodiscard]] bool PreparePageNavigation(Ui2TrackerPage source,
                                           Ui2TrackerPage target,
                                           std::uint8_t track,
                                           std::uint8_t row);

  [[nodiscard]] Ui2TrackerPage ActivePage() const { return activePage_; }
  [[nodiscard]] std::uint32_t ProjectMutationGeneration() const {
    return projectMutationGeneration_;
  }
  // Non-grid workflows use the same monotonic mutation source as grid edits,
  // so autosave never depends on page-specific dirty bookkeeping.
  void MarkProjectMutated() { ++projectMutationGeneration_; }
  void ResetProjectBoundary();

private:
  void ApplyAdjustCell(const Ui2TrackerCommand &command);
  void ApplyAdjustSelection(const Ui2TrackerCommand &command);
  void ApplySwitchPage(const Ui2TrackerCommand &command);
  void ApplyCutCell(const Ui2TrackerCommand &command);
  void ApplyPasteLast(const Ui2TrackerCommand &command);
  [[nodiscard]] bool ApplyAllocateNext(const Ui2TrackerCommand &command);
  [[nodiscard]] bool ApplyCloneCell(const Ui2TrackerCommand &command);
  [[nodiscard]] bool ApplyCopySelection(const Ui2TrackerCommand &command,
                                        bool cut);
  [[nodiscard]] bool ApplyPasteSelection(const Ui2TrackerCommand &command);
  [[nodiscard]] std::uint32_t ReadCell(Ui2TrackerPage page, std::uint8_t row,
                                       std::uint8_t column) const;
  void WriteCell(Ui2TrackerPage page, std::uint8_t row, std::uint8_t column,
                 std::uint32_t value);
  void ClearCell(Ui2TrackerPage page, std::uint8_t row, std::uint8_t column);
  void ApplyTransport(const Ui2TrackerCommand &command);
  [[nodiscard]] bool ResolveTableTrack(Ui2TrackerPage page,
                                       std::uint8_t track);
  [[nodiscard]] bool WarpChainSongPosition(std::uint8_t track,
                                           std::int16_t delta);
  [[nodiscard]] bool ResolveTargetPage(Ui2TrackerPage page, std::uint8_t track,
                                       std::uint8_t row);

  TrackerApplicationSession &session_;
  Ui2TrackerPage activePage_ = Ui2TrackerPage::Song;
  std::uint8_t phraseRow_ = 0;
  std::uint8_t phraseColumn_ = 0;
  std::uint8_t phraseDigit_ = 3;
  std::uint8_t phraseTableNumber_ = 0;
  std::uint8_t phraseTableRow_ = 0;
  std::uint8_t phraseTableColumn_ = 0;
  std::uint8_t phraseTableDigit_ = 3;
  std::uint8_t instrumentTableNumber_ = 0;
  std::uint8_t instrumentTableRow_ = 0;
  std::uint8_t instrumentTableColumn_ = 0;
  std::uint8_t instrumentTableDigit_ = 3;
  std::uint8_t lastChain_ = 0;
  std::uint8_t lastPhrase_ = 0;
  std::uint8_t lastNote_ = 60;
  std::uint8_t lastInstrument_ = 0;
  FourCC lastCommand_ = FourCC::InstrumentCommandNone;
  std::uint16_t lastParameter_ = 0;
  std::array<std::uint32_t, 8U * 16U> selectionClipboard_{};
  Ui2TrackerPage selectionClipboardPage_ = Ui2TrackerPage::None;
  std::uint8_t selectionClipboardStartColumn_ = 0;
  std::uint8_t selectionClipboardWidth_ = 0;
  std::uint8_t selectionClipboardHeight_ = 0;
  std::array<bool, SONG_CHANNEL_COUNT> soloMuteMask_{};
  bool soloActive_ = false;
  bool auditionOwned_ = false;
  std::uint32_t projectMutationGeneration_ = 0U;
};

} // namespace ui2
