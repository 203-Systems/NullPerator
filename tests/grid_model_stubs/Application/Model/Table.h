/* Test-only fixed table storage used by the real UI2 tracker adapter. */
#pragma once

#include "Foundation/Types/Types.h"

#include <algorithm>
#include <array>

#define TABLE_COUNT 0x20
#define TABLE_STEPS 16
#define NO_MORE_TABLE (TABLE_COUNT + 10)

class Table {
public:
  Table() { Reset(); }

  void Reset() {
    std::fill(std::begin(cmd1_), std::end(cmd1_),
              FourCC::InstrumentCommandNone);
    std::fill(std::begin(param1_), std::end(param1_), 0U);
    std::fill(std::begin(cmd2_), std::end(cmd2_),
              FourCC::InstrumentCommandNone);
    std::fill(std::begin(param2_), std::end(param2_), 0U);
    std::fill(std::begin(cmd3_), std::end(cmd3_),
              FourCC::InstrumentCommandNone);
    std::fill(std::begin(param3_), std::end(param3_), 0U);
  }

  FourCC cmd1_[TABLE_STEPS]{};
  ushort param1_[TABLE_STEPS]{};
  FourCC cmd2_[TABLE_STEPS]{};
  ushort param2_[TABLE_STEPS]{};
  FourCC cmd3_[TABLE_STEPS]{};
  ushort param3_[TABLE_STEPS]{};
};

class TableHolder {
public:
  static TableHolder *GetInstance() {
    static TableHolder holder;
    return &holder;
  }

  Table &GetTable(int table) { return tables_[table & (TABLE_COUNT - 1)]; }

  void Reset() {
    for (Table &table : tables_)
      table.Reset();
    used_.fill(false);
  }

  int GetNext() {
    for (int index = 0; index < TABLE_COUNT; ++index) {
      if (!used_[index]) {
        used_[index] = true;
        return index;
      }
    }
    return NO_MORE_TABLE;
  }

  void SetUsed(int table) {
    if (table >= 0 && table < TABLE_COUNT)
      used_[table] = true;
  }

private:
  std::array<Table, TABLE_COUNT> tables_{};
  std::array<bool, TABLE_COUNT> used_{};
};
