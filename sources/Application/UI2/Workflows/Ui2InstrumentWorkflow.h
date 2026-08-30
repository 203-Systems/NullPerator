/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Instruments/I_Instrument.h"
#include "Application/UI2/Ui2InstrumentImportTransaction.h"
#include "Application/UI2/Ui2InstrumentTypeTransaction.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace ui2 {

enum class Ui2InstrumentImportOutcome : std::uint8_t {
  Imported,
  Unavailable,
  InvalidFile,
  AllocationFailed,
  RestoreFailed,
  CommitFailed,
};

enum class Ui2InstrumentTypeOutcome : std::uint8_t {
  Changed,
  NoChange,
  Unavailable,
  PlayingBlocked,
  AllocationFailed,
  CommitFailed,
};

enum class Ui2InstrumentStorageResult : std::uint8_t {
  Saved,
  Exists,
  Failed,
};

enum class Ui2InstrumentExportOutcome : std::uint8_t {
  Saved,
  NoInstrument,
  MissingName,
  Exists,
  Failed,
};

struct Ui2InstrumentExportFeedback {
  const char *text = "";
  bool error = false;
};

// Owns the data/service portion of Instrument lifecycle commands. The methods
// stay templated so production passes stack-only lambdas directly and focused
// tests can use fixed-pool fakes without std::function or another interface
// object. UI state (dialogs, status text, navigation and dirty generation)
// deliberately remains at the application boundary.
class Ui2InstrumentWorkflow final {
public:
  template <typename Bank, typename Detector, typename Loader>
  [[nodiscard]] static Ui2InstrumentImportOutcome
  Import(Bank *bank, unsigned short slot, const char *filename,
         bool storageAvailable, Detector &&detector, Loader &&loader) {
    if (bank == nullptr || !storageAvailable)
      return Ui2InstrumentImportOutcome::Unavailable;
    if (filename == nullptr || filename[0] == '\0')
      return Ui2InstrumentImportOutcome::InvalidFile;

    const InstrumentType importedType =
        std::forward<Detector>(detector)(filename);
    if (importedType <= IT_NONE || importedType >= IT_LAST)
      return Ui2InstrumentImportOutcome::InvalidFile;

    const Ui2InstrumentImportResult result = Ui2ImportInstrumentAtomically(
        *bank, slot, importedType, std::forward<Loader>(loader));
    switch (result) {
    case Ui2InstrumentImportResult::Imported:
      return Ui2InstrumentImportOutcome::Imported;
    case Ui2InstrumentImportResult::AllocationFailed:
      return Ui2InstrumentImportOutcome::AllocationFailed;
    case Ui2InstrumentImportResult::RestoreFailed:
      return Ui2InstrumentImportOutcome::RestoreFailed;
    case Ui2InstrumentImportResult::CommitFailed:
      return Ui2InstrumentImportOutcome::CommitFailed;
    }
    return Ui2InstrumentImportOutcome::RestoreFailed;
  }

  template <typename Bank>
  [[nodiscard]] static Ui2InstrumentTypeOutcome
  ChangeType(Bank *bank, unsigned short slot, InstrumentType requested,
             bool playerRunning) {
    if (bank == nullptr)
      return Ui2InstrumentTypeOutcome::Unavailable;
    auto *current = bank->GetInstrument(slot);
    if (current != nullptr && current->GetType() == requested)
      return Ui2InstrumentTypeOutcome::NoChange;
    // Playback can begin after the confirmation dialog opens. Keep this check
    // inside the workflow immediately before fixed-pool replacement.
    if (playerRunning)
      return Ui2InstrumentTypeOutcome::PlayingBlocked;

    switch (Ui2ChangeInstrumentTypeAtomically(*bank, slot, requested)) {
    case Ui2InstrumentTypeChangeResult::Changed:
      return Ui2InstrumentTypeOutcome::Changed;
    case Ui2InstrumentTypeChangeResult::AllocationFailed:
      return Ui2InstrumentTypeOutcome::AllocationFailed;
    case Ui2InstrumentTypeChangeResult::CommitFailed:
      return Ui2InstrumentTypeOutcome::CommitFailed;
    }
    return Ui2InstrumentTypeOutcome::CommitFailed;
  }

  template <typename Instrument, typename Exporter>
  [[nodiscard]] static Ui2InstrumentExportOutcome
  Export(Instrument *instrument, bool overwrite, Exporter &&exporter) {
    if (instrument == nullptr || instrument->GetType() == IT_NONE)
      return Ui2InstrumentExportOutcome::NoInstrument;
    const auto name = instrument->GetUserSetName();
    if (name.empty())
      return Ui2InstrumentExportOutcome::MissingName;

    switch (std::forward<Exporter>(exporter)(instrument, name.c_str(),
                                              overwrite)) {
    case Ui2InstrumentStorageResult::Saved:
      return Ui2InstrumentExportOutcome::Saved;
    case Ui2InstrumentStorageResult::Exists:
      return overwrite ? Ui2InstrumentExportOutcome::Failed
                       : Ui2InstrumentExportOutcome::Exists;
    case Ui2InstrumentStorageResult::Failed:
      return Ui2InstrumentExportOutcome::Failed;
    }
    return Ui2InstrumentExportOutcome::Failed;
  }
};

[[nodiscard]] constexpr const char *
Ui2InstrumentImportFailureText(Ui2InstrumentImportOutcome outcome) {
  switch (outcome) {
  case Ui2InstrumentImportOutcome::Unavailable:
    return "INSTRUMENT LOAD UNAVAILABLE";
  case Ui2InstrumentImportOutcome::InvalidFile:
    return "INVALID INSTRUMENT FILE";
  case Ui2InstrumentImportOutcome::AllocationFailed:
    return "NO FREE INSTRUMENT SLOT";
  case Ui2InstrumentImportOutcome::RestoreFailed:
  case Ui2InstrumentImportOutcome::CommitFailed:
    return "INSTRUMENT LOAD FAILED";
  case Ui2InstrumentImportOutcome::Imported:
    return "";
  }
  return "INSTRUMENT LOAD FAILED";
}

[[nodiscard]] constexpr const char *
Ui2InstrumentTypeFailureText(Ui2InstrumentTypeOutcome outcome) {
  switch (outcome) {
  case Ui2InstrumentTypeOutcome::Unavailable:
    return "INSTRUMENT TYPE UNAVAILABLE";
  case Ui2InstrumentTypeOutcome::AllocationFailed:
    return "NO FREE INSTRUMENT SLOT";
  case Ui2InstrumentTypeOutcome::CommitFailed:
    return "INSTRUMENT TYPE FAILED";
  case Ui2InstrumentTypeOutcome::Changed:
  case Ui2InstrumentTypeOutcome::NoChange:
  case Ui2InstrumentTypeOutcome::PlayingBlocked:
    return "";
  }
  return "INSTRUMENT TYPE FAILED";
}

[[nodiscard]] constexpr Ui2InstrumentExportFeedback
Ui2InstrumentExportFeedbackFor(Ui2InstrumentExportOutcome outcome) {
  switch (outcome) {
  case Ui2InstrumentExportOutcome::Saved:
    return {.text = "INSTRUMENT SAVED", .error = false};
  case Ui2InstrumentExportOutcome::NoInstrument:
    return {.text = "NO INSTRUMENT TO SAVE", .error = true};
  case Ui2InstrumentExportOutcome::MissingName:
    return {.text = "NAME INSTRUMENT FIRST", .error = true};
  case Ui2InstrumentExportOutcome::Exists:
    // The overwrite confirmation is already visible and owns this state.
    return {};
  case Ui2InstrumentExportOutcome::Failed:
    return {.text = "INSTRUMENT SAVE FAILED", .error = true};
  }
  return {.text = "INSTRUMENT SAVE FAILED", .error = true};
}

static_assert(std::is_empty_v<Ui2InstrumentWorkflow>);
static_assert(sizeof(Ui2InstrumentImportOutcome) == 1U);
static_assert(sizeof(Ui2InstrumentTypeOutcome) == 1U);
static_assert(sizeof(Ui2InstrumentExportOutcome) == 1U);
static_assert(std::is_trivially_copyable_v<Ui2InstrumentExportFeedback>);

} // namespace ui2
