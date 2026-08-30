#include "tinyxml2adapter.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/I_File.h"
#include <cstdarg>
#include <cstdio>

void i_file_vfprintf(I_File *f, const char *fmt, va_list args) {
  if (f == nullptr || fmt == nullptr)
    return;

  char buffer[256];
  va_list copy;
  va_copy(copy, args);
  const int formatted = std::vsnprintf(buffer, sizeof(buffer), fmt, copy);
  va_end(copy);

  if (formatted <= 0 || formatted >= static_cast<int>(sizeof(buffer)))
    return;
  f->Write(buffer, 1, formatted);
}

void fprintf(I_File *f, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  i_file_vfprintf(f, fmt, args);
  va_end(args);
}
