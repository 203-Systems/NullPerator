/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_POSIX_FILE_H
#define PICOTRACKER_POSIX_FILE_H

#include "System/FileSystem/I_File.h"

#include "StoragePolicy.h"
#include <cstdio>

class PosixFile : public I_File {
public:
  explicit PosixFile(std::FILE *file, bool initiallyDirty = false,
                     StoragePolicy policy = {});
  ~PosixFile() override;

  int Read(void *ptr, int size) override;
  int GetC() override;
  int Write(const void *ptr, int size, int nmemb) override;
  void Seek(long offset, int whence) override;
  long Tell() override;
  bool Truncate(long size) override;
  int Error() override;
  bool Sync() override;
  void Dispose() override;

protected:
  bool Close() override;

private:
  std::FILE *file_;
  StoragePolicy policy_;
  bool dirty_ = false;
};

#endif
