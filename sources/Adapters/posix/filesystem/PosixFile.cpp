/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "PosixFile.h"
#include "StoragePolicy.h"
#include "System/Console/Profiler.h"

#include <unistd.h>
#include <utility>

PosixFile::PosixFile(std::FILE *file, bool initiallyDirty, StoragePolicy policy)
    : file_(file), policy_(policy), dirty_(initiallyDirty) {}

PosixFile::~PosixFile() { Close(); }

int PosixFile::Read(void *ptr, int size) {
  PROFILE_SCOPE(TraceCategory::Files, TraceName::FileRead);
  if (file_ == nullptr || ptr == nullptr || size < 0) {
    return 0;
  }
  return static_cast<int>(
      std::fread(ptr, 1, static_cast<std::size_t>(size), file_));
}

int PosixFile::GetC() { return file_ == nullptr ? EOF : std::fgetc(file_); }

int PosixFile::Write(const void *ptr, int size, int nmemb) {
  PROFILE_SCOPE(TraceCategory::Files, TraceName::FileWrite);
  if (file_ == nullptr || ptr == nullptr || size < 0 || nmemb < 0) {
    return 0;
  }
  const int written =
      static_cast<int>(std::fwrite(ptr, static_cast<std::size_t>(size),
                                   static_cast<std::size_t>(nmemb), file_));
  dirty_ = dirty_ || written > 0;
  return written;
}

void PosixFile::Seek(long offset, int whence) {
  if (file_ != nullptr) {
    std::fseek(file_, offset, whence);
  }
}

long PosixFile::Tell() { return file_ == nullptr ? -1L : std::ftell(file_); }

bool PosixFile::Truncate(long size) {
  if (file_ == nullptr || size < 0 || std::fflush(file_) != 0 ||
      ftruncate(fileno(file_), static_cast<off_t>(size)) != 0) {
    return false;
  }
  dirty_ = true;
  return true;
}

int PosixFile::Error() { return file_ == nullptr ? -1 : std::ferror(file_); }

bool PosixFile::Sync() {
  const bool synced = file_ != nullptr && std::fflush(file_) == 0 &&
                      (!policy_.Durable() || ::fsync(fileno(file_)) == 0);
  if (synced && dirty_) {
    dirty_ = false;
    policy_.NotifyMutation();
  }
  return synced;
}

bool PosixFile::Close() {
  if (file_ == nullptr) {
    return true;
  }
  std::FILE *file = std::exchange(file_, nullptr);
  const bool closed = std::fclose(file) == 0;
  if (closed && dirty_) {
    dirty_ = false;
    policy_.NotifyMutation();
  }
  return closed;
}

void PosixFile::Dispose() { delete this; }
