/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "PersistenceConstants.h"
#include <cstdio>
#include <cstring>
#define PROJECT_STATE_FILE "/.current"
#define PROJECT_STATE_TEMP_FILE "/.current.tmp"
#define PROJECT_STATE_BACKUP_FILE "/.current.bak"
#define PROJECT_STATE_BACKUP_TEMP_FILE "/.current.bak.tmp"
#define LOAD_ROLLBACK_FILE PROJECTS_DIR "/.load-rollback.dat"
#define STAGING_PROJECT_PATH PROJECTS_DIR "/" UNNAMED_PROJECT_NAME
#define STAGING_BACKUP_PATH PROJECTS_DIR "/" STAGING_BACKUP_PROJECT_NAME
#define MAX_DELETE_DEPTH 3

namespace PersistencyPaths {

template <size_t Capacity>
bool BuildSaveAsTransactionName(char (&destination)[Capacity],
                                const char *prefix, const char *projectName) {
  const size_t prefixLength = std::strlen(prefix);
  const size_t projectLength = std::strlen(projectName);
  if (prefixLength + projectLength + 1U > Capacity)
    return false;
  std::memcpy(destination, prefix, prefixLength);
  std::memcpy(destination + prefixLength, projectName, projectLength + 1U);
  return true;
}

template <size_t Capacity>
bool BuildProjectPath(char (&destination)[Capacity], const char *projectName) {
  const int length =
      std::snprintf(destination, Capacity, "%s/%s", PROJECTS_DIR, projectName);
  return length > 0 && static_cast<size_t>(length) < Capacity;
}

template <size_t Capacity>
bool BuildProjectFilePath(char (&destination)[Capacity],
                          const char *projectName, const char *filename) {
  const int length = std::snprintf(destination, Capacity, "%s/%s/%s",
                                   PROJECTS_DIR, projectName, filename);
  return length > 0 && static_cast<size_t>(length) < Capacity;
}

inline const char *SaveAsTransactionTarget(const char *name) {
  const size_t stagePrefixLength = std::strlen(SAVE_AS_STAGE_PREFIX);
  if (std::strncmp(name, SAVE_AS_STAGE_PREFIX, stagePrefixLength) == 0)
    return name + stagePrefixLength;
  const size_t backupPrefixLength = std::strlen(SAVE_AS_BACKUP_PREFIX);
  if (std::strncmp(name, SAVE_AS_BACKUP_PREFIX, backupPrefixLength) == 0)
    return name + backupPrefixLength;
  return nullptr;
}

inline constexpr const char *STAGING_COMMIT_CONTENTS = "COMMITTED";
inline constexpr const char *STAGING_PURGE_CONTENTS = "PURGE";

} // namespace PersistencyPaths
