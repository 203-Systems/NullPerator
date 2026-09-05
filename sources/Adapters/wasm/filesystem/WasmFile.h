/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "Adapters/posix/filesystem/PosixFile.h"
#include "WasmStorageBridge.h"
class WasmFile final : public PosixFile {
public:
  explicit WasmFile(std::FILE *file, bool dirty = false)
      : PosixFile(
            file, dirty,
            {StoragePolicy::SyncMode::Buffered, &WasmStorage_NotifyMutation}) {}
};
