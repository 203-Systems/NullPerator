/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SamplePoolLoading.h"

#include "Application/Instruments/SampleEditorFileJournal.h"
#include "Application/Persistency/PersistenceConstants.h"

#include <cstring>

namespace SamplePoolLoading {

bool EnterAndList(FileSystem &fileSystem, const char *projectName,
                  etl::ivector<int> &fileIndexes) {
  fileIndexes.clear();
  if (projectName == nullptr || projectName[0] == '\0' ||
      !fileSystem.chdir(PROJECTS_DIR) || !fileSystem.chdir(projectName)) {
    fileIndexes.clear();
    return false;
  }

  if (!fileSystem.chdir(PROJECT_SAMPLES_DIR)) {
    // Early PicoTracker projects with no samples did not create this
    // directory at all. They are valid empty pools, not corrupt projects.
    // Recreate it when the medium is writable so later imports have their
    // canonical destination; read-only media may still load the empty pool.
    if (fileSystem.exists(PROJECT_SAMPLES_DIR)) {
      fileIndexes.clear();
      return false;
    }
    if (!fileSystem.makeDir(PROJECT_SAMPLES_DIR, false) ||
        !fileSystem.chdir(PROJECT_SAMPLES_DIR)) {
      fileIndexes.clear();
      return true;
    }
  }

  if (!SampleEditorFileJournal::RecoverCurrentDirectory(fileSystem) ||
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

bool FitsLoadableSampleCapacity(FileSystem &fileSystem,
                                const etl::ivector<int> &fileIndexes,
                                std::size_t alreadyLoaded,
                                std::size_t capacity) {
  if (alreadyLoaded > capacity)
    return false;

  std::size_t loadableCount = 0U;
  for (const int index : fileIndexes) {
    char name[PFILENAME_SIZE]{};
    fileSystem.getFileName(index, name, sizeof(name));
    const PicoFileType type = fileSystem.getFileType(index);
    if (name[0] == '\0' || type == PFT_UNKNOWN)
      return false;
    if (type != PFT_FILE ||
        std::strlen(name) > MAX_INSTRUMENT_FILENAME_LENGTH) {
      continue;
    }
    ++loadableCount;
    if (loadableCount > capacity - alreadyLoaded)
      return false;
  }
  return true;
}

} // namespace SamplePoolLoading
