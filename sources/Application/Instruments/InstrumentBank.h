/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _INSTRUMENT_BANK_H_
#define _INSTRUMENT_BANK_H_

#include "Application/Instruments/I_Instrument.h"
#include "Application/Model/Song.h"
#include "Application/Persistency/Persistent.h"
#include "ChiptuneInstrument.h"
#include "DrumInstrument.h"
#include "Externals/etl/include/etl/array.h"
#include "GBInstrument.h"
#include "NoneInstrument.h"
#include "StackInstrument.h"
#include "System/Memory/Memory.h"

#define NO_MORE_INSTRUMENT 0x100

class InstrumentBank : public Persistent {
public:
  struct Allocator {
    void *(*allocate)(std::size_t) = PlatformMemory::AllocateBulk;
    void (*release)(void *) = PlatformMemory::FreeBulk;
  };

  // Staging handle used when an existing slot must be replaced
  // transactionally. The candidate has its own allocation and
  // is not visible through InstrumentsList() until Commit().  Destroying an
  // uncommitted handle frees that candidate, so parse/restore
  // failures cannot mutate or release the instrument currently in the slot.
  class Replacement {
  public:
    Replacement() = default;
    ~Replacement();

    Replacement(const Replacement &) = delete;
    Replacement &operator=(const Replacement &) = delete;

    I_Instrument *Candidate() const { return candidate_; }
    bool Commit();
    void Cancel();

  private:
    friend class InstrumentBank;
    InstrumentBank *bank_ = nullptr;
    I_Instrument *candidate_ = nullptr;
    I_Instrument *original_ = nullptr;
    unsigned short slot_ = NO_MORE_INSTRUMENT;
    std::uint32_t generation_ = 0;
  };

  InstrumentBank();
  explicit InstrumentBank(Allocator allocator);
  InstrumentBank(const InstrumentBank &) = delete;
  InstrumentBank &operator=(const InstrumentBank &) = delete;
  ~InstrumentBank();
  void Reset();
  void AssignDefaults();
  I_Instrument *GetInstrument(int i);
  virtual void SaveContent(tinyxml2::XMLPrinter *printer);
  virtual void RestoreContent(PersistencyDocument *doc);
  void Init();
  void OnStart();
  unsigned short GetNextAndAssignID(InstrumentType type, unsigned char id);
  bool BeginReplacement(unsigned short id, InstrumentType type,
                        Replacement &replacement);
  void releaseInstrument(unsigned short id);
  unsigned short Clone(unsigned short i);
  unsigned short GetNextFreeInstrumentSlotId();

  const etl::array<I_Instrument *, MAX_INSTRUMENT_COUNT> &
  InstrumentsList() const {
    return instruments_;
  }

private:
  I_Instrument *createInstrument(InstrumentType type);
  void destroyInstrument(I_Instrument *instrument);
  bool commitReplacement(Replacement &replacement);
  void cancelReplacement(Replacement &replacement);
  template <typename T, typename... Args>
  I_Instrument *allocateInstrument(Args &&...args);

  Allocator allocator_;
  TrackVoicePool<coping::chip::voice_t> chiptuneVoices_;
  TrackVoicePool<drum_voice_t> drumVoices_;
  TrackVoicePool<stack_voice_t> stackVoices_;
  // One lazily allocated pool shared across all three GB types, including
  // staged replacements. Never allocate a voice from the audio callback.
  TrackVoicePool<gb::Voice> *gbVoices_ = nullptr;
  unsigned gbUsers_ = 0;
  etl::array<I_Instrument *, MAX_INSTRUMENT_COUNT> instruments_;
  std::array<std::uint32_t, MAX_INSTRUMENT_COUNT> generations_{};
  NoneInstrument none_ = NoneInstrument();
};

#endif
