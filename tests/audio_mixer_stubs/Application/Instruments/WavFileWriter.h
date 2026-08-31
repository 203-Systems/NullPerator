#pragma once

#include "Application/Utils/fixed.h"

class WavFileWriter {
public:
  bool Open(const char *) { return false; }
  bool IsOpen() const { return false; }
  void AddBuffer(fixed *, int) {}
  void Close() {}
};
