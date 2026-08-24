/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/Instruments/InstrumentNameVariable.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIBitmaskVarField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "BaseClasses/UITextField.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/vector.h"
#include "FieldView.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/Variable.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "ViewData.h"
#include <array>
#include <cstddef>
#include <cstdint>

class SampleInstrument;

enum class InstrumentViewUi2Kind : std::uint8_t {
  None,
  Sample,
  Midi,
  Sid,
  Opal,
};

enum class InstrumentViewUi2Focus : std::uint8_t {
  None,
  Name,
  Type,
  Field,
  Operator1,
  Operator2,
  Unmapped,
};

struct InstrumentViewUi2Choice {
  const char *const *options = nullptr;
  std::uint8_t count = 0;
  std::uint8_t current = 0;
  bool wrap = false;

  [[nodiscard]] const char *Value() const {
    return options != nullptr && current < count ? options[current] : "";
  }
};

struct InstrumentViewUi2Field {
  static constexpr std::size_t LabelCapacity = 16;
  static constexpr std::size_t ValueCapacity = 24;

  std::array<char, LabelCapacity> label{};
  std::array<char, ValueCapacity> value{};
  std::int16_t y = 0;
};

struct InstrumentViewUi2OperatorRow {
  static constexpr std::size_t LabelCapacity = 16;
  static constexpr std::size_t ValueCapacity = 8;

  std::array<char, LabelCapacity> label{};
  std::array<char, ValueCapacity> op1{};
  std::array<char, ValueCapacity> op2{};
};

// Allocation-free bridge from the mutable legacy InstrumentView to UI2. All
// displayed text is copied into bounded storage. The only borrowed pointers
// are the process-lifetime InstrumentTypeNames option table.
struct InstrumentViewUi2Snapshot {
  static constexpr std::size_t MaxFields = 16;
  static constexpr std::size_t MaxOperatorRows = 6;

  InstrumentViewUi2Choice type{};
  std::array<char, 3> number{};
  std::array<char, MAX_INSTRUMENT_NAME_LENGTH + 1> name{};
  InstrumentViewUi2Kind kind = InstrumentViewUi2Kind::None;
  std::array<InstrumentViewUi2Field, MaxFields> fields{};
  std::array<InstrumentViewUi2OperatorRow, MaxOperatorRows> operators{};
  std::uint8_t fieldCount = 0;
  std::uint8_t operatorCount = 0;
  InstrumentViewUi2Focus focus = InstrumentViewUi2Focus::None;
  std::uint8_t selectedField = 0;
  std::uint8_t selectedOperator = 0;
  std::uint8_t nameAction = 0;
};

static_assert(sizeof(InstrumentViewUi2Snapshot) <= 1024,
              "Instrument UI2 snapshot must stay cheap enough for ESP32");

class InstrumentView : public FieldView, public I_Observer {
public:
  InstrumentView(GUIWindow &w, ViewData *data);
  virtual ~InstrumentView();
  void Reset();

  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int){};
  virtual void OnFocus();
  virtual bool ShouldDrawBattery() const override;
  virtual bool ShouldDrawPlayTime() const override;
  void onInstrumentTypeChange(bool updateUI = false);
  bool checkInstrumentModified();
  void resetInstrumentToDefaults();
  void applyProposedTypeChangeUI();
  [[nodiscard]] InstrumentViewUi2Snapshot SnapshotForUi2();

protected:
  GUIPoint GetAnchor();
  void warpToNext(int offset);
  void onInstrumentChange();
  void fillSampleParameters();
  void fillSIDParameters();
  void fillMidiParameters();
  void fillOpalParameters();
  void fillNoneParameters();
  I_Instrument *getInstrument();
  void Update(Observable &o, I_ObservableData *d);
  void refreshInstrumentFields();
  void addNameTextField(I_Instrument *instr, GUIPoint &position);
  void handleInstrumentExport();

private:
  FourCC getFieldID(UIField *field);
  bool ShouldShowExperimentalBanner() const;
  void onConfirmInstrumentTypeChange(View &view, ModalView &dialog);
  void onConfirmResetInstrument(View &view, ModalView &dialog);
  void onConfirmSampleChange(View &view, ModalView &dialog);
  void onConfirmExportOverwrite(View &view, ModalView &dialog);
  void onRenameFinished(View &view, ModalView &dialog);

  static constexpr size_t SliceCountLabelSize = 20;
  Project *project_;
  FourCC lastFocusID_;
  WatchedVariable instrumentType_;
  int lastSampleIndex_;
  bool suppressSampleChangeWarning_;
  etl::string<SliceCountLabelSize> sliceCountLabel_;

  // Variables for export confirmation dialog
  I_Instrument *pendingPurgeInstrument_ = nullptr;
  SampleInstrument *pendingSampleChangeInstrument_ = nullptr;
  int pendingSampleChangeNewIndex_ = -1;
  I_Instrument *exportInstrument_ = nullptr;
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> exportName_;
  InstrumentType pendingInstrumentType_ = IT_NONE;

  etl::vector<UIIntVarField, 1> typeIntVarField_;
  etl::vector<UIActionField, 2> persistentActionField_;
  etl::vector<UIIntVarField, 40> intVarField_;
  etl::vector<UINoteVarField, 1> noteVarField_;
  etl::vector<UIStaticField, 10> staticField_;
  etl::vector<UIBigHexVarField, 4> bigHexVarField_;
  etl::vector<UIIntVarOffField, 2> intVarOffField_;
  etl::vector<UIActionField, 1> sampleActionField_;
  etl::vector<UIBitmaskVarField, 3> bitmaskVarField_;
  etl::vector<UITextField<MAX_INSTRUMENT_NAME_LENGTH>, 1> nameTextField_;
  etl::vector<InstrumentNameVariable, 1> nameVariables_;
};
#endif
