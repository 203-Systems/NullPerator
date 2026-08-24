/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "InstrumentView.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Config.h"
#include "Application/Views/ImportView.h"
#include "Application/Views/SampleEditorView.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Externals/etl/include/etl/to_string.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "ModalDialogs/MessageBox.h"
#include "ModalDialogs/TextInputModalView.h"
#include "System/System/System.h"
#include <Application/Utils/stringutils.h>
#include <array>
#include <cstdint>
#include <nanoprintf.h>

static constexpr InstrumentType kMaxSelectableInstrumentType =
    static_cast<InstrumentType>(IT_LAST - 1);

namespace {

template <std::size_t Capacity>
void CopyUi2Text(std::array<char, Capacity> &destination, const char *source,
                 bool normalizeFilename = false) {
  destination.fill('\0');
  if (source == nullptr || Capacity == 0)
    return;
  std::size_t output = 0;
  while (*source != '\0' && output + 1 < Capacity) {
    unsigned char character = static_cast<unsigned char>(*source++);
    if (normalizeFilename && character == '_')
      character = ' ';
    if (character >= 'a' && character <= 'z')
      character = static_cast<unsigned char>(character - ('a' - 'A'));
    destination[output++] = static_cast<char>(character);
  }
}

Variable *FindUi2Variable(I_Instrument *instrument, FourCC::enum_type id) {
  return instrument != nullptr ? instrument->FindVariable(id) : nullptr;
}

int Ui2Int(I_Instrument *instrument, FourCC::enum_type id,
           int fallback = 0) {
  Variable *variable = FindUi2Variable(instrument, id);
  return variable != nullptr ? variable->GetInt() : fallback;
}

const char *Ui2Bool(bool value) { return value ? "TRUE" : "FALSE"; }

template <std::size_t Capacity>
void CopyUi2VariableText(std::array<char, Capacity> &destination,
                         I_Instrument *instrument, FourCC::enum_type id,
                         const char *fallback = "--") {
  Variable *variable = FindUi2Variable(instrument, id);
  if (variable == nullptr) {
    CopyUi2Text(destination, fallback);
    return;
  }
  const auto value = variable->GetString();
  CopyUi2Text(destination, value.empty() ? fallback : value.c_str());
}

InstrumentViewUi2Field &AddUi2Field(InstrumentViewUi2Snapshot &snapshot,
                                    const char *label, const char *value,
                                    std::int16_t y) {
  InstrumentViewUi2Field &field = snapshot.fields[snapshot.fieldCount++];
  CopyUi2Text(field.label, label);
  CopyUi2Text(field.value, value);
  field.y = y;
  return field;
}

InstrumentViewUi2OperatorRow &
AddUi2Operator(InstrumentViewUi2Snapshot &snapshot, const char *label,
               const char *op1, const char *op2) {
  InstrumentViewUi2OperatorRow &row =
      snapshot.operators[snapshot.operatorCount++];
  CopyUi2Text(row.label, label);
  CopyUi2Text(row.op1, op1);
  CopyUi2Text(row.op2, op2);
  return row;
}

const char *Ui2LoopName(int value) {
  switch (value) {
  case SILM_ONESHOT:
    return "ONE SHOT";
  case SILM_LOOP:
    return "FORWARD";
  case SILM_LOOP_PINGPONG:
    return "PING PONG";
  case SILM_OSC:
    return "OSCILLATOR";
  case SILM_LOOPSYNC:
    return "LOOP SYNC";
  default:
    return "--";
  }
}

const char *Ui2SidWaveformName(int value) {
  switch (value) {
  case DWF_NONE:
    return "--";
  case DWF_TRI:
    return "A";
  case DWF_SAW:
    return "/";
  case DWF_TRI_SAW:
    return "A/";
  case DWF_SQ:
    return "PULSE";
  case DWF_TRI_SQ:
    return "A PULSE";
  case DWF_SAW_SQ:
    return "/ PULSE";
  case DWF_TRI_SAW_SQ:
    return "A/ PULSE";
  case DWF_NOISE:
    return "NOISE";
  default:
    return "--";
  }
}

const char *Ui2OpalAlgorithmName(int value) {
  return value == 0 ? "1*2" : value == 1 ? "1+2" : "--";
}

const char *Ui2OpalWaveName(int value) {
  static constexpr std::array<const char *, 8> names{
      "SINE", "HALF", "ABS", "PULS", "EVEN", "AB-E", "SQR", "DSQR"};
  return value >= 0 && value < static_cast<int>(names.size()) ? names[value]
                                                              : "--";
}

const char *Ui2OpalKeyscaleName(int value) {
  static constexpr std::array<const char *, 4> names{"0", "1.5", "3", "6"};
  return value >= 0 && value < static_cast<int>(names.size()) ? names[value]
                                                              : "--";
}

void Ui2Binary(char *destination, std::size_t capacity, unsigned int value,
               unsigned int width) {
  if (destination == nullptr || capacity == 0)
    return;
  const unsigned int digits =
      width < capacity ? width : static_cast<unsigned int>(capacity - 1);
  for (unsigned int index = 0; index < digits; ++index) {
    const unsigned int bit = digits - index - 1;
    destination[index] = (value & (1U << bit)) != 0U ? '1' : '0';
  }
  destination[digits] = '\0';
}

void Ui2Note(char *destination, std::size_t capacity, int note) {
  static constexpr std::array<const char *, 12> names{
      "C",  "C#", "D",  "D#", "E",  "F",
      "F#", "G",  "G#", "A",  "A#", "B"};
  if (destination == nullptr || capacity == 0)
    return;
  if (note < 0) {
    npf_snprintf(destination, capacity, "--");
    return;
  }
  npf_snprintf(destination, capacity, "%s%d", names[note % 12], note / 12 - 2);
}

void ResolveUi2Focus(InstrumentViewUi2Snapshot &snapshot,
                     FourCC::enum_type focusId) {
  snapshot.focus = InstrumentViewUi2Focus::Unmapped;
  switch (focusId) {
  case FourCC::VarInstrumentType:
    snapshot.focus = InstrumentViewUi2Focus::Type;
    return;
  case FourCC::InstrumentName:
    snapshot.focus = InstrumentViewUi2Focus::Name;
    snapshot.nameAction = 2;
    return;
  case FourCC::ActionImport:
    snapshot.focus = InstrumentViewUi2Focus::Name;
    snapshot.nameAction = 0;
    return;
  case FourCC::ActionExport:
    snapshot.focus = InstrumentViewUi2Focus::Name;
    snapshot.nameAction = 1;
    return;
  default:
    break;
  }

  const auto selectField = [&](std::uint8_t index) {
    snapshot.focus = InstrumentViewUi2Focus::Field;
    snapshot.selectedField = index;
  };
  const auto selectOperator = [&](std::uint8_t index, bool second) {
    snapshot.focus = second ? InstrumentViewUi2Focus::Operator2
                            : InstrumentViewUi2Focus::Operator1;
    snapshot.selectedOperator = index;
  };

  switch (snapshot.kind) {
  case InstrumentViewUi2Kind::Sample:
    switch (focusId) {
    case FourCC::SampleInstrumentSample:
      selectField(0);
      break;
    case FourCC::ActionShowSampleSlices:
      selectField(1);
      break;
    case FourCC::SampleInstrumentVolume:
      selectField(2);
      break;
    case FourCC::SampleInstrumentPan:
      selectField(3);
      break;
    case FourCC::SampleInstrumentRootNote:
      selectField(4);
      break;
    case FourCC::SampleInstrumentFineTune:
      selectField(5);
      break;
    case FourCC::SampleInstrumentCrushVolume:
      selectField(6);
      break;
    case FourCC::SampleInstrumentCrush:
      selectField(7);
      break;
    case FourCC::SampleInstrumentDownsample:
      selectField(8);
      break;
    case FourCC::SampleInstrumentFilterCutOff:
    case FourCC::SampleInstrumentFilterResonance:
    case FourCC::SampleInstrumentFilterType:
    case FourCC::SampleInstrumentFilterMode:
      selectField(9);
      break;
    case FourCC::SampleInstrumentLoopMode:
      selectField(10);
      break;
    default:
      break;
    }
    return;
  case InstrumentViewUi2Kind::Midi:
    switch (focusId) {
    case FourCC::MidiInstrumentChannel:
      selectField(0);
      break;
    case FourCC::MidiInstrumentVolume:
      selectField(1);
      break;
    case FourCC::MidiInstrumentNoteLength:
      selectField(2);
      break;
    case FourCC::MidiInstrumentProgram:
      selectField(3);
      break;
    case FourCC::MidiInstrumentTableAutomation:
      selectField(4);
      break;
    case FourCC::MidiInstrumentTable:
      selectField(5);
      break;
    default:
      break;
    }
    return;
  case InstrumentViewUi2Kind::Sid:
    switch (focusId) {
    case FourCC::SIDInstrumentOSCNumber:
    case FourCC::SIDInstrumentPulseWidth:
      selectField(0);
      break;
    case FourCC::SIDInstrumentWaveform:
      selectField(1);
      break;
    case FourCC::SIDInstrumentVSync:
      selectField(2);
      break;
    case FourCC::SIDInstrumentRingModulator:
      selectField(3);
      break;
    case FourCC::SIDInstrumentADSR:
      selectField(4);
      break;
    case FourCC::SIDInstrumentFilterOn:
    case FourCC::SIDInstrument1FilterCut:
    case FourCC::SIDInstrument2FilterCut:
      selectField(5);
      break;
    case FourCC::SIDInstrument1FilterResonance:
    case FourCC::SIDInstrument2FilterResonance:
      selectField(6);
      break;
    case FourCC::SIDInstrument1FilterMode:
    case FourCC::SIDInstrument2FilterMode:
      selectField(7);
      break;
    default:
      break;
    }
    return;
  case InstrumentViewUi2Kind::Opal:
    switch (focusId) {
    case FourCC::OPALInstrumentAlgorithm:
      selectField(0);
      break;
    case FourCC::OPALInstrumentDeepTremeloVibrato:
      selectField(1);
      break;
    case FourCC::OPALInstrumentFeedback:
      selectField(2);
      break;
    case FourCC::OPALInstrumentOp1Level:
      selectOperator(0, false);
      break;
    case FourCC::OPALInstrumentOp2Level:
      selectOperator(0, true);
      break;
    case FourCC::OPALInstrumentOp1Multiplier:
      selectOperator(1, false);
      break;
    case FourCC::OPALInstrumentOp2Multiplier:
      selectOperator(1, true);
      break;
    case FourCC::OPALInstrumentOp1ADSR:
      selectOperator(2, false);
      break;
    case FourCC::OPALInstrumentOp2ADSR:
      selectOperator(2, true);
      break;
    case FourCC::OPALInstrumentOp1WaveShape:
      selectOperator(3, false);
      break;
    case FourCC::OPALInstrumentOp2WaveShape:
      selectOperator(3, true);
      break;
    case FourCC::OPALInstrumentOp1TremVibSusKSR:
      selectOperator(4, false);
      break;
    case FourCC::OPALInstrumentOp2TremVibSusKSR:
      selectOperator(4, true);
      break;
    case FourCC::OPALInstrumentOp1KeyScaleLevel:
      selectOperator(5, false);
      break;
    case FourCC::OPALInstrumentOp2KeyScaleLevel:
      selectOperator(5, true);
      break;
    default:
      break;
    }
    return;
  case InstrumentViewUi2Kind::None:
    return;
  }
}

} // namespace

InstrumentView::InstrumentView(GUIWindow &w, ViewData *data)
    : FieldView(w, data), instrumentType_(FourCC::VarInstrumentType,
                                          InstrumentTypeNames, IT_LAST, 0),
      lastSampleIndex_(-1), suppressSampleChangeWarning_(false) {
  project_ = data->project_;

  GUIPoint position = GUIPoint(1, 3);
  typeIntVarField_.emplace_back(position, *&instrumentType_, "Type: %s", 0,
                                static_cast<int>(kMaxSelectableInstrumentType),
                                1, 1);
  fieldList_.insert(fieldList_.end(), &(*typeIntVarField_.rbegin()));
  (*typeIntVarField_.rbegin()).AddObserver(*this);
  lastFocusID_ = FourCC::VarInstrumentType;

  // Create the name field with the actual instrument variable
  I_Instrument *instr = getInstrument();
  if (instr) {
    // NONE dont have a name field
    if (instr->GetType() != IT_NONE) {
      position._y = 5;
      addNameTextField(instr, position);
    }
  }

  // add ui action fields for exporting and importing instrument settings
  position._y = 3;

  position._x += 15;
  persistentActionField_.emplace_back("Import", FourCC::ActionImport, position);
  fieldList_.insert(fieldList_.end(), &(*persistentActionField_.rbegin()));
  (*persistentActionField_.rbegin()).AddObserver(*this);
  lastFocusID_ = FourCC::ActionImport;

  position._x += 7;
  persistentActionField_.emplace_back("Export", FourCC::ActionExport, position);
  fieldList_.insert(fieldList_.end(), &(*persistentActionField_.rbegin()));
  (*persistentActionField_.rbegin()).AddObserver(*this);
  lastFocusID_ = FourCC::ActionExport;

  sliceCountLabel_.clear();
}

InstrumentView::~InstrumentView() {}

InstrumentViewUi2Snapshot InstrumentView::SnapshotForUi2() {
  InstrumentViewUi2Snapshot snapshot;
  snapshot.type.options = InstrumentTypeNames;
  snapshot.type.count = IT_LAST;
  snapshot.type.wrap = true;

  I_Instrument *instrument = getInstrument();
  if (instrument == nullptr) {
    CopyUi2Text(snapshot.number, "00");
    CopyUi2Text(snapshot.name, "--");
    snapshot.focus = InstrumentViewUi2Focus::Unmapped;
    return snapshot;
  }

  const InstrumentType type = instrument->GetType();
  snapshot.type.current = static_cast<std::uint8_t>(type);
  snapshot.kind = static_cast<InstrumentViewUi2Kind>(type);
  npf_snprintf(snapshot.number.data(), snapshot.number.size(), "%02X",
               viewData_->currentInstrumentID_ & 0xFF);
  if (type == IT_NONE) {
    CopyUi2Text(snapshot.name, "--");
  } else {
    const auto displayName = instrument->GetDisplayName();
    CopyUi2Text(snapshot.name,
                displayName.empty() ? "--" : displayName.c_str(), true);
  }

  char value[InstrumentViewUi2Field::ValueCapacity]{};
  switch (type) {
  case IT_NONE:
    break;
  case IT_SAMPLE: {
    SampleInstrument *sample = static_cast<SampleInstrument *>(instrument);
    const auto filename = sample->GetSampleFileName();
    InstrumentViewUi2Field &sampleField =
        AddUi2Field(snapshot, "SAMPLE", "--", 66);
    CopyUi2Text(sampleField.value,
                filename.empty() ? "--" : filename.c_str(), true);

    int sliceCount = 0;
    for (std::size_t index = 0; index < SampleInstrument::MaxSlices; ++index) {
      if (sample->IsSliceDefined(index))
        ++sliceCount;
    }
    if (sliceCount <= 1) {
      npf_snprintf(value, sizeof(value), "OFF / ADJUST");
    } else {
      npf_snprintf(value, sizeof(value), "%d / ADJUST", sliceCount);
    }
    AddUi2Field(snapshot, "SLICES", value, 76);
    npf_snprintf(value, sizeof(value), "%02X",
                 Ui2Int(instrument, FourCC::SampleInstrumentVolume) & 0xFF);
    AddUi2Field(snapshot, "VOLUME", value, 86);
    npf_snprintf(value, sizeof(value), "%02X",
                 Ui2Int(instrument, FourCC::SampleInstrumentPan) & 0xFF);
    AddUi2Field(snapshot, "PAN", value, 96);
    Ui2Note(value, sizeof(value),
            Ui2Int(instrument, FourCC::SampleInstrumentRootNote));
    AddUi2Field(snapshot, "ROOT NOTE", value, 106);
    npf_snprintf(value, sizeof(value), "%02X",
                 Ui2Int(instrument, FourCC::SampleInstrumentFineTune) & 0xFF);
    AddUi2Field(snapshot, "DETUNE", value, 116);
    npf_snprintf(
        value, sizeof(value), "%02X",
        Ui2Int(instrument, FourCC::SampleInstrumentCrushVolume) & 0xFF);
    AddUi2Field(snapshot, "DRIVE", value, 126);
    npf_snprintf(value, sizeof(value), "%d",
                 Ui2Int(instrument, FourCC::SampleInstrumentCrush));
    AddUi2Field(snapshot, "CRUSH", value, 136);
    npf_snprintf(value, sizeof(value), "%d",
                 Ui2Int(instrument, FourCC::SampleInstrumentDownsample));
    AddUi2Field(snapshot, "DOWNSAMPLE", value, 146);
    npf_snprintf(
        value, sizeof(value), "LP / %02X %02X",
        Ui2Int(instrument, FourCC::SampleInstrumentFilterCutOff) & 0xFF,
        Ui2Int(instrument, FourCC::SampleInstrumentFilterResonance) & 0xFF);
    AddUi2Field(snapshot, "FILTER", value, 156);
    AddUi2Field(snapshot, "LOOP",
                Ui2LoopName(
                    Ui2Int(instrument, FourCC::SampleInstrumentLoopMode)),
                166);
    break;
  }
  case IT_MIDI:
    npf_snprintf(value, sizeof(value), "%02d",
                 Ui2Int(instrument, FourCC::MidiInstrumentChannel) + 1);
    AddUi2Field(snapshot, "CHANNEL", value, 66);
    npf_snprintf(value, sizeof(value), "%02X",
                 Ui2Int(instrument, FourCC::MidiInstrumentVolume) & 0xFF);
    AddUi2Field(snapshot, "VOLUME", value, 76);
    npf_snprintf(value, sizeof(value), "%02X",
                 Ui2Int(instrument, FourCC::MidiInstrumentNoteLength) & 0xFF);
    AddUi2Field(snapshot, "LENGTH", value, 86);
    if (Ui2Int(instrument, FourCC::MidiInstrumentProgram, VAR_OFF) == VAR_OFF) {
      npf_snprintf(value, sizeof(value), "--");
    } else {
      npf_snprintf(value, sizeof(value), "%02X",
                   Ui2Int(instrument, FourCC::MidiInstrumentProgram) & 0xFF);
    }
    AddUi2Field(snapshot, "PROGRAM", value, 96);
    AddUi2Field(
        snapshot, "AUTOMATION",
        Ui2Bool(Ui2Int(instrument, FourCC::MidiInstrumentTableAutomation) != 0),
        106);
    if (Ui2Int(instrument, FourCC::MidiInstrumentTable, VAR_OFF) == VAR_OFF) {
      npf_snprintf(value, sizeof(value), "--");
    } else {
      npf_snprintf(value, sizeof(value), "%02X",
                   Ui2Int(instrument, FourCC::MidiInstrumentTable) & 0xFF);
    }
    AddUi2Field(snapshot, "TABLE", value, 116);
    break;
  case IT_SID: {
    SIDInstrument *sid = static_cast<SIDInstrument *>(instrument);
    npf_snprintf(value, sizeof(value), "PULSEWIDTH %03X",
                 Ui2Int(instrument, FourCC::SIDInstrumentPulseWidth) & 0xFFF);
    AddUi2Field(snapshot, "OSCILLATOR", value, 66);
    AddUi2Field(
        snapshot, "WAVEFORM",
        Ui2SidWaveformName(
            Ui2Int(instrument, FourCC::SIDInstrumentWaveform)),
        76);
    AddUi2Field(snapshot, "OSC SYNC",
                Ui2Bool(Ui2Int(instrument, FourCC::SIDInstrumentVSync) != 0),
                86);
    AddUi2Field(
        snapshot, "RING MOD",
        Ui2Bool(Ui2Int(instrument, FourCC::SIDInstrumentRingModulator) != 0),
        96);
    npf_snprintf(value, sizeof(value), "%04X",
                 Ui2Int(instrument, FourCC::SIDInstrumentADSR) & 0xFFFF);
    AddUi2Field(snapshot, "ENV ADSR", value, 106);
    const bool firstChip = sid->GetChip() == SID1;
    const FourCC::enum_type cutoff = firstChip
                                         ? FourCC::SIDInstrument1FilterCut
                                         : FourCC::SIDInstrument2FilterCut;
    const FourCC::enum_type resonance =
        firstChip ? FourCC::SIDInstrument1FilterResonance
                  : FourCC::SIDInstrument2FilterResonance;
    const FourCC::enum_type mode = firstChip
                                       ? FourCC::SIDInstrument1FilterMode
                                       : FourCC::SIDInstrument2FilterMode;
    npf_snprintf(value, sizeof(value), "CUTOFF %03X",
                 Ui2Int(instrument, cutoff) & 0x7FF);
    AddUi2Field(snapshot, "FILTER", value, 116);
    npf_snprintf(value, sizeof(value), "%X",
                 Ui2Int(instrument, resonance) & 0xF);
    AddUi2Field(snapshot, "RESONANCE", value, 126);
    InstrumentViewUi2Field &modeField =
        AddUi2Field(snapshot, "MODE", "--", 136);
    CopyUi2VariableText(modeField.value, instrument, mode);
    break;
  }
  case IT_OPAL: {
    AddUi2Field(snapshot, "ALGORITHM",
                Ui2OpalAlgorithmName(
                    Ui2Int(instrument, FourCC::OPALInstrumentAlgorithm)),
                82);
    InstrumentViewUi2Field &deep =
        AddUi2Field(snapshot, "DEEP TREM/VIB", "00", 93);
    Ui2Binary(deep.value.data(), deep.value.size(),
              Ui2Int(instrument, FourCC::OPALInstrumentDeepTremeloVibrato), 2);
    npf_snprintf(value, sizeof(value), "%X",
                 Ui2Int(instrument, FourCC::OPALInstrumentFeedback) & 0x7);
    AddUi2Field(snapshot, "FEEDBACK", value, 104);

    char op1[InstrumentViewUi2OperatorRow::ValueCapacity]{};
    char op2[InstrumentViewUi2OperatorRow::ValueCapacity]{};
    npf_snprintf(op1, sizeof(op1), "%02X",
                 Ui2Int(instrument, FourCC::OPALInstrumentOp1Level) & 0x3F);
    npf_snprintf(op2, sizeof(op2), "%02X",
                 Ui2Int(instrument, FourCC::OPALInstrumentOp2Level) & 0x3F);
    AddUi2Operator(snapshot, "LEVEL", op1, op2);
    npf_snprintf(op1, sizeof(op1), "%X",
                 Ui2Int(instrument, FourCC::OPALInstrumentOp1Multiplier) & 0xF);
    npf_snprintf(op2, sizeof(op2), "%X",
                 Ui2Int(instrument, FourCC::OPALInstrumentOp2Multiplier) & 0xF);
    AddUi2Operator(snapshot, "MULTIPLIER", op1, op2);
    npf_snprintf(op1, sizeof(op1), "%04X",
                 Ui2Int(instrument, FourCC::OPALInstrumentOp1ADSR) & 0xFFFF);
    npf_snprintf(op2, sizeof(op2), "%04X",
                 Ui2Int(instrument, FourCC::OPALInstrumentOp2ADSR) & 0xFFFF);
    AddUi2Operator(snapshot, "A/D/S/R", op1, op2);
    AddUi2Operator(
        snapshot, "SHAPE",
        Ui2OpalWaveName(
            Ui2Int(instrument, FourCC::OPALInstrumentOp1WaveShape)),
        Ui2OpalWaveName(
            Ui2Int(instrument, FourCC::OPALInstrumentOp2WaveShape)));
    Ui2Binary(op1, sizeof(op1),
              Ui2Int(instrument, FourCC::OPALInstrumentOp1TremVibSusKSR), 4);
    Ui2Binary(op2, sizeof(op2),
              Ui2Int(instrument, FourCC::OPALInstrumentOp2TremVibSusKSR), 4);
    AddUi2Operator(snapshot, "TR/VB/SU/KSR", op1, op2);
    AddUi2Operator(
        snapshot, "KEYSCALE",
        Ui2OpalKeyscaleName(
            Ui2Int(instrument, FourCC::OPALInstrumentOp1KeyScaleLevel)),
        Ui2OpalKeyscaleName(
            Ui2Int(instrument, FourCC::OPALInstrumentOp2KeyScaleLevel)));
    break;
  }
  case IT_LAST:
    snapshot.kind = InstrumentViewUi2Kind::None;
    break;
  }

  UIField *focusField = GetFocus();
  ResolveUi2Focus(snapshot,
                  focusField != nullptr ? getFieldID(focusField).get_enum()
                                        : FourCC::Default);
  return snapshot;
}

GUIPoint InstrumentView::GetAnchor() { return GUIPoint(1, 4); }

bool InstrumentView::ShouldShowExperimentalBanner() const {
  InstrumentBank *bank = viewData_ ? viewData_->project_->GetInstrumentBank() : nullptr;
  I_Instrument *instr =
      bank ? bank->GetInstrument(viewData_->currentInstrumentID_) : nullptr;
  if (!instr) {
    return false;
  }
  InstrumentType type = instr->GetType();
  return type == IT_SID || type == IT_OPAL;
}

bool InstrumentView::ShouldDrawBattery() const {
  return !ShouldShowExperimentalBanner();
}

bool InstrumentView::ShouldDrawPlayTime() const {
  return !ShouldShowExperimentalBanner();
}

FourCC InstrumentView::getFieldID(UIField *field) {
  if (field == nullptr || field->IsStatic()) {
    return FourCC::VarInstrumentType;
  }

  if (!nameTextField_.empty() && field == &nameTextField_.front()) {
    return FourCC::InstrumentName;
  }
  if (!persistentActionField_.empty() && field == &persistentActionField_[0]) {
    return FourCC::ActionImport;
  }
  if (persistentActionField_.size() > 1 && field == &persistentActionField_[1]) {
    return FourCC::ActionExport;
  }
  if (!sampleActionField_.empty() && field == &sampleActionField_.front()) {
    return FourCC::ActionShowSampleSlices;
  }

  return static_cast<UIIntVarField *>(field)->GetVariableID();
}

void InstrumentView::Reset() {
  lastSampleIndex_ = -1;
  suppressSampleChangeWarning_ = false;
  exportInstrument_ = nullptr;
  exportName_.clear();
  lastFocusID_ = FourCC::VarInstrumentType;
  instrumentType_.SetInt(0, false);
  sliceCountLabel_.clear();
}

static void updateSliceCountLabel(etl::string<20> &label,
                                  SampleInstrument *instrument) {
  int32_t count = 0;
  if (instrument) {
    for (size_t i = 0; i < SampleInstrument::MaxSlices; ++i) {
      if (instrument->IsSliceDefined(i)) {
        count++;
      }
    }
  }
  if (count <= 1) {
    label = "slices: off";
  } else {
    label = "slices: ";
    etl::format_spec format;
    format.width(2).fill(' ');
    etl::to_string(count, label, format, true);
  }
}

void InstrumentView::addNameTextField(I_Instrument *instr, GUIPoint &position) {
  nameVariables_.emplace_back(instr);
  Variable &nameVar = *nameVariables_.rbegin();

  auto label =
      etl::make_string_with_capacity<MAX_UITEXTFIELD_LABEL_LENGTH>("Name: ");

  // Use an empty default name - we don't want to populate with sample filename
  // The display name will still be shown on the phrase screen via
  // GetDisplayName()
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> defaultName;

  nameTextField_.emplace_back(nameVar, position, label, FourCC::InstrumentName,
                              defaultName);
  fieldList_.insert(fieldList_.end(), &(*nameTextField_.rbegin()));
}

I_Instrument *InstrumentView::getInstrument() {
  int id = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  return bank->GetInstrument(id);
};

void InstrumentView::onInstrumentTypeChange(bool updateUI) {
  auto nuType = (InstrumentType)instrumentType_.GetInt();
  I_Instrument *old = getInstrument();

  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();

  auto id = viewData_->currentInstrumentID_;
  // release prev instrument back to available pool
  if (old != nullptr) {
    // first check if the instrument type actually changed, because could be
    // user is at end of instrument list and just keeps pressing key combo to
    // trigger next instrument event again and again
    if (old->GetType() == nuType) {
      if (updateUI) {
        refreshInstrumentFields();
      }
      return;
    }
    bank->releaseInstrument(viewData_->currentInstrumentID_);
  }

  // now assign new instrument type to the current instrument slot id
  unsigned short result = bank->GetNextAndAssignID(nuType, id);

  if (result == NO_MORE_INSTRUMENT) {
    Trace::Error("INSTRUMENTVIEW", "Failed to assign new instrument type: %d",
                 nuType);

    // TODO (democloid): this is a hack in order to ignore all existence of
    // certain instruments and seamlessly set NONE
    // Needed as the alternative is to change InstrumentTypeNames array plus all
    // switch instances which reference the types that wouldn't be available on
    // this platform

    // Show a dialog to the user
    char message[40];
    npf_snprintf(message, sizeof(message), "%s instruments exhausted!",
                 InstrumentTypeNames[nuType]);
    MessageBox *mb =
        MessageBox::Create(*this, message, "Trying next...", MBBF_OK);
    DoModal(mb);

    // Try to find the next available instrument type
    bool found = false;
    for (int i = nuType + 1;
         i <= static_cast<int>(kMaxSelectableInstrumentType); i++) {
      InstrumentType nextType = (InstrumentType)i;
      result = bank->GetNextAndAssignID(nextType, id);
      if (result != NO_MORE_INSTRUMENT) {
        Trace::Log("INSTRUMENTVIEW", "Assigned next available type: %d",
                   nextType);
        instrumentType_.SetInt(nextType, false);
        found = true;
        break; // Exit loop on success
      }
    }

    if (!found) {
      Trace::Log("INSTRUMENTVIEW",
                 "No other instrument types available, setting to NONE");
      instrumentType_.SetInt(IT_NONE, false);
    }

    refreshInstrumentFields();
    isDirty_ = true;
    return;
  }

  // Get the new instrument after type change
  I_Instrument *newInstr = getInstrument();
  if (newInstr) {
    Trace::Log("INSTRUMENTVIEW", "New instrument type: %d",
               newInstr->GetType());
  }

  // Refresh the UI fields for the new instrument type
  refreshInstrumentFields();

  // Mark the view as dirty to ensure it gets redrawn
  isDirty_ = true;
}

void InstrumentView::applyProposedTypeChangeUI() {
  instrumentType_.SetInt(pendingInstrumentType_, false);
  onInstrumentTypeChange();
}

void InstrumentView::onInstrumentChange() {

  ClearFocus();

  I_Instrument *old = getInstrument();
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();

  if (getInstrument() != old) {
    getInstrument()->RemoveObserver(*this);
  };

  // update type field to match current instrument
  ((WatchedVariable *)&instrumentType_)->SetInt(getInstrument()->GetType());

  refreshInstrumentFields();
};

void InstrumentView::refreshInstrumentFields() {
  for (auto &f : intVarField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : bigHexVarField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : intVarOffField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : bitmaskVarField_) {
    f.RemoveObserver(*this);
  }
  for (auto &f : sampleActionField_) {
    f.RemoveObserver(*this);
  }

  fieldList_.clear();
  intVarField_.clear();
  noteVarField_.clear();
  staticField_.clear();
  bigHexVarField_.clear();
  intVarOffField_.clear();
  sampleActionField_.clear();
  bitmaskVarField_.clear();
  nameTextField_.clear();
  nameVariables_.clear();
  lastSampleIndex_ = -1;

  // first put back the type field as its shown on *all* instrument types
  fieldList_.insert(fieldList_.end(), &(*typeIntVarField_.rbegin()));
  lastFocusID_ = FourCC::VarInstrumentType;

  // Re-add the action fields for export and import only if not IT_NONE
  if (instrumentType_.GetInt() != IT_NONE) {
    if (persistentActionField_.size() > 1) {
      // Import is temporarily moved onto Export's column for IT_NONE, so move
      // it back relative to Export when both actions are visible again.
      GUIPoint importPos = persistentActionField_[1].GetPosition();
      importPos._x -= 7;
      persistentActionField_[0].SetPosition(importPos);
    }
    for (auto &action : persistentActionField_) {
      fieldList_.insert(fieldList_.end(), &action);
      action.AddObserver(*this); // Make sure observers are re-added
    }
  } else {
    // add back only the import field for IT_NONE
    // bit of a hack !!since we just assume that import is the first action
    // field, and show it in Export's column so the lone action stays aligned
    if (persistentActionField_.size() > 1) {
      GUIPoint exportPos = persistentActionField_[1].GetPosition();
      persistentActionField_[0].SetPosition(exportPos);
    }
    fieldList_.insert(fieldList_.end(), &(*persistentActionField_.begin()));
    (*persistentActionField_.rbegin()).AddObserver(*this);
  }

  // Create a new nameTextField_ if the instrument type supports it
  if (instrumentType_.GetInt() != IT_NONE) {
    I_Instrument *instr = getInstrument();
    if (instr) {
      GUIPoint position = GetAnchor();
      addNameTextField(instr, position);
    }
  }

  InstrumentType it = getInstrument()->GetType();
  switch (it) {
  case IT_NONE:
    fillNoneParameters();
    break;
  case IT_MIDI:
    fillMidiParameters();
    break;
  case IT_SID:
    fillSIDParameters();
    break;
  case IT_SAMPLE:
    fillSampleParameters();
    break;
  case IT_OPAL:
    fillOpalParameters();
    break;
  case IT_LAST:
    // NA
    break;
  };

  for (auto field : fieldList_) {
    if (getFieldID(field) == lastFocusID_) {
      SetFocus(field);
      break;
    }
  }

  // observer all var fields so we can mark the instrument as modified
  // to be able to show confirmation dialog when switching instrument type
  for (auto &f : intVarField_) {
    f.AddObserver(*this);
  }
  for (auto &f : bigHexVarField_) {
    f.AddObserver(*this);
  }
  for (auto &f : intVarOffField_) {
    f.AddObserver(*this);
  }
  for (auto &f : bitmaskVarField_) {
    f.AddObserver(*this);
  }

  getInstrument()->AddObserver(*this);
}

void InstrumentView::fillNoneParameters() {}

void InstrumentView::fillSampleParameters() {
  int i = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  I_Instrument *instr = bank->GetInstrument(i);
  SampleInstrument *instrument = (SampleInstrument *)instr;
  lastSampleIndex_ = instrument->GetSampleIndex();

  GUIPoint position = GetAnchor();
  const int baseX = position._x;

  // offset y to account for instrument type and export/import fields
  position._y += 2;

  Variable *v = instrument->FindVariable(FourCC::SampleInstrumentSample);
  SamplePool *sp = SamplePool::GetInstance();
  intVarField_.emplace_back(position, *v, "sample: %.17s", 0,
                            sp->GetNameListSize() - 1, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  updateSliceCountLabel(sliceCountLabel_, instrument);
  staticField_.emplace_back(position, sliceCountLabel_.c_str());
  fieldList_.insert(fieldList_.end(), &staticField_.back());

  GUIPoint actionPos = position;
  actionPos._x = baseX + 12;
  sampleActionField_.emplace_back("Adjust", FourCC::ActionShowSampleSlices,
                                  actionPos);
  fieldList_.insert(fieldList_.end(), &sampleActionField_.back());
  sampleActionField_.back().AddObserver(*this);

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentVolume);
  intVarField_.emplace_back(position, *v, "volume: %d [%2.2X]", 0, 255, 1, 10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentPan);
  intVarField_.emplace_back(position, *v, "pan: %2.2X", 0, 0xFE, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentRootNote);
  noteVarField_.emplace_back(position, *v, "root note: %s", 0, 0x7F, 1, 0x0C);
  fieldList_.insert(fieldList_.end(), &(*noteVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentFineTune);
  intVarField_.emplace_back(position, *v, "detune: %2.2X", 0, 255, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._x += 12;
  v = instrument->FindVariable(FourCC::SampleInstrumentCrushVolume);
  intVarField_.emplace_back(position, *v, "drive: %2.2X", 0, 0xFF, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  position._x = baseX;

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentCrush);
  intVarField_.emplace_back(position, *v, "crush: %d", 1, 0x10, 1, 4);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentDownsample);
  intVarField_.emplace_back(position, *v, "downsample: %d", 0, 8, 1, 4);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 2;
  staticField_.emplace_back(position, "flt cut/res:");
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  position._x += 13;
  v = instrument->FindVariable(FourCC::SampleInstrumentFilterCutOff);
  intVarField_.emplace_back(position, *v, "%2.2X", 0, 0xFF, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._x += 3;
  v = instrument->FindVariable(FourCC::SampleInstrumentFilterResonance);
  intVarField_.emplace_back(position, *v, "%2.2X", 0, 0xFF, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._x -= 16;

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentFilterType);
  intVarField_.emplace_back(position, *v, "type: %2.2X", 0, 0xFF, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentFilterMode);
  intVarField_.emplace_back(position, *v, "Mode: %s", 0, 2, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentInterpolation);
  intVarField_.emplace_back(position, *v, "interpolation: %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentLoopMode);
  intVarField_.emplace_back(position, *v, "loop mode: %s", 0, SILM_LAST - 1, 1,
                            1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentStart);
  bigHexVarField_.emplace_back(position, *v, 7, "start: %7.7X", 0,
                               instrument->GetSampleSize() - 1, 16);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentLoopStart);
  bigHexVarField_.emplace_back(position, *v, 7, "loop start: %7.7X", 0,
                               instrument->GetSampleSize() - 1, 16);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentEnd);
  bigHexVarField_.emplace_back(position, *v, 7, "loop end: %7.7X", 0,
                               instrument->GetSampleSize() - 1, 16);
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SampleInstrumentTable);
  intVarOffField_.emplace_back(position, *v, "table: %2.2X", 0x00,
                               TABLE_COUNT - 1, 1, 0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarOffField_.rbegin()));

  v = instrument->FindVariable(FourCC::SampleInstrumentTableAutomation);
  position._x += 12;
  intVarField_.emplace_back(position, *v, "auto: %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
};

void InstrumentView::fillSIDParameters() {
  int i = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  SIDInstrument *instrument = (SIDInstrument *)bank->GetInstrument(i);
  GUIPoint position = GetAnchor();

  // offset y to account for instrument type, name and export/import fields
  position._y += 2;
  staticField_.emplace_back(position, "Oscillator Settings" char_line_5_s);
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  position._y += 2;
  Variable *v = instrument->FindVariable(FourCC::SIDInstrumentOSCNumber);
  intVarField_.emplace_back(position, *v, "Oscillator:    %1.1X", 0, 0x2, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SIDInstrumentPulseWidth);
  intVarField_.emplace_back(position, *v, "  Pulsewidth:  %2.2X", 0, 0xFFF, 1,
                            0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SIDInstrumentWaveform);

  intVarField_.emplace_back(position, *v, "  Waveform:    %s", 0, DWF_LAST - 1,
                            1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SIDInstrumentVSync);
  intVarField_.emplace_back(position, *v, "  Osc Sync:    %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::SIDInstrumentRingModulator);
  intVarField_.emplace_back(position, *v, "  Ring Mod:    %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 2;
  v = instrument->FindVariable(FourCC::SIDInstrumentADSR);
  bigHexVarField_.emplace_back(UIBigHexVarField(
      position, *v, 4, "Env. A/D/S/R:  %4.4X", 0, 0xFFFF, 16, true));
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 2;
  staticField_.emplace_back(position, "Chip Settings" char_line_11_s);
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  position._y += 2;
  v = instrument->FindVariable(FourCC::SIDInstrumentFilterOn);
  intVarField_.emplace_back(position, *v, "Filter:        %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  switch (instrument->GetChip()) {
  case SID1:
    v = instrument->FindVariable(FourCC::SIDInstrument1FilterCut);
    break;
  case SID2:
    v = instrument->FindVariable(FourCC::SIDInstrument2FilterCut);
    break;
  }
  intVarField_.emplace_back(position, *v, "  Cutoff:      %1.1X", 0, 0x7FF, 1,
                            0x10);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  switch (instrument->GetChip()) {
  case SID1:
    v = instrument->FindVariable(FourCC::SIDInstrument1FilterResonance);
    break;
  case SID2:
    v = instrument->FindVariable(FourCC::SIDInstrument2FilterResonance);
    break;
  }
  intVarField_.emplace_back(position, *v, "  Resonance:   %1.1X", 0, 0xF, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  switch (instrument->GetChip()) {
  case SID1:
    v = instrument->FindVariable(FourCC::SIDInstrument1FilterMode);
    break;
  case SID2:
    v = instrument->FindVariable(FourCC::SIDInstrument2FilterMode);
    break;
  }
  intVarField_.emplace_back(position, *v, "  Mode:        %s", 0, DFM_LAST - 1,
                            1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 2;
  switch (instrument->GetChip()) {
  case SID1:
    v = instrument->FindVariable(FourCC::SIDInstrument1Volume);
    break;
  case SID2:
    v = instrument->FindVariable(FourCC::SIDInstrument2Volume);
    break;
  }
  intVarField_.emplace_back(position, *v, "Volume:        %1.1X", 0, 0xF, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
};

void InstrumentView::fillMidiParameters() {

  int i = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  I_Instrument *instr = bank->GetInstrument(i);
  MidiInstrument *instrument = (MidiInstrument *)instr;
  GUIPoint position = GetAnchor();

  // offset y to account for instrument type, name and export/import fields
  position._y += 2;

  Variable *v = instrument->FindVariable(FourCC::MidiInstrumentChannel);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "channel: %2.2d", 0, 0x0F, 1, 0x04, 1));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentVolume);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "volume: %2.2X", 0, 0xFF, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentNoteLength);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "length: %2.2X", 0, 0xFF, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentProgram);
  intVarOffField_.emplace_back(
      UIIntVarOffField(position, *v, "program: %2.2X", 0, 0x7F, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &(*intVarOffField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentTableAutomation);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "automation: %s", 0, 1, 1, 1));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::MidiInstrumentTable);
  intVarOffField_.emplace_back(
      UIIntVarOffField(position, *v, "table: %2.2X", 0, 0x7F, 1, 0x10));
  fieldList_.insert(fieldList_.end(), &(*intVarOffField_.rbegin()));
};

void InstrumentView::fillOpalParameters() {
  int i = viewData_->currentInstrumentID_;
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  I_Instrument *instr = bank->GetInstrument(i);
  OpalInstrument *instrument = (OpalInstrument *)instr;
  GUIPoint position = GetAnchor();

  // extra y spacing to allow for gap between export/import and parameters
  position._y += 2;
  staticField_.emplace_back(position, "General Settings" char_line_8_s);
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  position._y += 2;
  Variable *v = instrument->FindVariable(FourCC::OPALInstrumentAlgorithm);
  intVarField_.emplace_back(position, *v, "Algorithm:     %s", 0, 1, 1, 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::OPALInstrumentDeepTremeloVibrato);
  bitmaskVarField_.emplace_back(
      UIBitmaskVarField(position, *v, "Deep Trem/Vib: %02b", 2));
  fieldList_.insert(fieldList_.end(), &(*bitmaskVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::OPALInstrumentFeedback);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "Feedback:      %1.1X", 0, 0x07, 1, 1, 0));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 2;
  staticField_.emplace_back(position, "Operator Settings" char_line_7_s);
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  // operator settings
  position._y += 2;
  staticField_.emplace_back(
      position, "               Op 1" char_border_single_vertical_s "Op 2");
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  position._y += 1;
  staticField_.emplace_back(
      position,
      "               " char_line_4_s char_border_single_cross_s char_line_4_s);
  fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));

  // vertical table separator
  GUIPoint p = position + GUIPoint(19, 1);
  for (int n = 0; n < 6; n++) {
    staticField_.emplace_back(p, char_border_single_vertical_s);
    fieldList_.insert(fieldList_.end(), &(*staticField_.rbegin()));
    p._y += 1;
  }

  position._y += 1;
  v = instrument->FindVariable(FourCC::OPALInstrumentOp1Level);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "Level:         %2.2X", 0, 63, 1, 1, 0));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  v = instrument->FindVariable(FourCC::OPALInstrumentOp2Level);
  intVarField_.emplace_back(
      UIIntVarField(position + GUIPoint(20, 0), *v, "%2.2X", 0, 63, 1, 1, 0));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::OPALInstrumentOp1Multiplier);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "Multiplier:    %1.1X", 0, 15, 1, 1, 0));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  v = instrument->FindVariable(FourCC::OPALInstrumentOp2Multiplier);
  intVarField_.emplace_back(
      UIIntVarField(position + GUIPoint(20, 0), *v, "%1.1X", 0, 15, 1, 1, 0));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::OPALInstrumentOp1ADSR);
  bigHexVarField_.emplace_back(UIBigHexVarField(
      position, *v, 4, "A/D/S/R:       %4.4X", 0, 0xFFFF, 16, true));
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  v = instrument->FindVariable(FourCC::OPALInstrumentOp2ADSR);
  bigHexVarField_.emplace_back(UIBigHexVarField(
      position + GUIPoint(20, 0), *v, 4, "%4.4X", 0, 0xFFFF, 16, true));
  fieldList_.insert(fieldList_.end(), &(*bigHexVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::OPALInstrumentOp1WaveShape);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "Shape:         %s", 0, 7, 1, 1));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  v = instrument->FindVariable(FourCC::OPALInstrumentOp2WaveShape);
  intVarField_.emplace_back(
      UIIntVarField(position + GUIPoint(20, 0), *v, "%s", 0, 7, 1, 1));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::OPALInstrumentOp1TremVibSusKSR);
  bitmaskVarField_.emplace_back(
      UIBitmaskVarField(position, *v, "TR/VB/SU/KSR:  %04b", 4));
  fieldList_.insert(fieldList_.end(), &(*bitmaskVarField_.rbegin()));

  v = instrument->FindVariable(FourCC::OPALInstrumentOp2TremVibSusKSR);
  bitmaskVarField_.emplace_back(
      UIBitmaskVarField(position + GUIPoint(20, 0), *v, "%04b", 4));
  fieldList_.insert(fieldList_.end(), &(*bitmaskVarField_.rbegin()));

  position._y += 1;
  v = instrument->FindVariable(FourCC::OPALInstrumentOp1KeyScaleLevel);
  intVarField_.emplace_back(
      UIIntVarField(position, *v, "Keyscale:      %s", 0, 3, 1, 1));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  v = instrument->FindVariable(FourCC::OPALInstrumentOp2KeyScaleLevel);
  intVarField_.emplace_back(
      UIIntVarField(position + GUIPoint(20, 0), *v, "%s", 0, 3, 1, 1));
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));

  Trace::Error("OPAL fill done, total fields: %d", fieldList_.size());
};

void InstrumentView::warpToNext(int offset) {
  int instrument = viewData_->currentInstrumentID_ + offset;
  if (instrument >= MAX_INSTRUMENT_COUNT) {
    instrument = instrument - MAX_INSTRUMENT_COUNT;
  };
  if (instrument < 0) {
    instrument = MAX_INSTRUMENT_COUNT + instrument;
  };
  viewData_->currentInstrumentID_ = instrument;
  onInstrumentChange();
  isDirty_ = true;
};

void InstrumentView::ProcessButtonMask(unsigned short mask, bool pressed) {

  if (!pressed)
    return;

  isDirty_ = false;
  UIField *focus = GetFocus();
  FourCC focusID = getFieldID(focus);
  UIIntVarField *focusedIntField =
      (focus != nullptr && focusID != FourCC::InstrumentName &&
       focusID != FourCC::ActionImport && focusID != FourCC::ActionExport &&
       focusID != FourCC::ActionShowSampleSlices)
          ? static_cast<UIIntVarField *>(focus)
          : nullptr;

  if ((mask & EPBM_EDIT) && (mask & EPBM_ENTER)) {
    int i = viewData_->currentInstrumentID_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    I_Instrument *instr = bank->GetInstrument(i);
    if (GetFocus() == *fieldList_.begin()) {
      bool instrumentModified = checkInstrumentModified();
      if (instrumentModified) {
        MessageBox *mb = MessageBox::Create(*this, "Reset all settings?",
                                            MBBF_YES | MBBF_NO);
        pendingPurgeInstrument_ = instr;
        DoModal(mb,
                ModalViewCallback::create<
                    InstrumentView, &InstrumentView::onConfirmResetInstrument>(
                    *this));
      }
      return;
    }
    if (getInstrument()->GetType() == IT_SAMPLE) {
      if (focusedIntField &&
          focusedIntField->GetVariableID() == FourCC::SampleInstrumentEnd) {
        Variable &var = focusedIntField->GetVariable();
        SampleInstrument *instrument = (SampleInstrument *)instr;
        var.SetInt(instrument->GetSampleSize() - 1);
        isDirty_ = true;
        return;
      };
    }
  }

  // Call the parent class's implementation first to ensure action fields like
  // Export, Import work correctly
  FieldView::ProcessButtonMask(mask, pressed);

  Player *player = Player::GetInstance();

  if (mask == EPBM_ENTER) {
    // Get the current field to check if we're on the sample field
    // Only allow sample import when the sample field is selected
    if (getInstrument()->GetType() == IT_SAMPLE && focusedIntField &&
        focusedIntField->GetVariableID() == FourCC::SampleInstrumentSample) {

      if (viewMode_ == VM_NEW) {
        viewMode_ = VM_NORMAL; // clear the "enter double tap" state
        if (!player->IsRunning()) {
          // First check if the samplelib exists
          bool samplelibExists =
              FileSystem::GetInstance()->exists(SAMPLES_LIB_DIR);

          if (!samplelibExists) {
            MessageBox *mb = MessageBox::Create(
                *this, "Can't access the samplelib", MBBF_OK);
            DoModal(mb);
          } else {
            ImportView::SetSourceViewType(VT_INSTRUMENT);
            // set browser into sample import mode in top level samples dir
            viewData_->importViewStartDir = SAMPLES_LIB_DIR;

            // Go to import sample
            ViewType vt = VT_IMPORT;
            ViewEvent ve(VET_SWITCH_VIEW, &vt);
            SetChanged();
            NotifyObservers(&ve);
          }
        } else {
          MessageBox *mb =
              MessageBox::Create(*this, "Not while playing", MBBF_OK);
          DoModal(mb);
        }
      } else {
        // mark as "new" mode so a 2nd following ENTER will trigger the sample
        // import above
        viewMode_ = VM_NEW;
      }
    } else if (viewMode_ == VM_NEW) {
      // If we're not on the sample field but in VM_NEW mode, reset it
      viewMode_ = VM_NORMAL;
    }

    if (focusedIntField) {
      Variable &v = focusedIntField->GetVariable();
      switch (v.GetID()) {
      case FourCC::SampleInstrumentTable: {
        int next = TableHolder::GetInstance()->GetNext();
        if (next != NO_MORE_TABLE) {
          v.SetInt(next);
          isDirty_ = true;
        }
        break;
      }
      default:
        break;
      }
    }
    mask &= (0xFFFF - EPBM_ENTER);
  } else {
    // Clear the VM_NEW state if any key other than ENTER is pressed
    if (viewMode_ == VM_NEW) {
      viewMode_ = VM_NORMAL;
    }
  }

  if (viewMode_ == VM_CLONE) {
    if ((mask & EPBM_ENTER) && (mask & EPBM_ALT)) {
      mask &= (0xFFFF - EPBM_ENTER);
      if (focusedIntField) {
        Variable &v = focusedIntField->GetVariable();
        int current = v.GetInt();
        if (current == -1)
          return;

        if ((focusedIntField->GetVariableID() ==
             FourCC::SampleInstrumentTable) ||
            (focusedIntField->GetVariableID() == FourCC::MidiInstrumentTable)) {
          int next = TableHolder::GetInstance()->Clone(current);
          if (next != NO_MORE_TABLE) {
            v.SetInt(next);
            isDirty_ = true;
          }
        };
      }
    }
    mask &= (0xFFFF - (EPBM_ENTER | EPBM_ALT));
  };

  if (viewMode_ == VM_SELECTION) {
  } else {
    // viewMode_ = VM_NORMAL;
  }

  // EDIT Modifier
  if (mask & EPBM_EDIT) {
    if (mask & EPBM_LEFT)
      warpToNext(-1);
    if (mask & EPBM_RIGHT)
      warpToNext(+1);
    if (mask & EPBM_DOWN)
      warpToNext(-16);
    if (mask & EPBM_UP)
      warpToNext(+16);
    if (mask & EPBM_ALT) {
      viewMode_ = VM_CLONE;
    };
    if (mask & EPBM_PLAY) {
      // recording screen
      if (!Player::GetInstance()->IsRunning()) {
        switchToRecordView();
      }
    }
  } else if (mask & EPBM_NAV) {
    // NAV Modifier
    if (mask & EPBM_LEFT) {
      ViewType vt = VT_PHRASE;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);

      // remove listening when leaving this screen
      getInstrument()->RemoveObserver(*this);
      ((WatchedVariable *)&instrumentType_)->RemoveObserver(*this);

      SetChanged();
      NotifyObservers(&ve);
    }

    if (mask & EPBM_DOWN) {

      // Go to table view

      ViewType vt = VT_TABLE2;

      int i = viewData_->currentInstrumentID_;
      InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
      I_Instrument *instr = bank->GetInstrument(i);
      int table = instr->GetTable();
      if (table != VAR_OFF) {
        viewData_->currentTable_ = table;
      }
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }

#if !defined(NODE)
    if (mask & EPBM_PLAY) {
      player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                            viewData_->chainRow_);
    }
#endif

  } else {
    // No modifier
    if (mask & EPBM_PLAY) {
#if defined(NODE)
      player->OnStartButton(PM_PHRASE, viewData_->songX_,
                            (mask & EPBM_ALT) != 0,
                            viewData_->chainRow_);
#else
      player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                            viewData_->chainRow_);
#endif
    }
  }

  lastFocusID_ = getFieldID(GetFocus());
};

void InstrumentView::DrawView() {
  Clear();

  GUITextProperties props;
  GUIPoint pos = GetTitlePosition();
  
  // Draw title

  char title[20];
  SetColor(CD_NORMAL);
  npf_snprintf(title, sizeof(title), "Instrument %2.2X",
               viewData_->currentInstrumentID_);
  DrawString(pos._x, pos._y, title, props);
  // Draw fields
  FieldView::Redraw();
  drawMap();

  // Draw instrument type with special handling for SID and OPAL
  if (ShouldShowExperimentalBanner()) {
    SetColor(CD_WARN);
    DrawString(16, 1, char_button_border_left_s, props);
    DrawString(17, 1, "EXPERIMENTAL", GUITextProperties(true));
    DrawString(29, 1, char_button_border_right_s, props);
    SetColor(CD_NORMAL);
  }
}

void InstrumentView::OnFocus() {
  Trace::Log("INSTRUMENTVIEW", "onFocus");

  // Get latest selected instrument, ensures we display the instrument that was
  // selected in the PhraseView
  int currentID = viewData_->currentInstrumentID_;
  Trace::Debug("INSTRUMENTVIEW", "Current instrument ID from ViewData: %d",
               currentID);

  // Get the current instrument based on the ViewData's currentInstrumentID_
  InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
  I_Instrument *instr = bank->GetInstrument(currentID);

  if (instr) {
    // Update the instrument type field to match the current instrument
    InstrumentType currentType = instr->GetType();

    Trace::Debug("INSTRUMENTVIEW", "Current instrument type: %d", currentType);

    // Only update if the type has changed
    if (instrumentType_.GetInt() != currentType) {
      Trace::Log("INSTRUMENTVIEW",
                 "OnFocus instrument type changed from %d to %d",
                 instrumentType_.GetInt(), currentType);
      // Set the instrument type without triggering the observer update
      // because we dont want the observer to do its normal check for a modified
      // instrument
      instrumentType_.SetInt(currentType, false);
    }

    // Always refresh the UI fields when focusing the view in case of instrument
    // change from last time
    onInstrumentTypeChange(true);
  }
}

void InstrumentView::Update(Observable &o, I_ObservableData *data) {

  if (!hasFocus_) {
    return;
  }

  uintptr_t fourcc = (uintptr_t)data;

  switch (fourcc) {
  case FourCC::VarInstrumentType: {
    // Get the current instrument to determine its actual type
    I_Instrument *instr = getInstrument();
    InstrumentType currentType = instr ? instr->GetType() : IT_NONE;

    // Store the proposed instrument type BEFORE we revert the UI
    InstrumentType proposedType = (InstrumentType)instrumentType_.GetInt();

    // Revert the UI field back to the current type until confirmed
    instrumentType_.SetInt(currentType, false);

    // Check if player is running
    Player *player = Player::GetInstance();
    if (!player->IsRunning()) {
      // Check if any instrument field has been modified
      bool instrumentModified = checkInstrumentModified();
      if (instrumentModified) {
        MessageBox *mb = MessageBox::Create(
            *this, "Change Instrument &", "lose settings?", MBBF_YES | MBBF_NO);
        pendingInstrumentType_ = proposedType;
        DoModal(
            mb,
            ModalViewCallback::create<
                InstrumentView, &InstrumentView::onConfirmInstrumentTypeChange>(
                *this));
      } else {
        // Apply the proposed type change immediately if not modified
        instrumentType_.SetInt(proposedType, false);
        onInstrumentTypeChange();
      }
    } else {
      MessageBox *mb = MessageBox::Create(*this, "Not while playing", MBBF_OK);
      DoModal(mb);
    }
    break;
  }
  case FourCC::ActionExport: {
    handleInstrumentExport();
  } break;
  case FourCC::ActionImport: {
    // Switch to the InstrumentImportView
    ViewType vt = VT_INSTRUMENT_IMPORT;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
  } break;
  case FourCC::SampleInstrumentSample: {
    I_Instrument *instr = getInstrument();
    if (!instr || instr->GetType() != IT_SAMPLE) {
      break;
    }

    SampleInstrument *sampleInstr = static_cast<SampleInstrument *>(instr);
    int newIndex = sampleInstr->GetSampleIndex();

    if (suppressSampleChangeWarning_) {
      suppressSampleChangeWarning_ = false;
      lastSampleIndex_ = newIndex;
      break;
    }

    if (newIndex == lastSampleIndex_) {
      break;
    }

    if (!sampleInstr->HasSlicesForWarning()) {
      sampleInstr->ClearSlices();
      lastSampleIndex_ = newIndex;
      updateSliceCountLabel(sliceCountLabel_, sampleInstr);
      isDirty_ = true;
      break;
    }

    MessageBox *mb = MessageBox::Create(*this, "Change sample &",
                                        "clear slices?", MBBF_YES | MBBF_NO);
    pendingSampleChangeInstrument_ = sampleInstr;
    pendingSampleChangeNewIndex_ = newIndex;
    DoModal(mb,
            ModalViewCallback::create<InstrumentView,
                                      &InstrumentView::onConfirmSampleChange>(
                *this));
  } break;
  case FourCC::ActionShowSampleSlices: {
    I_Instrument *instr = getInstrument();
    if (!instr || instr->GetType() != IT_SAMPLE) {
      break;
    }
    SampleInstrument *sampleInstr = static_cast<SampleInstrument *>(instr);
    if (sampleInstr->GetSampleIndex() < 0) {
      MessageBox *mb =
          MessageBox::Create(*this, "Assign a sample first", MBBF_OK);
      DoModal(mb);
      break;
    }
    ViewType vt = VT_SAMPLE_SLICES;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
  } break;
  case FourCC::MidiInstrumentProgram: {
    // When program value changes, send a MIDI Program Change message during
    // playback
    if (!Player::GetInstance()->IsRunning()) {
      break;
    }

    I_Instrument *instr = getInstrument();
    if (instr && instr->GetType() == IT_MIDI) {
      MidiInstrument *midiInstr = (MidiInstrument *)instr;

      // Get the channel and program values
      Variable *channelVar =
          midiInstr->FindVariable(FourCC::MidiInstrumentChannel);
      Variable *programVar =
          midiInstr->FindVariable(FourCC::MidiInstrumentProgram);

      if (channelVar && programVar) {
        int channel = channelVar->GetInt();
        int program = programVar->GetInt();

        // Send Program Change message
        midiInstr->SendProgramChange(channel, program);
      }
    }
  } break;
  default:
    break;
  }
}

bool InstrumentView::checkInstrumentModified() {
  // Get current instrument
  I_Instrument *instrument = getInstrument();
  if (!instrument) {
    return false;
  }

  // Get the list of variables for this instrument
  etl::ilist<Variable *> *variables = instrument->Variables();
  if (!variables) {
    return false;
  }

  // Check if any variable has been modified from its default value
  for (auto it = variables->begin(); it != variables->end(); ++it) {
    Variable *var = *it;
    if (var && var->IsModified()) {
      return true;
    }
  }

  // No variables have been modified
  return false;
}

void InstrumentView::resetInstrumentToDefaults() {
  // Get current instrument
  I_Instrument *instrument = getInstrument();
  if (!instrument) {
    return;
  }

  // Get the list of variables for this instrument
  etl::ilist<Variable *> *variables = instrument->Variables();
  if (!variables) {
    return;
  }

  // Reset all variables to their default values
  for (auto it = variables->begin(); it != variables->end(); ++it) {
    Variable *var = *it;
    if (var) {
      var->Reset();
    }
  }
}

void InstrumentView::handleInstrumentExport() {
  // Get current instrument using its id
  I_Instrument *instrument =
      viewData_->project_->GetInstrumentBank()->GetInstrument(
          viewData_->currentInstrumentID_);

  // Check if the instrument has a name set
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> name = instrument->GetDisplayName();
  // Check if the name is empty, the default value, or matches the default
  // instrument type name
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> defaultTypeName =
      instrument->GetDefaultName();

  if (name.empty() || name == defaultTypeName) {
    // Show error message if no name is set
    MessageBox *mb = MessageBox::Create(*this, "Please set a name",
                                        "before exporting", MBBF_OK);
    DoModal(mb);
  } else {
    // Export the instrument using the name field
    PersistencyResult result =
        PersistencyService::GetInstance()->ExportInstrument(instrument, name);

    if (result == PERSIST_EXISTS) {
      // File already exists, ask user if they want to override it
      etl::string<strlen("Overwrite existing file: ")> confirmMsg =
          "Overwrite existing file?";
      MessageBox *mb = MessageBox::Create(*this, confirmMsg.c_str(),
                                          name.c_str(), MBBF_YES | MBBF_NO);

      exportInstrument_ = instrument;
      exportName_ = name;
      DoModal(
          mb,
          ModalViewCallback::create<InstrumentView,
                                    &InstrumentView::onConfirmExportOverwrite>(
              *this));
    } else {
      // Create a message with the instrument name
      etl::string<MAX_INSTRUMENT_NAME_LENGTH + strlen("Exported: ")>
          successMsg = "Exported: ";
      successMsg += name;

      const char *message = result == PERSIST_SAVED
                                ? successMsg.c_str()
                                : "Failed to export instrument";
      // Show export result message
      MessageBox *mb = MessageBox::Create(*this, message, MBBF_OK);
      DoModal(mb);
    }
  }
}

void InstrumentView::onConfirmInstrumentTypeChange(View &, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    applyProposedTypeChangeUI();
  }
}

void InstrumentView::onConfirmResetInstrument(View &, ModalView &dialog) {
  I_Instrument *instr = pendingPurgeInstrument_;
  pendingPurgeInstrument_ = nullptr;

  if (dialog.GetReturnCode() != MBL_YES || !instr) {
    return;
  }

  instr->Purge();
  isDirty_ = true;
}

void InstrumentView::onConfirmSampleChange(View &, ModalView &dialog) {
  SampleInstrument *sampleInstr = pendingSampleChangeInstrument_;
  int newIndex = pendingSampleChangeNewIndex_;
  pendingSampleChangeInstrument_ = nullptr;
  pendingSampleChangeNewIndex_ = -1;

  if (!sampleInstr) {
    return;
  }

  if (dialog.GetReturnCode() == MBL_YES) {
    sampleInstr->ClearSlices();
    lastSampleIndex_ = newIndex;
    updateSliceCountLabel(sliceCountLabel_, sampleInstr);
    isDirty_ = true;
    return;
  }

  suppressSampleChangeWarning_ = true;
  if (Variable *sampleVar =
          sampleInstr->FindVariable(FourCC::SampleInstrumentSample)) {
    sampleVar->SetInt(lastSampleIndex_);
  }
  isDirty_ = true;
}

void InstrumentView::onConfirmExportOverwrite(View &, ModalView &dialog) {
  I_Instrument *instrument = exportInstrument_;
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> name = exportName_;
  exportInstrument_ = nullptr;
  exportName_.clear();

  if (dialog.GetReturnCode() != MBL_YES || !instrument) {
    return;
  }

  PersistencyService::GetInstance()->ExportInstrument(instrument, name, true);
  Trace::Log("INSTRUMENTVIEW", "Instrument '%s' exported with overwrite",
             name.c_str());
}
