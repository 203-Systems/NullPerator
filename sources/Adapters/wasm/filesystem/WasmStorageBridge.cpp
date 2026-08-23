/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmStorageBridge.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>

namespace {
EM_JS(void, NotifyStorageCoordinatorOnBrowserMain, (), {
  Module['picoTrackerStorageMutation']?.();
});

void NotifyStorageCoordinator() { NotifyStorageCoordinatorOnBrowserMain(); }
} // namespace

void WasmStorage_NotifyMutation() noexcept {
  if (emscripten_is_main_runtime_thread()) {
    NotifyStorageCoordinator();
    return;
  }
  emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_V,
                                              NotifyStorageCoordinator);
}
#else
#include <atomic>

namespace {
std::atomic<WasmStorageMutationNotifier> notifier{nullptr};
}

void WasmStorage_NotifyMutation() noexcept {
  if (const auto callback = notifier.load(std::memory_order_acquire)) {
    callback();
  }
}

void WasmStorage_SetMutationNotifierForTesting(
    WasmStorageMutationNotifier callback) noexcept {
  notifier.store(callback, std::memory_order_release);
}
#endif
