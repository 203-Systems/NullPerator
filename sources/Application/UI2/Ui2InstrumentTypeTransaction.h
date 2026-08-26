/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

namespace ui2 {

enum class Ui2InstrumentTypeChangeResult {
  Changed,
  AllocationFailed,
  CommitFailed,
};

// Match InstrumentView::checkInstrumentModified() for parameter state while
// also protecting a user-set name. IsEmpty() is not a reliable proxy: MIDI,
// SID and OPAL report non-empty even at their parameter defaults.
template <typename Instrument>
bool Ui2InstrumentNeedsTypeChangeConfirmation(Instrument *instrument) {
  if (instrument == nullptr)
    return false;
  if (!instrument->GetUserSetName().empty())
    return true;
  auto *variables = instrument->Variables();
  if (variables == nullptr)
    return false;
  for (auto *variable : *variables) {
    if (variable != nullptr && variable->IsModified())
      return true;
  }
  return false;
}

// Replacement owns a detached fixed-pool instrument until Commit(). This
// means pool exhaustion or a stale slot can never destroy the current
// instrument before the requested type is ready.
template <typename Bank, typename InstrumentType>
Ui2InstrumentTypeChangeResult
Ui2ChangeInstrumentTypeAtomically(Bank &bank, unsigned short slot,
                                  InstrumentType type) {
  typename Bank::Replacement replacement;
  if (!bank.BeginReplacement(slot, type, replacement))
    return Ui2InstrumentTypeChangeResult::AllocationFailed;
  if (!replacement.Commit())
    return Ui2InstrumentTypeChangeResult::CommitFailed;
  return Ui2InstrumentTypeChangeResult::Changed;
}

} // namespace ui2
