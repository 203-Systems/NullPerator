/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SamplePoolLoading.h"

#include "Application/Persistency/PersistenceConstants.h"

namespace SamplePoolLoading {

bool EnterAndList(FileSystem &fileSystem, const char *projectName,
                  etl::ivector<int> &fileIndexes) {
  fileIndexes.clear();
  if (projectName == nullptr || projectName[0] == '\0' ||
      !fileSystem.chdir(PROJECTS_DIR) || !fileSystem.chdir(projectName) ||
      !fileSystem.chdir(PROJECT_SAMPLES_DIR) ||
      !fileSystem.listChecked(&fileIndexes, ".wav", false)) {
    fileIndexes.clear();
    return false;
  }

  // listChecked cannot distinguish an exactly-full vector from a truncated
  // directory. Fail closed rather than silently rebinding sample indexes to a
  // partial alphabetic set.
  if (fileIndexes.full()) {
    fileIndexes.clear();
    return false;
  }
  return true;
}

} // namespace SamplePoolLoading
