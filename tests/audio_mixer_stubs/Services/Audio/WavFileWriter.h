#pragma once

#include "Foundation/Types/Fixed.h"

class WavFileWriter {
public:
  bool Open(const char *) { return false; }
  bool IsOpen() const { return false; }
  bool AddBuffer(const fixed *, int) { return false; }
  bool Close() { return true; }
};
