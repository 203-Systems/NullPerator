/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _UI2_INSTRUMENT_IMPORT_TRANSACTION_H_
#define _UI2_INSTRUMENT_IMPORT_TRANSACTION_H_

namespace ui2 {

enum class Ui2InstrumentImportResult {
  Imported,
  AllocationFailed,
  RestoreFailed,
  CommitFailed,
};

// Bank::Replacement owns a detached fixed-pool candidate.  Loader is invoked
// only for that candidate, and no visible bank slot is changed until Commit().
// Keeping this small adapter templated avoids std::function/heap use on ESP32
// while making the complete transaction independently testable.
template <typename Bank, typename InstrumentType, typename Loader>
Ui2InstrumentImportResult
Ui2ImportInstrumentAtomically(Bank &bank, unsigned short slot,
                              InstrumentType type, Loader &&loader) {
  typename Bank::Replacement replacement;
  if (!bank.BeginReplacement(slot, type, replacement))
    return Ui2InstrumentImportResult::AllocationFailed;
  if (!loader(replacement.Candidate()))
    return Ui2InstrumentImportResult::RestoreFailed;
  if (!replacement.Commit())
    return Ui2InstrumentImportResult::CommitFailed;
  return Ui2InstrumentImportResult::Imported;
}

} // namespace ui2

#endif
