/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "System/FileSystem/CopyFileJournal.h"

namespace ui2 {

// Project SamplePool is deliberately flat: SamplePool::Load() enumerates only
// files in /projects/<project>/samples and stores leaf names. Keep every UI2
// entry point on the same contract so a nested browser leaf can never be
// reinterpreted as a different file at the pool root.
[[nodiscard]] constexpr bool Ui2IsFlatProjectSampleLeaf(const char *name) {
  if (name == nullptr || name[0] == '\0')
    return false;
  if (name[0] == '.' && name[1] == '\0')
    return false;
  if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
    return false;
  if (FileCopyJournal::IsReservedLeaf(name))
    return false;
  for (const char *cursor = name; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\')
      return false;
  }
  return true;
}

} // namespace ui2
