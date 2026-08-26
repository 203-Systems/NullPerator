/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "picoTrackerFileSystem.h"
#include "Externals/etl/include/etl/pool.h"
#include "System/FileSystem/CopyFileJournal.h"
#include "pico/multicore.h"
#include <cctype>
#include <cstdio>
#include <cstring>

// Global mutex for thread safety
Mutex mutex;

constexpr uint32_t MAX_OPEN_FILES = 10;

static etl::pool<picoTrackerFile, MAX_OPEN_FILES> filePool;

namespace {
bool ContainsCaseInsensitive(const char *text, const char *filter) {
  if (filter == nullptr || filter[0] == '\0')
    return true;
  if (text == nullptr)
    return false;
  for (const char *start = text; *start != '\0'; ++start) {
    const char *left = start;
    const char *right = filter;
    while (*left != '\0' && *right != '\0' &&
           std::tolower(static_cast<unsigned char>(*left)) ==
               std::tolower(static_cast<unsigned char>(*right))) {
      ++left;
      ++right;
    }
    if (*right == '\0')
      return true;
  }
  return false;
}
} // namespace

picoTrackerFileSystem::picoTrackerFileSystem() {
  // init out access mutex
  std::lock_guard<Mutex> lock(mutex);

  // Check for the common case, FAT filesystem as first partition
  Trace::Log("FILESYSTEM", "Try to mount SD Card");
  if (sd.begin(SD_CONFIG)) {
    Trace::Log("FILESYSTEM", "Mounted SD Card FAT Filesystem first partition");
    return;
  }
  // Do we have any kind of card?
  if (!sd.card() || sd.sdErrorCode() != 0) {
    Trace::Log("FILESYSTEM", "No SD Card present");
    return;
  }
  // Try to mount the whole card as FAT (without partition table)
  if (static_cast<FsVolume *>(&sd)->begin(sd.card(), true, 0)) {
    Trace::Log("FILESYSTEM",
               "Mounted SD Card FAT Filesystem without partition table");
    return;
  }
}

bool picoTrackerFileSystem::isExFat() {
  std::lock_guard<Mutex> lock(mutex);
  return sd.fatType() == FAT_TYPE_EXFAT;
}

FileHandle picoTrackerFileSystem::Open(const char *name, const char *mode) {
  Trace::Log("FILESYSTEM", "Open file:%s, mode:%s", name, mode);
  std::lock_guard<Mutex> lock(mutex);
  const bool hasPlus = (mode != nullptr) && (std::strchr(mode, '+') != nullptr);

  if (!mode || !*mode) {
    Trace::Error("Invalid mode: %s", mode ? mode : "(null)");
    return FileHandle();
  }

  oflag_t rmode = 0;
  switch (*mode) {
  case 'r':
    rmode = hasPlus ? O_RDWR : O_RDONLY;
    break;
  case 'w':
    rmode = (hasPlus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    break;
  default:
    Trace::Error("Invalid mode: %s", mode);
    return FileHandle();
  }
  FsBaseFile cwd;
  if (!cwd.openCwd()) {
    return FileHandle();
  }
  I_File *wFile = 0;
  if (!cwd.open(name, rmode)) {
    Trace::Error("FILESYSTEM: Cannot open file:%s", name, mode);
    return FileHandle();
  }
  wFile = filePool.create(cwd);
  if (wFile == nullptr) {
    Trace::Error("FILESYSTEM: No file slots available (max %d)",
                 static_cast<int>(MAX_OPEN_FILES));
    return FileHandle();
  }
  return MakeFileHandle(wFile);
}

bool picoTrackerFileSystem::chdir(const char *name) {
  Trace::Log("FILESYSTEM", "chdir:%s", name);
  std::lock_guard<Mutex> lock(mutex);

  sd.chvol();
  auto res = sd.vol()->chdir(name);
  File cwd;
  char buf[PFILENAME_SIZE];
  cwd.openCwd();
  cwd.getName(buf, 128);
  Trace::Log("FILESYSTEM", "new CWD:%s", buf);
  cwd.close();
  return res;
}

PicoFileType picoTrackerFileSystem::getFileType(int index) {
  std::lock_guard<Mutex> lock(mutex);

  FsBaseFile cwd;
  if (!cwd.openCwd()) {
    char name[PFILENAME_SIZE];
    cwd.getName(name, PFILENAME_SIZE);
    Trace::Error("Failed to open cwd: %s", name);
    return PFT_UNKNOWN;
  }
  FsBaseFile entry;
  entry.open(index);
  auto isDir = entry.isDirectory();
  entry.close();

  return isDir ? PFT_DIR : PFT_FILE;
}

void picoTrackerFileSystem::list(etl::ivector<int> *fileIndexes,
                                 const char *filter, bool subDirOnly,
                                 bool includeHidden) {
  std::lock_guard<Mutex> lock(mutex);
  (void)List_(fileIndexes, filter, subDirOnly, includeHidden);
}

bool picoTrackerFileSystem::listChecked(etl::ivector<int> *fileIndexes,
                                        const char *filter, bool subDirOnly,
                                        bool includeHidden) {
  std::lock_guard<Mutex> lock(mutex);
  return List_(fileIndexes, filter, subDirOnly, includeHidden);
}

bool picoTrackerFileSystem::listPathChecked(
    const char *path, FileSystemDirectorySnapshot &snapshot,
    const char *filter, bool subDirOnly, bool includeHidden) {
  std::lock_guard<Mutex> lock(mutex);
  snapshot.Reset();
  if (path == nullptr || path[0] != '/')
    return false;

  File directory;
  if (!directory.open(path, O_RDONLY) || !directory.isDir()) {
    directory.close();
    return false;
  }

  bool scanned = true;
  File entry;
  char name[PFILENAME_SIZE]{};
  while (entry.openNext(&directory, O_READ)) {
    if (entry.getName(name, sizeof(name)) == 0U) {
      scanned = false;
      entry.close();
      break;
    }
    const bool directoryEntry = entry.isDirectory();
    const bool eligible = std::strcmp(name, ".") != 0 &&
                          std::strcmp(name, "..") != 0 &&
                          (includeHidden || !entry.isHidden()) &&
                          (!subDirOnly || directoryEntry) &&
                          ContainsCaseInsensitive(name, filter);
    if (eligible &&
        !snapshot.Add(name, directoryEntry ? PFT_DIR : PFT_FILE,
                      directoryEntry ? 0U : entry.fileSize())) {
      if (!entry.close())
        scanned = false;
      break;
    }
    if (!entry.close()) {
      scanned = false;
      break;
    }
  }
  scanned = scanned && directory.getError() == 0;
  return directory.close() && scanned;
}

bool picoTrackerFileSystem::List_(etl::ivector<int> *fileIndexes,
                                  const char *filter, bool subDirOnly,
                                  bool includeHidden) {
  if (fileIndexes == nullptr)
    return false;

  fileIndexes->clear();

  File cwd;
  if (!cwd.openCwd()) {
    char name[PFILENAME_SIZE];
    cwd.getName(name, PFILENAME_SIZE);
    Trace::Error("Failed to open cwd");
    return false;
  }
  char buffer[PFILENAME_SIZE];
  cwd.getName(buffer, PFILENAME_SIZE);
  Trace::Log("FILESYSTEM", "LIST DIR:%s", buffer);

  if (!cwd.isDir()) {
    Trace::Error("Path is not a directory");
    cwd.close();
    return false;
  }

  File entry;
  uint16_t count = 0;
  // ref: https://github.com/greiman/SdFat/issues/353#issuecomment-1003422848
  bool scanned = true;
  while (count < fileIndexes->capacity() && entry.openNext(&cwd, O_READ)) {
    uint32_t index = entry.dirIndex();
    if (entry.getName(buffer, PFILENAME_SIZE) == 0U) {
      scanned = false;
      entry.close();
      break;
    }

    bool matchesFilter = true;
    if (strlen(filter) > 0) {
      tolowercase(buffer);
      matchesFilter = (strstr(buffer, filter) != nullptr);
      // Trace::Log("FILESYSTEM", "FILTER: %s=%s [%d]", buffer, filter,
      //            matchesFilter);
    }
    // filter out "." and files that dont match filter if a filter is given
    if ((entry.isDirectory() && entry.dirIndex() != 0) ||
        ((includeHidden || !entry.isHidden()) && matchesFilter)) {
      if (subDirOnly) {
        if (entry.isDirectory()) {
          fileIndexes->push_back(index);
        }
      } else {
        fileIndexes->push_back(index);
      }
      // Trace::Log("FILESYSTEM", "[%d] got file: %s", index, buffer);
      count++;
    } else {
      // Trace::Log("FILESYSTEM", "skipped hidden: %s", buffer);
    }
    if (!entry.close()) {
      scanned = false;
      break;
    }
  }
  const bool directoryReadOk = cwd.getError() == 0;
  const bool directoryClosed = cwd.close();
  scanned = scanned && directoryReadOk && directoryClosed;
  Trace::Log("FILESYSTEM", "scanned: %d, added file indexes:%d", count,
             fileIndexes->size());
  return scanned;
}

void picoTrackerFileSystem::getFileName(int index, char *name, int length) {
  std::lock_guard<Mutex> lock(mutex);
  FsBaseFile cwd;
  if (!cwd.openCwd()) {
    char dirname[PFILENAME_SIZE];
    cwd.getName(dirname, PFILENAME_SIZE);
    Trace::Error("Failed to open cwd:%s", dirname);
    return;
  }
  FsBaseFile entry;
  entry.open(index);
  entry.getName(name, length);
  entry.close();
  cwd.close();
}

bool picoTrackerFileSystem::isParentRoot() {
  std::lock_guard<Mutex> lock(mutex);
  FsBaseFile cwd;
  if (!cwd.openCwd()) {
    char dirname[PFILENAME_SIZE];
    cwd.getName(dirname, PFILENAME_SIZE);
    Trace::Error("Failed to open cwd:%s", dirname);
    return false;
  }

  FsFile root;
  root.openRoot(sd.vol());
  FsFile up;
  up.open(1);
  // check the index=1 entry, aka ".." if its firstSector  matches
  // the root dirs firstSector, ie they are the same dir
  bool result = root.firstSector() == up.firstSector();
  root.close();
  up.close();
  cwd.close();
  return result;
}

bool picoTrackerFileSystem::isCurrentRoot() {
  std::lock_guard<Mutex> lock(mutex);
  FsBaseFile cwd;
  char dirname[PFILENAME_SIZE];
  cwd.getName(dirname, PFILENAME_SIZE);
  if (!cwd.openCwd()) {
    Trace::Error("Failed to open cwd:%s", dirname);
    return false;
  }

  cwd.getName(dirname, PFILENAME_SIZE);
  // If current path is root then its "/"
  return (strcmp(dirname, "/") == 0);
}

bool picoTrackerFileSystem::DeleteFile(const char *path) {
  std::lock_guard<Mutex> lock(mutex);
  return sd.remove(path);
}

bool picoTrackerFileSystem::DeleteDir(const char *path) {
  std::lock_guard<Mutex> lock(mutex);
  auto delDir = sd.open(path, O_READ);
  return delDir.rmdir();
}

bool picoTrackerFileSystem::exists(const char *path) {
  std::lock_guard<Mutex> lock(mutex);
  return sd.exists(path);
}

bool picoTrackerFileSystem::makeDir(const char *path, bool pFlag) {
  std::lock_guard<Mutex> lock(mutex);
  return sd.mkdir(path, pFlag);
}

uint64_t picoTrackerFileSystem::getFileSize(const int index) {
  std::lock_guard<Mutex> lock(mutex);
  FsBaseFile cwd;
  FsBaseFile entry;
  if (!entry.open(index)) {
    char name[PFILENAME_SIZE];
    cwd.getName(name, PFILENAME_SIZE);
    Trace::Error("Failed to open file: %d", index);
  }
  auto size = entry.fileSize();
  if (size == 0) {
    size = entry.fileSize();
  }
  entry.close();
  cwd.close();
  return size;
}

bool picoTrackerFileSystem::CopyFile(const char *srcFilename,
                                     const char *destFilename) {
  std::lock_guard<Mutex> lock(mutex);
  if (srcFilename == nullptr || destFilename == nullptr ||
      std::strcmp(srcFilename, destFilename) == 0) {
    return false;
  }

  // Open the source before touching any destination-side state. A missing
  // source or exhausted file slot must never truncate/delete an existing WAV.
  auto fSrc = sd.open(srcFilename, O_READ);
  if (!fSrc.isOpen()) {
    Trace::Error("Failed to open copy source: %s", srcFilename);
    return false;
  }

  char tempFilename[PFILENAME_SIZE]{};
  char backupFilename[PFILENAME_SIZE]{};
  if (!FileCopyJournal::BuildSiblingPath(
          destFilename, FileCopyJournal::TempPrefix, tempFilename,
          sizeof(tempFilename)) ||
      !FileCopyJournal::BuildSiblingPath(
          destFilename, FileCopyJournal::BackupPrefix, backupFilename,
          sizeof(backupFilename))) {
    fSrc.close();
    return false;
  }

  // Recover the only ambiguous FAT state from a prior interrupted copy before
  // starting a new one. A present destination is authoritative; otherwise the
  // backup is restored. Temporary data is never promoted without this call's
  // successful Sync/close sequence.
  if (sd.exists(backupFilename)) {
    if (sd.exists(destFilename)) {
      if (!sd.remove(backupFilename)) {
        fSrc.close();
        return false;
      }
    } else if (!sd.rename(backupFilename, destFilename)) {
      fSrc.close();
      return false;
    }
  }
  if (sd.exists(tempFilename) && !sd.remove(tempFilename)) {
    fSrc.close();
    return false;
  }

  auto fDest = sd.open(tempFilename, O_WRITE | O_CREAT | O_TRUNC);
  if (!fDest.isOpen()) {
    Trace::Error("Failed to open copy temp: %s", tempFilename);
    fSrc.close();
    sd.remove(tempFilename);
    return false;
  }

  bool copied = true;
  const int bufferSize = sizeof(fileBuffer_);
  while (copied) {
    const int count = fSrc.read(fileBuffer_, bufferSize);
    if (count < 0) {
      Trace::Error("Failed to read file: %s", srcFilename);
      copied = false;
      break;
    }
    if (count > 0 &&
        fDest.write(fileBuffer_, static_cast<size_t>(count)) !=
            static_cast<size_t>(count)) {
      Trace::Error("Short write copying file: %s", tempFilename);
      copied = false;
      break;
    }
    if (count < bufferSize)
      break;
  }
  copied = copied && fSrc.getError() == 0 && fDest.getError() == 0 &&
           fDest.sync();
  const bool sourceClosed = fSrc.close();
  const bool destinationClosed = fDest.close();
  copied = copied && sourceClosed && destinationClosed;
  if (!copied) {
    sd.remove(tempFilename);
    return false;
  }

  // New destinations install in one rename. SdFat refuses overwrite, so an
  // existing destination uses a sibling backup journal and is restored on any
  // install failure. Only this call's temp is ever deleted on copy failure.
  if (sd.rename(tempFilename, destFilename))
    return true;
  if (!sd.exists(destFilename)) {
    sd.remove(tempFilename);
    return false;
  }
  if (!sd.rename(destFilename, backupFilename)) {
    sd.remove(tempFilename);
    return false;
  }
  if (!sd.rename(tempFilename, destFilename)) {
    if (!sd.rename(backupFilename, destFilename))
      Trace::Error("Failed to restore copy destination: %s", destFilename);
    sd.remove(tempFilename);
    return false;
  }
  if (!sd.remove(backupFilename))
    Trace::Error("Copy backup cleanup deferred: %s", backupFilename);
  return true;
}

bool picoTrackerFileSystem::MoveFile(const char *srcFilename,
                                     const char *destFilename) {
  std::lock_guard<Mutex> lock(mutex);
  return sd.rename(srcFilename, destFilename);
}

void picoTrackerFileSystem::tolowercase(char *temp) {
  // Convert to lower case
  char *s = temp;
  while (*s != '\0') {
    *s = tolower((unsigned char)*s);
    s++;
  }
}

// picoTrackerFile implementation

picoTrackerFile::picoTrackerFile(FsBaseFile file)
    : file_(file), isOpen_(true) {}

picoTrackerFile::~picoTrackerFile() { Close(); }

int picoTrackerFile::Read(void *ptr, int size) {
  std::lock_guard<Mutex> lock(mutex);
  return file_.read(ptr, size);
}

void picoTrackerFile::Seek(long offset, int whence) {
  std::lock_guard<Mutex> lock(mutex);
  switch (whence) {
  case SEEK_SET:
    file_.seek(offset);
    break;
  case SEEK_CUR:
    file_.seekCur(offset);
    break;
  case SEEK_END:
    file_.seekEnd(offset);
    break;
  default:
    Trace::Error("Invalid seek whence: %s", whence);
  }
}

int picoTrackerFile::GetC() {
  std::lock_guard<Mutex> lock(mutex);
  return file_.read();
}

int picoTrackerFile::Write(const void *ptr, int size, int nmemb) {
  std::lock_guard<Mutex> lock(mutex);
  return file_.write(ptr, size * nmemb);
}

long picoTrackerFile::Tell() {
  std::lock_guard<Mutex> lock(mutex);
  return file_.curPosition();
}

int picoTrackerFile::Error() {
  std::lock_guard<Mutex> lock(mutex);
  return file_.getError();
}

bool picoTrackerFile::Close() {
  if (!isOpen_) {
    return true;
  }

  std::lock_guard<Mutex> lock(mutex);
  bool closed = file_.close();
  if (closed) {
    isOpen_ = false;
  }
  return closed;
}

bool picoTrackerFile::Sync() {
  std::lock_guard<Mutex> lock(mutex);
  return file_.sync();
}

void picoTrackerFile::Dispose() { filePool.destroy(this); }
