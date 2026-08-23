/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

using WasmStorageMutationNotifier = void (*)();

// Mutation paths call this after a successful synchronous filesystem change.
// It only schedules browser-side persistence and never waits for IndexedDB.
void WasmStorage_NotifyMutation() noexcept;

#ifdef HOST_TEST
void WasmStorage_SetMutationNotifierForTesting(
    WasmStorageMutationNotifier notifier) noexcept;
#endif
