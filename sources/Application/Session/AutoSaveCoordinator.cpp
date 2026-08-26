/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "AutoSaveCoordinator.h"

void AutoSaveCoordinator::OnProjectLoaded(std::uint32_t nowMs) noexcept {
  ResetProjectState(nowMs);
}

void AutoSaveCoordinator::OnProjectCreated(std::uint32_t nowMs) noexcept {
  ResetProjectState(nowMs);
}

void AutoSaveCoordinator::OnProjectSaved(std::uint32_t nowMs) noexcept {
  ResetProjectState(nowMs);
}

void AutoSaveCoordinator::OnProjectClosed(std::uint32_t nowMs) noexcept {
  ResetProjectState(nowMs);
  cadenceArmed_ = false;
}

void AutoSaveCoordinator::MarkDirty(std::uint32_t nowMs) noexcept {
  ++revision_;
  if (!cadenceArmed_) {
    cadenceStartedMs_ = nowMs;
    cadenceArmed_ = true;
  }
}

void AutoSaveCoordinator::SetPersistBusy(bool busy) noexcept {
  persistBusy_ = busy;
  if (!busy)
    autoSaveInFlight_ = false;
}

void AutoSaveCoordinator::SetSaveAsPending(bool pending) noexcept {
  saveAsPending_ = pending;
}

AutoSaveCoordinator::TickResult
AutoSaveCoordinator::Tick(std::uint32_t nowMs,
                          const Conditions &conditions) noexcept {
  if (!Due(nowMs))
    return TickResult::Idle;

  // Keep a real cadence while the project is clean. Without this advance, an
  // edit made after a long idle would save immediately merely because an old
  // deadline had expired while there was nothing to persist.
  if (!Dirty()) {
    AdvanceCadence(nowMs);
    return TickResult::Idle;
  }

  if (!conditions.projectLoaded || conditions.playerRunning ||
      conditions.recordingActive || !conditions.operationAllowsSave ||
      persistBusy_ || saveAsPending_) {
    return TickResult::Deferred;
  }

  persistBusy_ = true;
  autoSaveInFlight_ = true;
  savingRevision_ = revision_;
  return TickResult::SaveRequested;
}

void AutoSaveCoordinator::CompleteAutoSave(std::uint32_t nowMs,
                                           bool succeeded) noexcept {
  if (!autoSaveInFlight_)
    return;
  if (succeeded)
    persistedRevision_ = savingRevision_;
  persistBusy_ = false;
  autoSaveInFlight_ = false;
  // A failed write remains dirty, but follows the normal cadence instead of
  // retrying on every platform tick.
  AdvanceCadence(nowMs);
}

bool AutoSaveCoordinator::Due(std::uint32_t nowMs) const noexcept {
  return cadenceArmed_ &&
         static_cast<std::uint32_t>(nowMs - cadenceStartedMs_) >= IntervalMs;
}

void AutoSaveCoordinator::ResetProjectState(std::uint32_t nowMs) noexcept {
  cadenceStartedMs_ = nowMs;
  revision_ = 0U;
  persistedRevision_ = 0U;
  savingRevision_ = 0U;
  cadenceArmed_ = true;
  persistBusy_ = false;
  saveAsPending_ = false;
  autoSaveInFlight_ = false;
}

void AutoSaveCoordinator::AdvanceCadence(std::uint32_t nowMs) noexcept {
  cadenceStartedMs_ = nowMs;
  cadenceArmed_ = true;
}

