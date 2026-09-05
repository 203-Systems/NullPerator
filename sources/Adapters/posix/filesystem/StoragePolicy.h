/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

struct StoragePolicy {
  enum class SyncMode { Durable, Buffered };
  SyncMode syncMode = SyncMode::Durable;
  void (*mutationNotifier)() = nullptr;
  void NotifyMutation() const {
    if (mutationNotifier)
      mutationNotifier();
  }
  bool Durable() const { return syncMode == SyncMode::Durable; }
};
