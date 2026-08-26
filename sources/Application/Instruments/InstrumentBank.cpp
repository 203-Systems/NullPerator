/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "InstrumentBank.h"
#include "Application/Instruments/InstrumentBankRestorePolicy.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Config.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Utils/char.h"
#include "Filters.h"
#include "MidiInstrument.h"
#include "OpalInstrument.h"
#include "SIDInstrument.h"
#include "System/io/Status.h"

#define XML_DEBUG_LOGGING 0

// Contain all instrument definition
InstrumentBank::InstrumentBank()
    : Persistent("INSTRUMENTBANK"), sampleInstrumentPool_(),
      midiInstrumentPool_(), sidInstrumentPool_(), opalInstrumentPool_() {

  for (size_t i = 0; i < instruments_.max_size(); i++) {
    instruments_[i] = &none_;
  }

  Status::Set("All instruments preloaded");
};

InstrumentBank::~InstrumentBank() { Reset(); };

InstrumentBank::Replacement::~Replacement() { Cancel(); }

bool InstrumentBank::Replacement::Commit() {
  return bank_ != nullptr && bank_->commitReplacement(*this);
}

void InstrumentBank::Replacement::Cancel() {
  if (bank_ != nullptr)
    bank_->cancelReplacement(*this);
}

void InstrumentBank::Reset() {
  sampleInstrumentPool_.release_all();
  midiInstrumentPool_.release_all();
  sidInstrumentPool_.release_all();
  opalInstrumentPool_.release_all();

  for (size_t i = 0; i < instruments_.max_size(); i++) {
    instruments_[i] = &none_;
  }
  sidOscCount = 0;
};

I_Instrument *InstrumentBank::GetInstrument(int i) { return instruments_[i]; };

void InstrumentBank::SaveContent(tinyxml2::XMLPrinter *printer) {
  char hex[3];
  int i = 0;
  for (auto &instr : instruments_) {
    if (!instr->IsEmpty()) {
      hex2char(i, hex);
      printer->OpenElement("INSTRUMENT");
      printer->PushAttribute("ID", hex);

      // Let the instrument save its own content
      instr->SaveContent(printer);

      printer->CloseElement(); // INSTRUMENT
    }
    i++;
  }
};

void InstrumentBank::RestoreContent(PersistencyDocument *doc) {
  if (doc == nullptr || doc->HadError()) {
    if (doc != nullptr)
      doc->MarkError();
    return;
  }

  InstrumentBankRestorePolicy policy;
  std::array<Replacement, MAX_INSTRUMENT_COUNT> staged;
  bool elem = doc->FirstChild();
  while (elem) {
    if (strcasecmp(doc->ElemName(), "INSTRUMENT") != 0) {
      doc->MarkError();
      return;
    }

    std::uint8_t id = 0U;
    InstrumentType instrumentType = IT_SAMPLE; // Legacy files omitted TYPE.
    bool hasId = false;
    bool hasType = false;
    bool hasAttr = doc->NextAttribute();
    while (hasAttr) {
      if (!strcasecmp(doc->attrname_, "ID")) {
        if (hasId || !DecodeInstrumentBankSlotId(doc->attrval_, id)) {
          doc->MarkError();
          return;
        }
        hasId = true;
#if XML_DEBUG_LOGGING
        Trace::Log("INSTRUMENTBANK", "instrument ID from xml:%d", id);
#endif
      } else if (!strcasecmp(doc->attrname_, "TYPE")) {
        if (hasType ||
            !DecodeInstrumentBankType(doc->attrval_, instrumentType)) {
          doc->MarkError();
          return;
        }
        hasType = true;
#if XML_DEBUG_LOGGING
        Trace::Log("INSTRUMENTBANK", "instrument type from xml:%s",
                   doc->attrval_);
#endif
      }
      // Leave instrument-specific root attributes (legacy sample SLxx fields)
      // for the concrete instrument restore. That restore also rejects any
      // later ID/TYPE token as a duplicate of the envelope consumed here.
      if (hasId && hasType)
        break;
      hasAttr = doc->NextAttribute();
    }

    if (doc->HadError() || !hasId ||
        !policy.Reserve(id, instrumentType) ||
        !BeginReplacement(id, instrumentType, staged[id])) {
      Trace::Error("Invalid or exhausted instrument bank entry");
      doc->MarkError();
      return;
    }

    I_Instrument *instrument = staged[id].Candidate();
    instrument->RestoreContent(doc);
    if (doc->HadError())
      return;
    elem = doc->NextSibling();
  }
  if (doc->HadError())
    return;

  // Every instrument parsed and validated successfully. Publish candidates
  // only now, so a late duplicate, truncation, or pool failure cannot mutate
  // the current bank before the enclosing project transaction accepts it.
  for (std::uint8_t id = 0U; id < MAX_INSTRUMENT_COUNT; ++id) {
    if (policy.Seen(id) && !staged[id].Commit()) {
      doc->MarkError();
      return;
    }
  }
};

void InstrumentBank::Init() {}

// Get the next available instance of the given Instrument type from the pool of
// unused Instruments and assign it to the given instrument "slot id"
unsigned short InstrumentBank::GetNextAndAssignID(InstrumentType type,
                                                  uint8_t id) {
  if (id >= instruments_.size())
    return NO_MORE_INSTRUMENT;

  I_Instrument *instrument = createInstrument(type);
  if (instrument == nullptr)
    return NO_MORE_INSTRUMENT;
  instruments_[id] = instrument;
  return id;
};

I_Instrument *InstrumentBank::createInstrument(InstrumentType type) {
  switch (type) {
  case IT_SAMPLE: {
    SampleInstrument *si = sampleInstrumentPool_.create();
    if (si == nullptr) {
      Trace::Log("INSTRUMENTBANK", "Sample INSTRUMENT EXHAUSTED!");
      return nullptr;
    }
    si->Init();

    Variable *sample = si->FindVariable(FourCC::SampleInstrumentSample);
    if (sample == nullptr || sample->GetInt() != -1) {
      Trace::Log("INSTRUMENTBANK",
                 "unexpected sample value for new instrument: %d",
                 sample == nullptr ? 0 : sample->GetInt());
      sampleInstrumentPool_.destroy(si);
      return nullptr;
    }
    return si;
  } break;
  case IT_MIDI: {
    MidiInstrument *mi = midiInstrumentPool_.create();
    if (mi == nullptr) {
      Trace::Error("MIDI INSTRUMENT EXHAUSTED!");
      return nullptr;
    }
    mi->Init();
    return mi;
  } break;
  case IT_SID: {
    // TODO need to figure out how to properly manage sid oc count
    SIDInstrument *si = sidInstrumentPool_.create(SID1);
    if (si == nullptr) {
      Trace::Error("SID INSTRUMENT EXHAUSTED!");
      return nullptr;
    }
    si->Init();
    return si;
  } break;
  case IT_OPAL: {
    OpalInstrument *oi = opalInstrumentPool_.create();
    if (oi == nullptr) {
      Trace::Error("Opal INSTRUMENT EXHAUSTED!");
      return nullptr;
    }
    oi->Init();
    return oi;
  } break;
  case IT_NONE:
    return &none_;
  default:
    break;
  }

  return nullptr;
};

bool InstrumentBank::BeginReplacement(unsigned short id, InstrumentType type,
                                      Replacement &replacement) {
  replacement.Cancel();
  if (id >= instruments_.size() || type < IT_NONE || type >= IT_LAST)
    return false;

  I_Instrument *candidate = createInstrument(type);
  if (candidate == nullptr)
    return false;

  replacement.bank_ = this;
  replacement.candidate_ = candidate;
  replacement.original_ = instruments_[id];
  replacement.slot_ = id;
  return true;
}

bool InstrumentBank::commitReplacement(Replacement &replacement) {
  if (replacement.bank_ != this || replacement.candidate_ == nullptr ||
      replacement.slot_ >= instruments_.size() ||
      instruments_[replacement.slot_] != replacement.original_)
    return false;

  I_Instrument *original = replacement.original_;
  instruments_[replacement.slot_] = replacement.candidate_;
  replacement.bank_ = nullptr;
  replacement.candidate_ = nullptr;
  replacement.original_ = nullptr;
  replacement.slot_ = NO_MORE_INSTRUMENT;
  destroyInstrument(original);
  return true;
}

void InstrumentBank::cancelReplacement(Replacement &replacement) {
  if (replacement.bank_ != this)
    return;
  destroyInstrument(replacement.candidate_);
  replacement.bank_ = nullptr;
  replacement.candidate_ = nullptr;
  replacement.original_ = nullptr;
  replacement.slot_ = NO_MORE_INSTRUMENT;
}

void InstrumentBank::destroyInstrument(I_Instrument *instrument) {
  if (instrument == nullptr || instrument == &none_)
    return;

  switch (instrument->GetType()) {
  case IT_SAMPLE:
    sampleInstrumentPool_.destroy(instrument);
    break;
  case IT_MIDI:
    midiInstrumentPool_.destroy(instrument);
    break;
  case IT_SID:
    sidInstrumentPool_.destroy(instrument);
    break;
  case IT_OPAL:
    opalInstrumentPool_.destroy(instrument);
    break;
  case IT_NONE:
  default:
    break;
  }
}

void InstrumentBank::releaseInstrument(unsigned short id) {
  auto instrument = instruments_[id];
  destroyInstrument(instrument);
  instruments_[id] = &none_;
}

unsigned short InstrumentBank::GetNextFreeInstrumentSlotId() {
  for (unsigned short i = 0; i < instruments_.max_size(); i++) {
    if (instruments_[i] == &none_) {
      return i;
    }
  }
  return NO_MORE_INSTRUMENT;
}

unsigned short InstrumentBank::Clone(unsigned short i) {
  I_Instrument *src = instruments_[i];

  // Find next available instrument slot
  auto nextFreeInstrumentSlotId = GetNextFreeInstrumentSlotId();

  unsigned short next =
      GetNextAndAssignID(src->GetType(), nextFreeInstrumentSlotId);

  if (next == NO_MORE_INSTRUMENT) {
    return NO_MORE_INSTRUMENT;
  }

  I_Instrument *dst = instruments_[next];

  // sanity check not trying to clone into itself
  if (src == dst) {
    return NO_MORE_INSTRUMENT;
  }

  for (auto it = src->Variables()->begin(); it != src->Variables()->end();
       it++) {
    Variable *dstV = dst->FindVariable((*it)->GetID());
    if (dstV) {
      dstV->CopyFrom(**it);
    }
  }
  return next;
}

void InstrumentBank::OnStart() {
  for (auto &elem : instruments_) {
    elem->OnStart();
  }
  init_filters();
};
