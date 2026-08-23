/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_FILESYSTEM_H
#define PICOTRACKER_WASM_FILESYSTEM_H

#include "System/FileSystem/FileSystem.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class WasmFileSystem final : public FileSystem {
public:
  static constexpr const char *MountPoint = "/data";

  explicit WasmFileSystem(std::string mountPoint = MountPoint);

  FileHandle Open(const char *name, const char *mode) override;
  bool chdir(const char *path) override;
  void list(etl::ivector<int> *fileIndexes, const char *filter,
            bool subDirOnly, bool includeHidden = false) override;
  void getFileName(int index, char *name, int length) override;
  PicoFileType getFileType(int index) override;
  bool isParentRoot() override;
  bool isCurrentRoot() override;
  bool DeleteFile(const char *name) override;
  bool DeleteDir(const char *name) override;
  bool exists(const char *path) override;
  bool makeDir(const char *path, bool pFlag = false) override;
  std::uint64_t getFileSize(int index) override;
  bool CopyFile(const char *srcFilename, const char *destFilename) override;
  bool MoveFile(const char *srcFilename, const char *destFilename) override;
  bool isExFat() override;

private:
  struct DirEntry {
    std::string name;
    PicoFileType type;
    std::uint64_t size;
  };

  bool Resolve(const char *path, std::string &resolved) const;
  static bool EnsureParentDirectories(const std::string &path,
                                      std::vector<std::string> &created);
  static bool RollbackCreatedDirectories(const std::vector<std::string> &created);
  void RefreshDirectory(const char *filter, bool subDirOnly,
                        bool includeHidden);

  mutable std::recursive_mutex mutex_;
  std::string root_;
  std::string cwd_;
  std::vector<DirEntry> entries_;
};

#endif
