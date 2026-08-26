/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Model/Table.h"
#include "Application/UI2/Ui2InstrumentParameters.h"
#include "Foundation/Variables/Variable.h"

namespace ui2 {

// ENTER allocates a table only for the two instrument fields whose value is a
// table reference. Keeping the FourCC allow-list here prevents action rows and
// the still-unapproved SID/FILTER/SAMPLE interactions from consuming tables.
inline bool Ui2AllocateInstrumentTable(
    const Ui2InstrumentParameterDescriptor &descriptor, Variable &value,
    TableHolder &tables) {
  const bool tableField =
      descriptor.primary == FourCC::SampleInstrumentTable ||
      descriptor.primary == FourCC::MidiInstrumentTable;
  if (!descriptor.Valid() || !descriptor.editable || !tableField ||
      value.GetID() != descriptor.primary)
    return false;

  const int next = tables.GetNext();
  if (next == NO_MORE_TABLE)
    return false;
  value.SetInt(next);
  return true;
}

} // namespace ui2
