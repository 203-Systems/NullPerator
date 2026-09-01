/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2ChainController.h"
#include "Application/UI2/Controllers/Ui2PhraseController.h"
#include "Application/UI2/Controllers/Ui2SongController.h"
#include "Application/UI2/Controllers/Ui2TableController.h"
#include "Application/UI2/Ui2ApplicationStateSource.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace ui2 {

struct Ui2TrackerGridState {
  Ui2TrackerPage activePage = Ui2TrackerPage::Song;
  std::uint8_t track = 0;
  std::uint8_t songVisibleRow = 0;
  std::uint8_t songRowOffset = 0;
  std::uint8_t chainNumber = 0;
  std::uint8_t chainRow = 0;
  std::uint8_t chainColumn = 0;
  std::uint8_t phraseNumber = 0;
  std::uint8_t phraseRow = 0;
  std::uint8_t phraseColumn = 0;
  std::uint8_t phraseDigit = 3;
  std::uint8_t phraseTableNumber = 0;
  std::uint8_t phraseTableRow = 0;
  std::uint8_t phraseTableColumn = 0;
  std::uint8_t phraseTableDigit = 3;
  std::uint8_t instrumentTableNumber = 0;
  std::uint8_t instrumentTableRow = 0;
  std::uint8_t instrumentTableColumn = 0;
  std::uint8_t instrumentTableDigit = 3;
  bool liveMode = false;
};

struct Ui2TrackerActiveControllerState {
  Ui2TrackerPage page = Ui2TrackerPage::None;
  std::uint8_t row = 0;
  std::uint8_t column = 0;
  std::uint8_t number = 0;
  std::uint8_t track = 0;
  std::uint8_t digit = 3;
  std::uint8_t rowOffset = 0;
  std::uint16_t heldMask = 0;
  Ui2GridSelectionState selection{};
  bool liveMode = false;
  bool numberFocus = false;
  bool trackFocus = false;
  bool enterDigitFocus = false;
};

class IUi2TrackerModelPort {
public:
  virtual ~IUi2TrackerModelPort() = default;
  [[nodiscard]] virtual Ui2TrackerGridState LoadGridState() const = 0;
  virtual void StoreGridState(const Ui2TrackerGridState &state) = 0;
  virtual void ApplyGridCommand(const Ui2TrackerCommand &command) = 0;
};

class Ui2TrackerControllerHub {
public:
  Ui2TrackerControllerHub() = default;
  explicit Ui2TrackerControllerHub(const Ui2TrackerGridState &state) {
    Synchronize(state);
  }

  [[nodiscard]] Ui2TrackerPage ActivePage() const { return activePage_; }

  [[nodiscard]] UiApplicationPage ActiveApplicationPage() const {
    switch (activePage_) {
    case Ui2TrackerPage::Song:
      return UiApplicationPage::Song;
    case Ui2TrackerPage::Chain:
      return UiApplicationPage::Chain;
    case Ui2TrackerPage::Phrase:
      return UiApplicationPage::Phrase;
    case Ui2TrackerPage::PhraseTable:
    case Ui2TrackerPage::InstrumentTable:
      return UiApplicationPage::Table;
    case Ui2TrackerPage::Project:
      return UiApplicationPage::Project;
    case Ui2TrackerPage::Mixer:
      return UiApplicationPage::Mixer;
    case Ui2TrackerPage::Groove:
      return UiApplicationPage::Groove;
    case Ui2TrackerPage::Instrument:
      return UiApplicationPage::Instrument;
    case Ui2TrackerPage::Record:
      return UiApplicationPage::Record;
    case Ui2TrackerPage::None:
      return UiApplicationPage::None;
    }
    return UiApplicationPage::None;
  }

  [[nodiscard]] const Ui2SongController &Song() const { return song_; }
  [[nodiscard]] const Ui2ChainController &Chain() const { return chain_; }
  [[nodiscard]] const Ui2PhraseController &Phrase() const { return phrase_; }
  [[nodiscard]] const Ui2TableController &Table() const {
    return activePage_ == Ui2TrackerPage::InstrumentTable ? instrumentTable_
                                                          : phraseTable_;
  }
  [[nodiscard]] const Ui2TableController &PhraseTable() const {
    return phraseTable_;
  }
  [[nodiscard]] const Ui2TableController &InstrumentTable() const {
    return instrumentTable_;
  }

  void SetNavigationHeld(bool held) {
    navigationHeld_ = held;
    song_.SetNavigationHeld(held);
    chain_.SetNavigationHeld(held);
    phrase_.SetNavigationHeld(held);
    phraseTable_.SetNavigationHeld(held);
    instrumentTable_.SetNavigationHeld(held);
  }

  [[nodiscard]] Ui2TrackerActiveControllerState ActiveState() const {
    Ui2TrackerActiveControllerState state{.page = activePage_,
                                          .track = song_.Track()};
    switch (activePage_) {
    case Ui2TrackerPage::Song:
      state.row = song_.VisibleRow();
      state.column = song_.Track();
      state.track = song_.Track();
      state.rowOffset = song_.RowOffset();
      state.heldMask = song_.HeldMask();
      state.selection = song_.Selection();
      state.liveMode = song_.LiveMode();
      break;
    case Ui2TrackerPage::Chain:
      CaptureNumberedGridState(state, chain_);
      break;
    case Ui2TrackerPage::Phrase:
      CaptureNumberedGridState(state, phrase_);
      CaptureParameterState(state, phrase_);
      break;
    case Ui2TrackerPage::PhraseTable:
    case Ui2TrackerPage::InstrumentTable: {
      const Ui2TableController &table = Table();
      CaptureNumberedGridState(state, table);
      CaptureParameterState(state, table);
      break;
    }
    case Ui2TrackerPage::Project:
    case Ui2TrackerPage::Mixer:
    case Ui2TrackerPage::Groove:
    case Ui2TrackerPage::Instrument:
    case Ui2TrackerPage::Record:
    case Ui2TrackerPage::None:
      break;
    }
    return state;
  }

  [[nodiscard]] Ui2TrackerCommandBatch<> Handle(TrackerAction action,
                                                  bool pressed) {
    const std::uint8_t actionIndex = static_cast<std::uint8_t>(action);
    if (!TrackerActionIsValid(action))
      return {};

    Ui2TrackerPage owner = activePage_;
    if (pressed) {
      if (pressOwners_[actionIndex] == Ui2TrackerPage::None)
        pressOwners_[actionIndex] = activePage_;
      owner = pressOwners_[actionIndex];
    } else {
      owner = pressOwners_[actionIndex] == Ui2TrackerPage::None
                  ? activePage_
                  : pressOwners_[actionIndex];
      pressOwners_[actionIndex] = Ui2TrackerPage::None;
    }

    switch (owner) {
    case Ui2TrackerPage::Song:
      return song_.Handle(action, pressed);
    case Ui2TrackerPage::Chain:
      return chain_.Handle(action, pressed);
    case Ui2TrackerPage::Phrase:
      return phrase_.Handle(action, pressed);
    case Ui2TrackerPage::PhraseTable:
      return phraseTable_.Handle(action, pressed);
    case Ui2TrackerPage::InstrumentTable:
      return instrumentTable_.Handle(action, pressed);
    case Ui2TrackerPage::Project:
    case Ui2TrackerPage::Mixer:
    case Ui2TrackerPage::Groove:
    case Ui2TrackerPage::Instrument:
    case Ui2TrackerPage::Record:
    case Ui2TrackerPage::None:
      return {};
    }
    return {};
  }

  bool Activate(Ui2TrackerPage page) {
    if (page == Ui2TrackerPage::None || page == activePage_)
      return false;
    const std::uint8_t sharedTrack = ActiveState().track;
    AlignTrack(page, sharedTrack);
    activePage_ = page;
    // AlignTrack rebuilds the destination controller so its page-local cursor
    // can inherit the shared track. Application navigation may activate that
    // page while SHIFT remains physically held; restore the hub-owned latch so
    // a following PLAY is still SHIFT+PLAY instead of a local context start.
    SetNavigationHeld(navigationHeld_);
    return true;
  }

  bool Synchronize(const Ui2TrackerGridState &state) {
    std::uint16_t heldModifiers = ActiveState().heldMask;
    if (navigationHeld_)
      heldModifiers = static_cast<std::uint16_t>(
          heldModifiers | TrackerActionBit(TrackerAction::Shift));
    song_ = Ui2SongController(state.track, state.songVisibleRow,
                              state.songRowOffset, state.liveMode);
    chain_ = Ui2ChainController(state.chainNumber, state.track, state.chainRow,
                                state.chainColumn);
    phrase_ = Ui2PhraseController(state.phraseNumber, state.track,
                                  state.phraseRow, state.phraseColumn,
                                  state.phraseDigit);
    phraseTable_ = Ui2TableController(
        Ui2TrackerPage::PhraseTable, state.phraseTableNumber, state.track,
        state.phraseTableRow, state.phraseTableColumn, state.phraseTableDigit);
    instrumentTable_ = Ui2TableController(
        Ui2TrackerPage::InstrumentTable, state.instrumentTableNumber,
        state.track, state.instrumentTableRow, state.instrumentTableColumn,
        state.instrumentTableDigit);
    activePage_ = state.activePage == Ui2TrackerPage::None
                      ? Ui2TrackerPage::Song
                      : state.activePage;
    SetNavigationHeld(navigationHeld_);
    SynchronizeActiveHeldModifiers(heldModifiers);
    pressOwners_.fill(Ui2TrackerPage::None);
    return true;
  }

  [[nodiscard]] Ui2TrackerGridState State() const {
    return {
        .activePage = activePage_,
        .track = ActiveState().track,
        .songVisibleRow = song_.VisibleRow(),
        .songRowOffset = song_.RowOffset(),
        .chainNumber = chain_.Number(),
        .chainRow = chain_.Row(),
        .chainColumn = chain_.Column(),
        .phraseNumber = phrase_.Number(),
        .phraseRow = phrase_.Row(),
        .phraseColumn = phrase_.Column(),
        .phraseDigit = phrase_.ParameterDigit(),
        .phraseTableNumber = phraseTable_.Number(),
        .phraseTableRow = phraseTable_.Row(),
        .phraseTableColumn = phraseTable_.Column(),
        .phraseTableDigit = phraseTable_.ParameterDigit(),
        .instrumentTableNumber = instrumentTable_.Number(),
        .instrumentTableRow = instrumentTable_.Row(),
        .instrumentTableColumn = instrumentTable_.Column(),
        .instrumentTableDigit = instrumentTable_.ParameterDigit(),
        .liveMode = song_.LiveMode(),
    };
  }

private:
  template <typename Controller>
  static void CaptureNumberedGridState(Ui2TrackerActiveControllerState &state,
                                       const Controller &controller) {
    state.row = controller.Row();
    state.column = controller.Column();
    state.number = controller.Number();
    state.track = controller.SelectedTrack();
    state.heldMask = controller.HeldMask();
    state.selection = controller.Selection();
    state.numberFocus = controller.NumberFocus();
    state.trackFocus = controller.TrackFocus();
  }

  template <typename Controller>
  static void CaptureParameterState(Ui2TrackerActiveControllerState &state,
                                    const Controller &controller) {
    state.digit = controller.ParameterDigit();
    state.enterDigitFocus = controller.EnterDigitFocus();
  }

  void SynchronizeActiveHeldModifiers(std::uint16_t mask) {
    switch (activePage_) {
    case Ui2TrackerPage::Song:
      song_.SynchronizeHeldModifiers(mask);
      break;
    case Ui2TrackerPage::Chain:
      chain_.SynchronizeHeldModifiers(mask);
      break;
    case Ui2TrackerPage::Phrase:
      phrase_.SynchronizeHeldModifiers(mask);
      break;
    case Ui2TrackerPage::PhraseTable:
      phraseTable_.SynchronizeHeldModifiers(mask);
      break;
    case Ui2TrackerPage::InstrumentTable:
      instrumentTable_.SynchronizeHeldModifiers(mask);
      break;
    case Ui2TrackerPage::Project:
    case Ui2TrackerPage::Mixer:
    case Ui2TrackerPage::Groove:
    case Ui2TrackerPage::Instrument:
    case Ui2TrackerPage::Record:
    case Ui2TrackerPage::None:
      break;
    }
  }

  void AlignTrack(Ui2TrackerPage page, std::uint8_t track) {
    switch (page) {
    case Ui2TrackerPage::Song:
      song_ = Ui2SongController(track, song_.VisibleRow(), song_.RowOffset(),
                                song_.LiveMode());
      break;
    case Ui2TrackerPage::Chain:
      chain_ = Ui2ChainController(chain_.Number(), track, chain_.Row(),
                                  chain_.Column());
      break;
    case Ui2TrackerPage::Phrase:
      phrase_ = Ui2PhraseController(
          phrase_.Number(), track, phrase_.Row(), phrase_.Column(),
          phrase_.ParameterDigit());
      break;
    case Ui2TrackerPage::PhraseTable:
      phraseTable_ = Ui2TableController(
          Ui2TrackerPage::PhraseTable, phraseTable_.Number(), track,
          phraseTable_.Row(), phraseTable_.Column(),
          phraseTable_.ParameterDigit());
      break;
    case Ui2TrackerPage::InstrumentTable:
      instrumentTable_ = Ui2TableController(
          Ui2TrackerPage::InstrumentTable, instrumentTable_.Number(), track,
          instrumentTable_.Row(), instrumentTable_.Column(),
          instrumentTable_.ParameterDigit());
      break;
    case Ui2TrackerPage::Project:
    case Ui2TrackerPage::Mixer:
    case Ui2TrackerPage::Groove:
    case Ui2TrackerPage::Instrument:
    case Ui2TrackerPage::Record:
    case Ui2TrackerPage::None:
      break;
    }
  }

  Ui2SongController song_{};
  Ui2ChainController chain_{};
  Ui2PhraseController phrase_{};
  Ui2TableController phraseTable_{Ui2TrackerPage::PhraseTable};
  Ui2TableController instrumentTable_{Ui2TrackerPage::InstrumentTable};
  bool navigationHeld_ = false;
  std::array<Ui2TrackerPage,
             static_cast<std::size_t>(TrackerAction::Count)>
      pressOwners_{};
  Ui2TrackerPage activePage_ = Ui2TrackerPage::Song;
};

class Ui2TrackerCommandExecutor {
public:
  explicit Ui2TrackerCommandExecutor(IUi2TrackerModelPort &port)
      : port_(port), hub_(port.LoadGridState()) {}

  [[nodiscard]] Ui2TrackerControllerHub &Hub() { return hub_; }
  [[nodiscard]] const Ui2TrackerControllerHub &Hub() const { return hub_; }
  [[nodiscard]] Ui2TrackerPage ActivePage() const { return hub_.ActivePage(); }
  [[nodiscard]] UiApplicationPage ActiveApplicationPage() const {
    return hub_.ActiveApplicationPage();
  }
  [[nodiscard]] Ui2TrackerActiveControllerState ActiveState() const {
    return hub_.ActiveState();
  }

  Ui2TrackerCommandBatch<> Handle(TrackerAction action, bool pressed) {
    Ui2TrackerCommandBatch<> batch = hub_.Handle(action, pressed);
    bool synchronize = false;
    for (std::uint8_t index = 0; index < batch.count; ++index) {
      const Ui2TrackerCommand &command = batch.commands[index];
      port_.ApplyGridCommand(command);
      if (command.type == Ui2TrackerCommandType::SwitchPage) {
        hub_.Activate(command.targetPage);
        synchronize = true;
      } else if (command.type == Ui2TrackerCommandType::SelectTrack ||
                 command.type == Ui2TrackerCommandType::WarpVertical ||
                 command.type == Ui2TrackerCommandType::JumpSection) {
        // These commands resolve through Song -> Chain -> Phrase model
        // context. Reload the accepted target instead of storing the
        // controller's optimistic cursor over a rejected empty cell.
        synchronize = true;
      }
    }
    if (synchronize)
      hub_.Synchronize(port_.LoadGridState());
    port_.StoreGridState(hub_.State());
    return batch;
  }

  bool SynchronizeFromPort() {
    return hub_.Synchronize(port_.LoadGridState());
  }

private:
  IUi2TrackerModelPort &port_;
  Ui2TrackerControllerHub hub_{};
};

static_assert(std::is_trivially_copyable_v<Ui2TrackerGridState>);
static_assert(sizeof(Ui2TrackerGridState) <= 24U,
              "grid state must remain a small embedded value");
static_assert(std::is_trivially_copyable_v<Ui2TrackerActiveControllerState>);

} // namespace ui2
