/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _FILESYSTEM_H_
#define _FILESYSTEM_H_

#include "Externals/etl/include/etl/vector.h"
#include "Foundation/T_Factory.h"
#include "System/FileSystem/FileHandle.h"
#include <stdint.h>

#define MAX_FILE_INDEX_SIZE 256
#define PFILENAME_SIZE 256                 // per FAT32 spec for LFNs
#define MAX_PROJECT_SAMPLE_PATH_LENGTH 146 // 17 + 128 + 1

enum PicoFileType { PFT_UNKNOWN, PFT_FILE, PFT_DIR };

// Caller-owned, allocation-free destination for an absolute-path directory
// scan. Implementations must copy any names they need to keep: the name passed
// to Add() is valid only for that call. Returning false stops a successful scan
// early (for example after a fixed-capacity page has been filled).
class FileSystemDirectorySnapshot {
public:
  virtual ~FileSystemDirectorySnapshot() = default;
  virtual void Reset() = 0;
  virtual bool Add(const char *name, PicoFileType type, uint64_t size) = 0;
};

// Forward declaration
class I_File;

// This is the main FileSystem interface that will be implemented by
// platform-specific classes
class FileSystem : public T_Factory<FileSystem> {
public:
  FileSystem() {}
  virtual ~FileSystem() {}

  virtual FileHandle Open(const char *name, const char *mode) = 0;
  virtual bool chdir(const char *path) = 0;
  virtual bool read(int index, void *data) {
    return false;
  } // Default implementation
  virtual void list(etl::ivector<int> *fileIndexes, const char *filter,
                    bool subDirOnly, bool includeHidden = false) = 0;
  // Transactional callers must be able to distinguish a genuinely empty
  // directory from a failed/short enumeration. Existing UI callers retain
  // the void API; adapters with detectable errors override this checked form.
  virtual bool listChecked(etl::ivector<int> *fileIndexes, const char *filter,
                           bool subDirOnly, bool includeHidden = false) {
    list(fileIndexes, filter, subDirOnly, includeHidden);
    return true;
  }
  // Browser scans apply the filter to leaf files while retaining directories
  // so users can descend into folders whose names do not contain the file
  // extension. Legacy adapters fall back to listChecked(); adapters used by
  // an interactive browser should override this form.
  virtual bool listBrowserChecked(etl::ivector<int> *fileIndexes,
                                  const char *filter,
                                  bool includeHidden = false) {
    return listChecked(fileIndexes, filter, false, includeHidden);
  }
  // Enumerates an absolute logical path without changing the process-global
  // working directory or the legacy index cache used by list()/getFileName().
  // Directory entries never synthesize "." or ".."; browser owners model
  // parent navigation themselves. The default keeps legacy-only adapters
  // source-compatible while making unsupported path scans fail explicitly.
  virtual bool listPathChecked(const char *path,
                               FileSystemDirectorySnapshot &snapshot,
                               const char *filter, bool subDirOnly,
                               bool includeHidden = false) {
    (void)path;
    (void)snapshot;
    (void)filter;
    (void)subDirOnly;
    (void)includeHidden;
    return false;
  }
  virtual void getFileName(int index, char *name, int length) = 0;
  virtual PicoFileType getFileType(int index) = 0;
  virtual bool isParentRoot() = 0;
  virtual bool isCurrentRoot() = 0;
  virtual bool DeleteFile(const char *name) = 0;
  virtual bool DeleteDir(const char *name) = 0;
  // Optional batching hook for filesystem implementations that cache listings.
  virtual void BeginBatch() {}
  virtual void EndBatch() {}
  virtual bool exists(const char *path) = 0;
  virtual bool makeDir(const char *path, bool pFlag = false) = 0;
  virtual uint64_t getFileSize(int index) = 0;
  virtual bool CopyFile(const char *srcFilename, const char *destFilename) = 0;
  virtual bool MoveFile(const char *srcFilename, const char *destFilename) = 0;
  virtual bool isExFat() = 0;
};

#endif // _FILESYSTEM_H_
