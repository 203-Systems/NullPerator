/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmFile.h"
#include "WasmStorageBridge.h"
#include "Adapters/wasm/tracing/WasmProfiler.h"

#include <utility>

WasmFile::WasmFile(std::FILE *file, bool initiallyDirty)
    : file_(file), dirty_(initiallyDirty) {}

WasmFile::~WasmFile() { Close(); }

int WasmFile::Read(void *ptr, int size) {
  WASM_TRACE_SCOPE(WasmTraceCategory::Files, WasmTraceName::FileRead);
  if (file_ == nullptr || ptr == nullptr || size < 0) {
    return 0;
  }
  return static_cast<int>(std::fread(ptr, 1, static_cast<std::size_t>(size),
                                     file_));
}

int WasmFile::GetC() { return file_ == nullptr ? EOF : std::fgetc(file_); }

int WasmFile::Write(const void *ptr, int size, int nmemb) {
  WASM_TRACE_SCOPE(WasmTraceCategory::Files, WasmTraceName::FileWrite);
  if (file_ == nullptr || ptr == nullptr || size < 0 || nmemb < 0) {
    return 0;
  }
  const int written = static_cast<int>(std::fwrite(
      ptr, static_cast<std::size_t>(size), static_cast<std::size_t>(nmemb), file_));
  dirty_ = dirty_ || written > 0;
  return written;
}

void WasmFile::Seek(long offset, int whence) {
  if (file_ != nullptr) {
    std::fseek(file_, offset, whence);
  }
}

long WasmFile::Tell() { return file_ == nullptr ? -1L : std::ftell(file_); }

int WasmFile::Error() { return file_ == nullptr ? -1 : std::ferror(file_); }

bool WasmFile::Sync() {
  const bool synced = file_ != nullptr && std::fflush(file_) == 0;
  if (synced && dirty_) {
    dirty_ = false;
    WasmStorage_NotifyMutation();
  }
  return synced;
}

bool WasmFile::Close() {
  if (file_ == nullptr) {
    return true;
  }
  std::FILE *file = std::exchange(file_, nullptr);
  const bool closed = std::fclose(file) == 0;
  if (closed && dirty_) {
    dirty_ = false;
    WasmStorage_NotifyMutation();
  }
  return closed;
}

void WasmFile::Dispose() { delete this; }
