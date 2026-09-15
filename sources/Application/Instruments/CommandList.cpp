/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "CommandList.h"
#include "Foundation/Types/FxCommands.h"

// Keep command entries grouped by displayed mnemonic first letter;
// GetNextAlpha/GetPrevAlpha depend on this ordering.
static constexpr auto _all = [] {
  std::array<FourCC::enum_type, fx::commands.size()> ids{};
  for (std::size_t i = 0; i < ids.size(); ++i)
    ids[i] = fx::commands[i].id;
  return ids;
}();

static char GetCommandGroupLetter(FourCC command) {
  const char *name = FourCC(command).c_str();
  return (name && name[0]) ? name[0] : '\0';
}

// Applies command-specific range limits to parameter values
ushort CommandList::RangeLimitCommandParam(FourCC command, ushort paramValue) {
  // Each command type can have its own specific range limits
  if (command == FourCC::InstrumentCommandVelocity) {
    // For VEL command, limit the bb part to 0x7F (127) while preserving the aa
    // part
    return (paramValue & 0xFF00) | (paramValue & 0x7F);
  }
  // Add more command-specific limits here as needed
  // Example:
  // else if (command == FourCC::InstrumentCommandMidiCC) {
  //   // MIDI CC values should also be limited to 0-127
  //   return (paramValue & 0xFF00) | (paramValue & 0x7F);
  // }

  // If no specific limit applies, return the original value
  return paramValue;
}

FourCC CommandList::GetNext(FourCC current) {
  for (uint i = 0; i < _all.size() - 1; i++) {
    if (_all[i] == current) {
      return _all[i + 1];
    };
  };
  return current;
};

FourCC CommandList::GetPrev(FourCC current) {
  uint count = _all.size();
  for (uint i = 2; i < count; i++) {
    if (_all[i] == current) {
      return _all[i - 1];
    };
  };
  return current;
};

FourCC CommandList::GetNextAlpha(FourCC current) {
  char letter = GetCommandGroupLetter(current);
  bool found = false;
  for (uint i = 0; i < _all.size(); i++) {
    char tLetter = GetCommandGroupLetter(_all[i]);
    if (!found) {
      if (tLetter == letter) {
        found = true;
      }
    } else {
      if (tLetter != letter) {
        return _all[i];
      }
    };
  };
  // No later group: saturate at the final command, including within V.
  return found ? _all[_all.size() - 1] : current;
};

FourCC CommandList::GetPrevAlpha(FourCC current) {

  char letter = GetCommandGroupLetter(current);
  bool found = false;
  FourCC tReturn = FourCC::Default;
  uint count = _all.size();

  for (uint i = count - 1; i > 0; i--) {
    char tLetter = GetCommandGroupLetter(_all[i]);
    if (!found) {
      if (tLetter == letter) {
        found = true;
      }
    } else {
      if (tLetter != letter) {
        if (tReturn == 0xFF) {
          tReturn = _all[i];
        } else {
          if (tLetter != GetCommandGroupLetter(tReturn)) {
            return tReturn;
          } else {
            tReturn = _all[i];
          }
        }
      }
    };
  };
  if (tReturn != 0xFF) {
    return tReturn;
  }
  return current;
};

FourCC CommandList::MoveGrid(FourCC current, int dx, int dy, bool table) {
  constexpr int count = static_cast<int>(_all.size());
  int index = 0;
  for (int i = 0; i < count; ++i)
    if (_all[i] == current)
      index = i;
  int col = index % 5 + dx, row = index / 5 + dy;
  if (col < 0 || col >= 5 || row < 0 || row >= (count + 4) / 5)
    return current;
  int next = row * 5 + col;
  if (next >= count)
    return current;
  if (table && _all[next] == FourCC::InstrumentCommandTable) {
    next += dy != 0 ? dy * 5 : dx;
    if (next < 0 || next >= count || (dy == 0 && next / 5 != row))
      return current;
  }
  return _all[next];
}
