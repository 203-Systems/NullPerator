/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _TABLE_H_
#define _TABLE_H_

#include "Application/Persistency/Persistent.h"
#include "Foundation/T_Singleton.h"
#include "Foundation/Types/Types.h"

#define TABLE_COUNT 0x20
#define TABLE_STEPS 16
#define TABLE_COLUMNS 3

#define NO_MORE_TABLE TABLE_COUNT + 10

// Table references are persisted as signed integers so VAR_OFF (-1) can be
// represented alongside the 00..1F table IDs. Keep these checks at the model
// boundary as well as in editors: older or externally-authored files can carry
// values that no current UI would generate.
[[nodiscard]] constexpr bool IsValidTableIndex(int table) {
  return table >= 0 && table < TABLE_COUNT;
}

[[nodiscard]] constexpr bool IsValidInstrumentTableValue(int table) {
  return table == -1 || IsValidTableIndex(table);
}

class Table {
public:
  Table();
  void Reset();
  bool IsEmpty();
  void Copy(const Table &other);

public:
  FourCC cmd1_[TABLE_STEPS];
  ushort param1_[TABLE_STEPS];
  FourCC cmd2_[TABLE_STEPS];
  ushort param2_[TABLE_STEPS];
  FourCC cmd3_[TABLE_STEPS];
  ushort param3_[TABLE_STEPS];
};

class TableHolder : public T_Singleton<TableHolder>, Persistent {
public:
  TableHolder();
  void Reset();
  Table &GetTable(int table);
  void SetUsed(int table);
  int GetNext();
  int Clone(int table);
  virtual void SaveContent(tinyxml2::XMLPrinter *printer);
  virtual void RestoreContent(PersistencyDocument *doc);

private:
  Table table_[TABLE_COUNT];
  bool allocation_[TABLE_COUNT];
};

#endif
