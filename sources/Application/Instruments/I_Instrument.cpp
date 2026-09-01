/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "I_Instrument.h"
#include "Application/Model/ProjectVersion.h"
#include "Application/Model/Table.h"
#include "Application/Persistency/PersistencyAttribute.h"
#include "System/Console/Trace.h"
#include "ProductVersion.h"

#include <array>
#include <cstring>

namespace {

bool GenericIntegerRange(FourCC id, int &minimum, int &maximum) {
  minimum = 0;
  maximum = 0;
  if (id == FourCC::MidiInstrumentChannel) {
    maximum = 0x0F;
  } else if (id == FourCC::MidiInstrumentNoteLength ||
             id == FourCC::MidiInstrumentVolume) {
    maximum = 0xFF;
  } else if (id == FourCC::MidiInstrumentProgram) {
    minimum = VAR_OFF;
    maximum = 0x7F;
  } else if (id == FourCC::MidiInstrumentTable ||
             id == FourCC::SIDInstrumentTable) {
    minimum = VAR_OFF;
    maximum = TABLE_COUNT - 1;
  } else if (id == FourCC::SIDInstrumentOSCNumber) {
    maximum = 2;
  } else if (id == FourCC::SIDInstrumentPulseWidth) {
    maximum = 0xFFF;
  } else if (id == FourCC::SIDInstrumentADSR ||
             id == FourCC::OPALInstrumentOp1ADSR ||
             id == FourCC::OPALInstrumentOp2ADSR) {
    maximum = 0xFFFF;
  } else if (id == FourCC::SIDInstrument1FilterCut ||
             id == FourCC::SIDInstrument2FilterCut) {
    maximum = 0x7FF;
  } else if (id == FourCC::SIDInstrument1FilterResonance ||
             id == FourCC::SIDInstrument2FilterResonance ||
             id == FourCC::SIDInstrument1Volume ||
             id == FourCC::SIDInstrument2Volume ||
             id == FourCC::OPALInstrumentOp1Multiplier ||
             id == FourCC::OPALInstrumentOp2Multiplier ||
             id == FourCC::OPALInstrumentOp1TremVibSusKSR ||
             id == FourCC::OPALInstrumentOp2TremVibSusKSR) {
    maximum = 0x0F;
  } else if (id == FourCC::OPALInstrumentFeedback) {
    maximum = 7;
  } else if (id == FourCC::OPALInstrumentDeepTremeloVibrato) {
    maximum = 3;
  } else if (id == FourCC::OPALInstrumentOp1Level ||
             id == FourCC::OPALInstrumentOp2Level) {
    maximum = 63;
  } else {
    return false;
  }
  return true;
}

bool ValidateGenericInstrumentVariable(Variable &variable,
                                       const char *value) {
  if (value == nullptr)
    return false;
  switch (variable.GetType()) {
  case Variable::INT: {
    int minimum = 0;
    int maximum = 0;
    int parsed = 0;
    return GenericIntegerRange(variable.GetID(), minimum, maximum) &&
           ParsePersistedIntegerAttribute(value, minimum, maximum, parsed);
  }
  case Variable::BOOL:
    // Keep persistence canonical and in lockstep with Variable::SetString(),
    // whose false token is deliberately the exact lower-case string.
    return std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "false") == 0;
  case Variable::CHAR_LIST:
    for (std::uint8_t index = 0U; index < variable.GetListSize(); ++index) {
      const char *option = variable.GetListPointer()[index];
      if (option != nullptr && strcasecmp(option, value) == 0)
        return true;
    }
    return false;
  case Variable::STRING:
    return std::strlen(value) <= MAX_VARIABLE_STRING_LENGTH;
  case Variable::FLOAT:
    // No current MIDI/SID/OPAL instrument persists floating-point values.
    return false;
  }
  return false;
}

} // namespace

I_Instrument::~I_Instrument() {
  // Virtual destructor implementation
}

void I_Instrument::SaveContent(tinyxml2::XMLPrinter *printer) {
  printer->PushAttribute("FORMAT", nullperator_project::Format);
  printer->PushAttribute("SCHEMA", nullperator_project::Schema);
  printer->PushAttribute("CREATED_WITH", nullperator_product::Version);
  // Save the instrument type
  printer->PushAttribute("TYPE", InstrumentTypeNames[GetType()]);

  // Save the instrument name as its not stored in the Variables
  if (!name_.empty()) {
    printer->OpenElement("PARAM");
    printer->PushAttribute("NAME", "InstrumentName");
    printer->PushAttribute("VALUE", name_.c_str());
    printer->CloseElement(); // PARAM
  }

  // Save all the instrument's parameters
  for (auto it = Variables()->begin(); it != Variables()->end(); it++) {
    printer->OpenElement("PARAM");
    printer->PushAttribute("NAME", (*it)->GetName());
    printer->PushAttribute("VALUE", (*it)->GetString().c_str());
    printer->CloseElement(); // PARAM
  }
}

void I_Instrument::RestoreContent(PersistencyDocument *doc) {
  if (doc == nullptr || doc->HadError()) {
    if (doc != nullptr)
      doc->MarkError();
    return;
  }

  struct StagedUpdate {
    Variable *target = nullptr;
    std::array<char, MAX_VARIABLE_STRING_LENGTH + 1U> value{};
  };
  // SID currently has the largest generic variable list (17). Keep spare
  // fixed capacity for compatible future parameters without heap allocation.
  std::array<StagedUpdate, 24U> updates{};
  std::size_t updateCount = 0U;
  std::array<char, MAX_INSTRUMENT_NAME_LENGTH + 1U> stagedName{};
  bool hasStagedName = false;
  const auto fail = [doc]() { doc->MarkError(); };

  const bool childAlreadySelected =
      doc->r_ == YXML_ELEMSTART &&
      strcasecmp(doc->ElemName(), "PARAM") == 0;
  if (!childAlreadySelected) {
    // InstrumentBank stops immediately after consuming the project envelope's
    // TYPE attribute so concrete restore can still see legacy root fields.
    // Direct .pti restore starts at ELEMSTART and owns its first TYPE token.
    bool typeSeen = doc->r_ == YXML_ATTREND;
    bool attribute = doc->NextAttribute();
    while (attribute) {
      if (!strcasecmp(doc->attrname_, "TYPE")) {
        if (typeSeen) {
          fail();
          return;
        }
        typeSeen = true;
        Trace::Log("I_INSTRUMENT", "Instrument type from XML: %s",
                   doc->attrval_);
      } else if (!strcasecmp(doc->attrname_, "ID")) {
        fail();
        return;
      }
      attribute = doc->NextAttribute();
    }
    if (doc->HadError())
      return;
  }

  // NextAttribute() stops when it encounters the first child start token. In
  // that state the child is already selected, so calling FirstChild() again
  // would skip the first PARAM entirely (and historically made single-PARAM
  // MIDI/SID/OPAL files appear to load without changing anything).
  bool element = childAlreadySelected || doc->r_ == YXML_ELEMSTART ||
                 doc->FirstChild();
  while (element) {
    if (strcasecmp(doc->ElemName(), "PARAM") != 0) {
      fail();
      return;
    }
    std::array<char, MAX_VARIABLE_STRING_LENGTH + 1U> name{};
    std::array<char, MAX_VARIABLE_STRING_LENGTH + 1U> value{};
    bool hasName = false;
    bool hasValue = false;
    bool attribute = doc->NextAttribute();
    while (attribute) {
      if (!strcasecmp(doc->attrname_, "NAME")) {
        if (hasName || !CopyPersistedVariableAttribute(
                           *doc, name.data(), name.size(), false)) {
          fail();
          return;
        }
        hasName = true;
      } else if (!strcasecmp(doc->attrname_, "VALUE")) {
        if (hasValue || !CopyPersistedVariableAttribute(
                            *doc, value.data(), value.size(), true)) {
          fail();
          return;
        }
        hasValue = true;
      }
      attribute = doc->NextAttribute();
    }
    if (doc->HadError() || doc->r_ != YXML_ELEMEND || !hasName ||
        !hasValue) {
      fail();
      return;
    }

    if (!strcasecmp(name.data(), "InstrumentName")) {
      const std::size_t length = std::strlen(value.data());
      if (hasStagedName || length > MAX_INSTRUMENT_NAME_LENGTH) {
        fail();
        return;
      }
      std::memcpy(stagedName.data(), value.data(), length + 1U);
      hasStagedName = true;
    } else {
      Variable *target = nullptr;
      for (Variable *candidate : *Variables()) {
        if (!strcasecmp(candidate->GetName(), name.data())) {
          target = candidate;
          break;
        }
      }
      if (target != nullptr) {
        if (!ValidateGenericInstrumentVariable(*target, value.data()) ||
            updateCount >= updates.size()) {
          fail();
          return;
        }
        for (std::size_t index = 0U; index < updateCount; ++index) {
          if (updates[index].target == target) {
            fail();
            return;
          }
        }
        updates[updateCount].target = target;
        updates[updateCount].value = value;
        ++updateCount;
      } else {
        // Unknown parameters stay forward-compatible. Known malformed values
        // and duplicate known parameters are rejected above.
        Trace::Error("Parameter '%s' not found in instrument", name.data());
      }
    }
    element = doc->NextSibling();
  }
  if (doc->HadError() || doc->r_ != YXML_ELEMEND) {
    fail();
    return;
  }

  for (std::size_t index = 0U; index < updateCount; ++index)
    updates[index].target->SetString(updates[index].value.data());
  if (hasStagedName)
    SetName(stagedName.data());

  Variable *nameVar = FindVariable(FourCC::InstrumentName);
  if (nameVar != nullptr && !name_.empty())
    nameVar->SetString(name_.c_str());
}

void I_Instrument::Purge() {
  for (auto it = Variables()->begin(); it != Variables()->end(); it++) {
    (*it)->Reset();
  }
};
