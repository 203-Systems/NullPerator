/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "Adapters/posix/filesystem/PosixFileSystem.h"
#include "WasmStorageBridge.h"
#include <utility>
class WasmFileSystem final : public PosixFileSystem {
public:
  explicit WasmFileSystem(std::string mountPoint = MountPoint)
      : PosixFileSystem(
            std::move(mountPoint),
            {StoragePolicy::SyncMode::Buffered, &WasmStorage_NotifyMutation}) {}
};
