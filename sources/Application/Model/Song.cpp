/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Song.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Utils/HexBuffers.h"
#include "Phrase.h"
#include "System/System/System.h"
#include "System/io/Status.h"
#include "Table.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

Song::Song() : Persistent("SONG"), chain_(), phrase_() { Reset(); };

Song::~Song(){};

void Song::Reset() {
  for (int i = 0; i < SONG_CHANNEL_COUNT * SONG_ROW_COUNT; i++) {
    data_[i] = EMPTY_SONG_VALUE;
  }
  chain_.Reset();
  phrase_.Reset();
}

void Song::SaveContent(tinyxml2::XMLPrinter *printer) {
  saveHexBuffer(printer, "SONG", data_, SONG_ROW_COUNT * SONG_CHANNEL_COUNT);
  saveHexBuffer(printer, "CHAINS", chain_.data_,
                CHAIN_COUNT * PHRASES_PER_CHAIN);
  saveHexBuffer(printer, "TRANSPOSES", chain_.transpose_,
                CHAIN_COUNT * PHRASES_PER_CHAIN);
  saveHexBuffer(printer, "NOTES", phrase_.note_,
                PHRASE_COUNT * STEPS_PER_PHRASE);
  saveHexBuffer(printer, "INSTRUMENTS", phrase_.instr_,
                PHRASE_COUNT * STEPS_PER_PHRASE);
  saveHexBuffer(printer, "COMMAND1", phrase_.cmd1_,
                PHRASE_COUNT * STEPS_PER_PHRASE);
  saveHexBuffer(printer, "PARAM1", phrase_.param1_,
                PHRASE_COUNT * STEPS_PER_PHRASE);
  saveHexBuffer(printer, "COMMAND2", phrase_.cmd2_,
                PHRASE_COUNT * STEPS_PER_PHRASE);
  saveHexBuffer(printer, "PARAM2", phrase_.param2_,
                PHRASE_COUNT * STEPS_PER_PHRASE);
};

void Song::RestoreContent(PersistencyDocument *doc) {
  bool elem = doc->FirstChild();

  while (elem) {
    if (!strcmp("SONG", doc->ElemName())) {
      if (!restoreHexBuffer(doc, data_, sizeof(data_)))
        return;
      // CHAIN_COUNT is 0xFF: every byte 0x00..0xFE is a valid legacy chain ID
      // and 0xFF alone is the empty sentinel. Do not narrow this to 7 bits;
      // older projects legitimately use the upper half of the chain bank.
    };
    if (!strcmp("CHAINS", doc->ElemName())) {
      if (!restoreHexBuffer(doc, chain_.data_, sizeof(chain_.data_)))
        return;
      // 0xFF is the only empty sentinel. Every other byte is a valid phrase
      // ID, so legacy and imported projects can use the full 00..FE range.
      for (const unsigned char phrase : chain_.data_) {
        if (phrase != 0xFF && phrase >= PHRASE_COUNT) {
          doc->MarkError();
          return;
        }
      }
    };
    if (!strcmp("TRANSPOSES", doc->ElemName())) {
      if (!restoreHexBuffer(doc, chain_.transpose_, sizeof(chain_.transpose_)))
        return;
    };
    if (!strcmp("NOTES", doc->ElemName())) {
      if (!restoreHexBuffer(doc, phrase_.note_, sizeof(phrase_.note_)))
        return;
    };
    if (!strcmp("INSTRUMENTS", doc->ElemName())) {
      if (!restoreHexBuffer(doc, phrase_.instr_, sizeof(phrase_.instr_)))
        return;
      // 0xFF is the only empty sentinel. Reject corrupt instrument slots
      // before later Project/InstrumentBank code uses them as array indexes.
      for (const unsigned char instrument : phrase_.instr_) {
        if (instrument != 0xFF && instrument >= MAX_INSTRUMENT_COUNT) {
          doc->MarkError();
          return;
        }
      }
    };
    if (!strcmp("COMMAND1", doc->ElemName())) {
      if (!restoreHexBuffer(doc, phrase_.cmd1_,
                            PHRASE_COUNT * STEPS_PER_PHRASE))
        return;
    };
    if (!strcmp("PARAM1", doc->ElemName())) {
      if (!restoreHexBuffer(doc, reinterpret_cast<uchar *>(phrase_.param1_),
                            sizeof(phrase_.param1_)))
        return;
    };
    if (!strcmp("COMMAND2", doc->ElemName())) {
      if (!restoreHexBuffer(doc, phrase_.cmd2_,
                            PHRASE_COUNT * STEPS_PER_PHRASE))
        return;
    };
    if (!strcmp("PARAM2", doc->ElemName())) {
      if (!restoreHexBuffer(doc, reinterpret_cast<uchar *>(phrase_.param2_),
                            sizeof(phrase_.param2_)))
        return;
    };
    elem = doc->NextSibling();
  }

  Status::Set("Restoring allocation");

  // Restore chain & phrase allocation table

  unsigned char *data = data_;
  for (int i = 0; i < SONG_ROW_COUNT * SONG_CHANNEL_COUNT; i++) {
    if (*data != EMPTY_SONG_VALUE)
      chain_.SetUsed(*data);
    data++;
  }

  data = chain_.data_;

  for (int i = 0; i < CHAIN_COUNT; i++) {
    for (int j = 0; j < PHRASES_PER_CHAIN; j++) {
      if (*data != 0xFF) {
        chain_.SetUsed(i);
        phrase_.SetUsed(*data);
      }
      data++;
    };
  }

  data = phrase_.note_;

  FourCC *table1 = phrase_.cmd1_;
  FourCC *table2 = phrase_.cmd2_;

  ushort *param1 = phrase_.param1_;
  ushort *param2 = phrase_.param2_;

  TableHolder *th = TableHolder::GetInstance();

  for (int i = 0; i < PHRASE_COUNT; i++) {
    for (int j = 0; j < STEPS_PER_PHRASE; j++) {
      if (*data != 0xFF) {
        phrase_.SetUsed(i);
      }
      if (*table1 == FourCC::InstrumentCommandTable) {
        *param1 &= 0x7F;
        if (*param1 >= TABLE_COUNT) {
          doc->MarkError();
          return;
        }
        th->SetUsed((*param1));
      };
      if (*table2 == FourCC::InstrumentCommandTable) {
        *param2 &= 0x7F;
        if (*param2 >= TABLE_COUNT) {
          doc->MarkError();
          return;
        }
        th->SetUsed((*param2));
      };
      table1++;
      table2++;
      param1++;
      param2++;
      data++;
    };
  }
};
