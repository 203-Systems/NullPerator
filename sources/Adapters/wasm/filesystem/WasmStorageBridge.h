/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

using WasmStorageMutationNotifier = void (*)();

// Mutation paths call this after a successful synchronous filesystem change.
// The WASM adapter records the mutation without starting IDBFS in the middle of
// a higher-level filesystem transaction.
void WasmStorage_NotifyMutation() noexcept;

// Called by the application loop after the current SDL event batch has fully
// returned. Multiple low-level changes are coalesced into one browser-side
// persistence request, so IDBFS never observes an atomic replace halfway
// through its backup/move/delete sequence.
void WasmStorage_FlushMutationNotifications() noexcept;

#ifdef HOST_TEST
void WasmStorage_SetMutationNotifierForTesting(
    WasmStorageMutationNotifier notifier) noexcept;
#endif
