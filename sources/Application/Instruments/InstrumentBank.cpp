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
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Utils/char.h"
#include "ChiptuneInstrument.h"
#include "DrumInstrument.h"
#include "Filters.h"
#include "MidiInstrument.h"
#include "OpalInstrument.h"
#include "SIDInstrument.h"
#include "StackInstrument.h"
#include "System/Console/Trace.h"
#include <memory>
#include <utility>

#define XML_DEBUG_LOGGING 0

// Contain all instrument definition
InstrumentBank::InstrumentBank() : InstrumentBank(Allocator{}) {}

InstrumentBank::InstrumentBank(Allocator allocator)
    : Persistent("INSTRUMENTBANK"), allocator_(allocator) {

  for (size_t i = 0; i < instruments_.max_size(); i++) {
    instruments_[i] = &none_;
  }
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
  for (size_t i = 0; i < instruments_.max_size(); i++) {
    destroyInstrument(instruments_[i]);
    instruments_[i] = &none_;
    ++generations_[i];
  }
};

I_Instrument *InstrumentBank::GetInstrument(int i) {
  return i >= 0 && i < MAX_INSTRUMENT_COUNT ? instruments_[i] : &none_;
}

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

    if (doc->HadError() || !hasId || !policy.Reserve(id, instrumentType) ||
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
  // only now, so a late duplicate, truncation, or allocation failure cannot
  // mutate the current bank before the enclosing project transaction accepts
  // it.
  for (std::uint8_t id = 0U; id < MAX_INSTRUMENT_COUNT; ++id) {
    if (policy.Seen(id) && !staged[id].Commit()) {
      doc->MarkError();
      return;
    }
  }
};

void InstrumentBank::Init() {}

// Construct only the requested type and publish it after allocation succeeds.
unsigned short InstrumentBank::GetNextAndAssignID(InstrumentType type,
                                                  uint8_t id) {
  if (id >= instruments_.size())
    return NO_MORE_INSTRUMENT;

  I_Instrument *instrument = createInstrument(type);
  if (instrument == nullptr)
    return NO_MORE_INSTRUMENT;
  I_Instrument *previous = instruments_[id];
  instruments_[id] = instrument;
  ++generations_[id];
  destroyInstrument(previous);
  return id;
};

template <typename T, typename... Args>
I_Instrument *InstrumentBank::allocateInstrument(Args &&...args) {
  if (!allocator_.allocate || !allocator_.release)
    return nullptr;
  void *storage = allocator_.allocate(sizeof(T));
  if (!storage)
    return nullptr;
  T *instrument =
      std::construct_at(static_cast<T *>(storage), std::forward<Args>(args)...);
  // An unbound sample is a valid editable preset. Its legacy Init returns
  // false even after successful setup; that is not an allocation failure.
  const bool initialized = instrument->Init();
  if (!initialized && instrument->GetType() != IT_SAMPLE) {
    std::destroy_at(instrument);
    allocator_.release(storage);
    return nullptr;
  }
  return instrument;
}

I_Instrument *InstrumentBank::createInstrument(InstrumentType type) {
  switch (type) {
  case IT_DRUM:
    return allocateInstrument<DrumInstrument>(&drumVoices_);
  case IT_STACK:
    return allocateInstrument<StackInstrument>(&stackVoices_);
  case IT_CHIPTUNE:
    return allocateInstrument<ChiptuneInstrument>(&chiptuneVoices_);
  case IT_SAMPLE:
    return allocateInstrument<SampleInstrument>();
  case IT_MIDI:
    return allocateInstrument<MidiInstrument>();
  case IT_SID:
    return allocateInstrument<SIDInstrument>(SID1);
  case IT_OPAL:
    return allocateInstrument<OpalInstrument>();
  case IT_NONE:
    return &none_;
  default:
    return nullptr;
  }
}

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
  replacement.generation_ = generations_[id];
  return true;
}

bool InstrumentBank::commitReplacement(Replacement &replacement) {
  if (replacement.bank_ != this || replacement.candidate_ == nullptr ||
      replacement.slot_ >= instruments_.size() ||
      instruments_[replacement.slot_] != replacement.original_ ||
      generations_[replacement.slot_] != replacement.generation_)
    return false;

  I_Instrument *original = replacement.original_;
  instruments_[replacement.slot_] = replacement.candidate_;
  ++generations_[replacement.slot_];
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
  std::destroy_at(instrument);
  allocator_.release(instrument);
}

void InstrumentBank::releaseInstrument(unsigned short id) {
  if (id >= instruments_.size())
    return;
  auto instrument = instruments_[id];
  destroyInstrument(instrument);
  instruments_[id] = &none_;
  ++generations_[id];
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
  if (i >= instruments_.size() || instruments_[i] == &none_)
    return NO_MORE_INSTRUMENT;
  I_Instrument *src = instruments_[i];

  // Find next available instrument slot
  auto nextFreeInstrumentSlotId = GetNextFreeInstrumentSlotId();
  if (nextFreeInstrumentSlotId == NO_MORE_INSTRUMENT)
    return NO_MORE_INSTRUMENT;

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
  dst->SetName(src->GetUserSetName().c_str());
  return next;
}

void InstrumentBank::OnStart() {
  // MIDI setup already has last-preset-wins semantics per protocol channel.
  // Keep that final state and slot ordering, but omit overwritten setup
  // messages so 64 presets still fit the existing realtime queue budget.
  std::array<int, midi_queue_budget::kMidiProtocolChannelCount> lastProgram;
  std::array<int, midi_queue_budget::kMidiProtocolChannelCount> lastVolume;
  lastProgram.fill(-1);
  lastVolume.fill(-1);
  for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot) {
    auto *instrument = instruments_[slot];
    if (instrument->GetType() != IT_MIDI)
      continue;
    const int channel =
        instrument->FindVariable(FourCC::MidiInstrumentChannel)->GetInt();
    if (channel < 0 || channel >= static_cast<int>(lastProgram.size()))
      continue;
    const int program =
        instrument->FindVariable(FourCC::MidiInstrumentProgram)->GetInt();
    if (program >= 0 && program <= 0x7F)
      lastProgram[channel] = slot;
    if (instrument->FindVariable(FourCC::MidiInstrumentVolume)->GetInt() > 0)
      lastVolume[channel] = slot;
  }
  for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot) {
    auto *instrument = instruments_[slot];
    if (instrument->GetType() == IT_MIDI) {
      const int channel =
          instrument->FindVariable(FourCC::MidiInstrumentChannel)->GetInt();
      const bool valid =
          channel >= 0 && channel < static_cast<int>(lastProgram.size());
      static_cast<MidiInstrument *>(instrument)
          ->OnStart(valid && lastProgram[channel] == slot,
                    valid && lastVolume[channel] == slot);
    } else {
      instrument->OnStart();
    }
  }
  init_filters();
};
