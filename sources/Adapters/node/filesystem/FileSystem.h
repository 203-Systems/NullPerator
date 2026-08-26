/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * VFS/FatFS-backed filesystem for Node (ESP32 + SDMMC).
 */

#ifndef _NODE_FILESYSTEM_H_
#define _NODE_FILESYSTEM_H_

#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"
#include <stdio.h>
#include <mutex>
#include <string>
#include <vector>

class NodeFileSystem : public FileSystem {
public:
  NodeFileSystem();
  ~NodeFileSystem() override = default;

  FileHandle Open(const char *name, const char *mode) override;
  bool chdir(const char *path) override;
  void list(etl::ivector<int> *fileIndexes, const char *filter,
            bool subDirOnly, bool includeHidden = false) override;
  bool listChecked(etl::ivector<int> *fileIndexes, const char *filter,
                   bool subDirOnly, bool includeHidden = false) override;
  void getFileName(int index, char *name, int length) override;
  PicoFileType getFileType(int index) override;
  bool isParentRoot() override;
  bool isCurrentRoot() override;
  bool DeleteFile(const char *name) override;
  bool DeleteDir(const char *name) override;
  bool exists(const char *path) override;
  bool makeDir(const char *path, bool pFlag = false) override;
  uint64_t getFileSize(int index) override;
  bool CopyFile(const char *src, const char *dest) override;
  bool MoveFile(const char *src, const char *dest) override;
  bool isExFat() override;

private:
  bool RefreshDir(const char *filter, bool subDirOnly, bool includeHidden);
  bool List_(etl::ivector<int> *fileIndexes, const char *filter,
             bool subDirOnly, bool includeHidden);

  std::mutex mutex_;
  std::string cwd_;
  struct DirEntry {
    std::string name;
    bool is_dir;
    uint64_t size;
  };
  std::vector<DirEntry> entries_;
};

class VfsFile : public I_File {
public:
  explicit VfsFile(FILE *f);
  ~VfsFile() override;

  int Read(void *ptr, int size) override;
  int GetC() override;
  int Write(const void *ptr, int size, int nmemb) override;
  void Seek(long offset, int whence) override;
  long Tell() override;
  int Error() override;
  bool Sync() override;
  bool Close() override;
  void Dispose() override;

private:
  FILE *f_;
};

#endif // _NODE_FILESYSTEM_H_
