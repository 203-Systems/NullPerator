/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_FILE_H
#define PICOTRACKER_WASM_FILE_H

#include "System/FileSystem/I_File.h"

#include <cstdio>

class WasmFile final : public I_File {
public:
  explicit WasmFile(std::FILE *file, bool initiallyDirty = false);
  ~WasmFile() override;

  int Read(void *ptr, int size) override;
  int GetC() override;
  int Write(const void *ptr, int size, int nmemb) override;
  void Seek(long offset, int whence) override;
  long Tell() override;
  int Error() override;
  bool Sync() override;
  void Dispose() override;

protected:
  bool Close() override;

private:
  std::FILE *file_;
  bool dirty_ = false;
};

#endif
