/* Test-only command domain used by the real UI2 tracker session adapter. */
#pragma once

#include "Foundation/Types/Types.h"

class CommandList {
public:
  static FourCC GetNext(FourCC current) { return current; }
  static FourCC GetPrev(FourCC current) { return current; }
  static FourCC GetNextAlpha(FourCC current) { return current; }
  static FourCC GetPrevAlpha(FourCC current) { return current; }
  static ushort RangeLimitCommandParam(FourCC, ushort value) { return value; }
};
