/*
 * FatFS-backed filesystem for Node using ESP-IDF VFS (SDMMC 4-line mode).
 */

#include "FileSystem.h"
#include "PathResolver.h"

#include "Adapters/node/hal/nullperator/storage/storage.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/CopyFileJournal.h"
#include "System/FileSystem/I_File.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
constexpr const char *kMountPoint = "/sdcard";

int NoFollowStat(const char *path, struct stat *state) {
#if defined(ESP_PLATFORM)
  return stat(path, state);
#else
  return lstat(path, state);
#endif
}

bool IsSafeExistingPath(const std::string &path) {
  return NodePath::IsContainedWithoutSymlinks(kMountPoint, path, false);
}

bool IsSafeCreatablePath(const std::string &path) {
  return NodePath::IsContainedWithoutSymlinks(kMountPoint, path, true);
}

bool EnsureDirectory(const std::string &dir);

bool HasSafeExistingParent(const std::string &fullPath) {
  const auto pos = fullPath.find_last_of('/');
  if (pos == std::string::npos || pos < strlen(kMountPoint)) {
    return false;
  }
  const std::string parent = fullPath.substr(0, pos);
  struct stat state {};
  return IsSafeExistingPath(parent) &&
         NoFollowStat(parent.c_str(), &state) == 0 &&
         S_ISDIR(state.st_mode);
}

bool EnsureParentDirs(const std::string &full_path) {
  auto pos = full_path.find_last_of('/');
  if (pos == std::string::npos || pos < strlen(kMountPoint)) {
    return false;
  }
  const std::string dir = full_path.substr(0, pos);
  return EnsureDirectory(dir);
}

bool EnsureDirectory(const std::string &dir) {
  if (!IsSafeCreatablePath(dir)) {
    return false;
  }
  struct stat st {};
  if (NoFollowStat(dir.c_str(), &st) == 0) {
    return !S_ISLNK(st.st_mode) && S_ISDIR(st.st_mode);
  }
  if (errno != ENOENT || dir == kMountPoint || !EnsureParentDirs(dir)) {
    return false;
  }
  if (mkdir(dir.c_str(), 0777) != 0 && errno != EEXIST) {
    return false;
  }
  return NoFollowStat(dir.c_str(), &st) == 0 && !S_ISLNK(st.st_mode) &&
         S_ISDIR(st.st_mode);
}

bool MountCard() {
  if (!NullperatorHAL::Storage::IsMounted()) {
    Trace::Error("FILESYSTEM", "SD card is not mounted");
    return false;
  }
  return true;
}

bool MatchesFilter(const std::string &name, const char *filter) {
  if (filter == nullptr || filter[0] == '\0')
    return true;
  std::string lowerName = name;
  for (char &character : lowerName) {
    character = static_cast<char>(
        tolower(static_cast<unsigned char>(character)));
  }
  return lowerName.find(filter) != std::string::npos;
}
} // namespace

NodeFileSystem::NodeFileSystem() {
  std::lock_guard<std::mutex> lock(mutex_);
  cwd_ = kMountPoint;
  MountCard();
}

FileHandle NodeFileSystem::Open(const char *name, const char *mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!MountCard()) {
    return FileHandle();
  }
  const auto resolved = NodePath::Resolve(cwd_, name);
  if (!resolved) {
    Trace::Error("FILESYSTEM", "Path escapes SD mount: %s", name ? name : "");
    return FileHandle();
  }
  const std::string &full = *resolved;
  const bool mayCreate = mode && (strchr(mode, 'w') || strchr(mode, 'a'));
  const bool mayWrite = mayCreate || (mode && strchr(mode, '+'));
  if (!(mayCreate ? IsSafeCreatablePath(full) : IsSafeExistingPath(full))) {
    Trace::Error("FILESYSTEM", "Unsafe or symlinked path: %s", full.c_str());
    return FileHandle();
  }
  if (mayWrite) {
    if (!EnsureParentDirs(full)) {
      Trace::Error("FILESYSTEM", "EnsureParentDirs failed: %s errno:%d (%s)", full.c_str(), errno, strerror(errno));
      return FileHandle();
    }
    if (!IsSafeCreatablePath(full)) {
      Trace::Error("FILESYSTEM", "Path changed while opening: %s",
                   full.c_str());
      return FileHandle();
    }
  }
  FILE *f = fopen(full.c_str(), mode);
  if (f == nullptr) {
    int err = errno;
    Trace::Error("FILESYSTEM", "Open failed: %s mode:%s errno:%d (%s)", full.c_str(),
                 mode ? mode : "", err, strerror(err));
    return FileHandle();
  }
  return MakeFileHandle(new VfsFile(f));
}

bool NodeFileSystem::chdir(const char *path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (path == nullptr) {
    return false;
  }
  const auto resolved = NodePath::Resolve(cwd_, path);
  if (!resolved) {
    Trace::Error("FILESYSTEM", "Path escapes SD mount: %s", path);
    return false;
  }
  const std::string &newPath = *resolved;
  struct stat st {};
  if (IsSafeExistingPath(newPath) &&
      NoFollowStat(newPath.c_str(), &st) == 0 &&
      S_ISDIR(st.st_mode)) {
    cwd_ = newPath;
    return true;
  }
  Trace::Error("FILESYSTEM", "chdir failed: %s errno:%d (%s)", newPath.c_str(), errno, strerror(errno));
  return false;
}

bool NodeFileSystem::RefreshDir(const char *filter, bool subDirOnly,
                                bool includeHidden,
                                bool retainDirectories) {
  entries_.clear();
  if (!IsSafeExistingPath(cwd_)) {
    return false;
  }
  DIR *dir = opendir(cwd_.c_str());
  if (!dir) {
    return false;
  }
  if (cwd_ != kMountPoint) {
    entries_.emplace_back("..", true, 0);
  }
  while (true) {
    errno = 0;
    dirent *ent = readdir(dir);
    if (!ent) {
      if (errno != 0) {
        closedir(dir);
        return false;
      }
      break;
    }
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    std::string name(ent->d_name);
    const bool isHidden = !name.empty() && name.front() == '.';
    if (!includeHidden && isHidden) {
      continue;
    }
    // Preserve the cheap filename rejection used by non-browser scans. A
    // browser needs the stat first so it can retain non-matching directories.
    if (!retainDirectories && !MatchesFilter(name, filter)) {
      continue;
    }
    std::string full = cwd_ + "/" + name;
    struct stat st {};
    if (NoFollowStat(full.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) {
      closedir(dir);
      return false;
    }
    const uint64_t sz = st.st_size;
    const bool isDir = S_ISDIR(st.st_mode);
    if (retainDirectories && !isDir && !MatchesFilter(name, filter)) {
      continue;
    }
    if (subDirOnly && !isDir) {
      continue;
    }
    entries_.emplace_back(name.c_str(), isDir, sz);
  }
  return closedir(dir) == 0;
}

void NodeFileSystem::list(etl::ivector<int> *fileIndexes, const char *filter,
                          bool subDirOnly, bool includeHidden) {
  std::lock_guard<std::mutex> lock(mutex_);
  (void)List_(fileIndexes, filter, subDirOnly, includeHidden);
}

bool NodeFileSystem::listChecked(etl::ivector<int> *fileIndexes,
                                 const char *filter, bool subDirOnly,
                                 bool includeHidden) {
  std::lock_guard<std::mutex> lock(mutex_);
  return List_(fileIndexes, filter, subDirOnly, includeHidden);
}

bool NodeFileSystem::listBrowserChecked(etl::ivector<int> *fileIndexes,
                                        const char *filter,
                                        bool includeHidden) {
  std::lock_guard<std::mutex> lock(mutex_);
  return List_(fileIndexes, filter, false, includeHidden, true);
}

bool NodeFileSystem::listPathChecked(
    const char *path, FileSystemDirectorySnapshot &snapshot,
    const char *filter, bool subDirOnly, bool includeHidden) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot.Reset();
  if (!MountCard())
    return false;
  const auto resolved = NodePath::Resolve(kMountPoint, path);
  if (!resolved || !IsSafeExistingPath(*resolved))
    return false;
  DIR *dir = opendir(resolved->c_str());
  if (dir == nullptr)
    return false;

  std::string loweredFilter = filter == nullptr ? "" : filter;
  for (char &character : loweredFilter)
    character = static_cast<char>(tolower(static_cast<unsigned char>(character)));

  bool scanned = true;
  while (true) {
    errno = 0;
    dirent *entry = readdir(dir);
    if (entry == nullptr) {
      scanned = errno == 0;
      break;
    }
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    const std::string name(entry->d_name);
    if (!includeHidden && !name.empty() && name.front() == '.')
      continue;
    std::string loweredName = name;
    for (char &character : loweredName)
      character =
          static_cast<char>(tolower(static_cast<unsigned char>(character)));
    if (!loweredFilter.empty() &&
        loweredName.find(loweredFilter) == std::string::npos)
      continue;

    const std::string full = *resolved + "/" + name;
    struct stat state {};
    if (NoFollowStat(full.c_str(), &state) != 0 || S_ISLNK(state.st_mode)) {
      scanned = false;
      break;
    }
    const bool directory = S_ISDIR(state.st_mode);
    if (subDirOnly && !directory)
      continue;
    if (!snapshot.Add(name.c_str(), directory ? PFT_DIR : PFT_FILE,
                      static_cast<std::uint64_t>(state.st_size)))
      break;
  }
  return closedir(dir) == 0 && scanned;
}

bool NodeFileSystem::List_(etl::ivector<int> *fileIndexes, const char *filter,
                           bool subDirOnly, bool includeHidden,
                           bool retainDirectories) {
  if (fileIndexes != nullptr) {
    fileIndexes->clear();
  }
  const bool scanned =
      RefreshDir(filter, subDirOnly, includeHidden, retainDirectories);
  size_t listedCount = 0;
  if (fileIndexes != nullptr) {
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (fileIndexes->full()) {
        break;
      }
      fileIndexes->push_back(static_cast<int>(i));
      ++listedCount;
    }
  }
  return scanned && fileIndexes != nullptr && listedCount == entries_.size();
}

void NodeFileSystem::getFileName(int index, char *name, int length) {
  if (name == nullptr || length <= 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 0 || static_cast<size_t>(index) >= entries_.size()) {
    name[0] = '\0';
    return;
  }
  strncpy(name, entries_[index].name.c_str(), length - 1);
  name[length - 1] = '\0';
}

PicoFileType NodeFileSystem::getFileType(int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 0 || static_cast<size_t>(index) >= entries_.size()) {
    return PFT_UNKNOWN;
  }
  return entries_[index].is_dir ? PFT_DIR : PFT_FILE;
}

bool NodeFileSystem::isParentRoot() {
  if (cwd_ == kMountPoint) {
    return false;
  }
  const size_t separator = cwd_.find_last_of('/');
  return separator != std::string::npos &&
         cwd_.substr(0, separator) == kMountPoint;
}
bool NodeFileSystem::isCurrentRoot() { return cwd_ == kMountPoint; }

bool NodeFileSystem::DeleteFile(const char *name) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto full = NodePath::Resolve(cwd_, name);
  return full && IsSafeExistingPath(*full) && unlink(full->c_str()) == 0;
}

bool NodeFileSystem::DeleteDir(const char *name) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto full = NodePath::Resolve(cwd_, name);
  return full && IsSafeExistingPath(*full) && rmdir(full->c_str()) == 0;
}

bool NodeFileSystem::exists(const char *path) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto full = NodePath::Resolve(cwd_, path);
  if (!full) {
    return false;
  }
  struct stat st {};
  return IsSafeExistingPath(*full) &&
         NoFollowStat(full->c_str(), &st) == 0;
}

bool NodeFileSystem::makeDir(const char *path, bool pFlag) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto resolved = NodePath::Resolve(cwd_, path);
  if (!resolved) {
    return false;
  }
  const std::string &full = *resolved;
  if (!pFlag) {
    if (!IsSafeCreatablePath(full) || !HasSafeExistingParent(full) ||
        mkdir(full.c_str(), 0755) != 0) {
      return false;
    }
    struct stat state {};
    return NoFollowStat(full.c_str(), &state) == 0 &&
           !S_ISLNK(state.st_mode) &&
           S_ISDIR(state.st_mode);
  }
  return EnsureDirectory(full);
}

uint64_t NodeFileSystem::getFileSize(int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 0 || static_cast<size_t>(index) >= entries_.size()) {
    return 0;
  }
  return entries_[index].size;
}

bool NodeFileSystem::CopyFile(const char *src, const char *dest) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto source = NodePath::Resolve(cwd_, src);
  const auto target = NodePath::Resolve(cwd_, dest);
  if (!source || !target || *source == *target ||
      !IsSafeExistingPath(*source) || !IsSafeCreatablePath(*target)) {
    return false;
  }
  // A missing/unreadable source must not create, truncate or unlink the real
  // destination. Open it before any destination-side operation.
  FILE *in = fopen(source->c_str(), "rb");
  if (in == nullptr) {
    return false;
  }

  if (!EnsureParentDirs(*target)) {
    fclose(in);
    return false;
  }
  if (!IsSafeCreatablePath(*target)) {
    fclose(in);
    return false;
  }
  const std::size_t tempCapacity = FileCopyJournal::SiblingPathCapacity(
      target->c_str(), FileCopyJournal::TempPrefix);
  const std::size_t backupCapacity = FileCopyJournal::SiblingPathCapacity(
      target->c_str(), FileCopyJournal::BackupPrefix);
  if (tempCapacity == 0U || backupCapacity == 0U) {
    fclose(in);
    return false;
  }
  std::string temp(tempCapacity, '\0');
  std::string backup(backupCapacity, '\0');
  if (!FileCopyJournal::BuildSiblingPath(
          target->c_str(), FileCopyJournal::TempPrefix, temp.data(),
          temp.size()) ||
      !FileCopyJournal::BuildSiblingPath(
          target->c_str(), FileCopyJournal::BackupPrefix, backup.data(),
          backup.size())) {
    fclose(in);
    return false;
  }
  temp.resize(tempCapacity - 1U);
  backup.resize(backupCapacity - 1U);
  if (!IsSafeCreatablePath(temp) || !IsSafeCreatablePath(backup)) {
    fclose(in);
    return false;
  }
  struct stat state {};
  const bool targetExists = NoFollowStat(target->c_str(), &state) == 0;
  if (NoFollowStat(backup.c_str(), &state) == 0) {
    if (targetExists) {
      if (unlink(backup.c_str()) != 0) {
        fclose(in);
        return false;
      }
    } else if (rename(backup.c_str(), target->c_str()) != 0) {
      fclose(in);
      return false;
    }
  }
  if (NoFollowStat(temp.c_str(), &state) == 0 &&
      unlink(temp.c_str()) != 0) {
    fclose(in);
    return false;
  }

  FILE *out = fopen(temp.c_str(), "wb");
  if (out == nullptr) {
    fclose(in);
    return false;
  }
  char buf[4096];
  bool copied = true;
  size_t n = 0;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      copied = false;
      break;
    }
  }
  copied = copied && ferror(in) == 0 && fflush(out) == 0 &&
           fsync(fileno(out)) == 0 && ferror(out) == 0;
  const bool inputClosed = fclose(in) == 0;
  const bool outputClosed = fclose(out) == 0;
  copied = copied && inputClosed && outputClosed;
  if (!copied) {
    unlink(temp.c_str());
    return false;
  }

  // POSIX VFS may replace in one rename; FatFS uses a recoverable sibling
  // backup. Failure removes only this call's temp and restores the old target.
  if (rename(temp.c_str(), target->c_str()) == 0)
    return true;
  if (NoFollowStat(target->c_str(), &state) != 0) {
    unlink(temp.c_str());
    return false;
  }
  if (NoFollowStat(backup.c_str(), &state) == 0 &&
      unlink(backup.c_str()) != 0) {
    unlink(temp.c_str());
    return false;
  }
  if (rename(target->c_str(), backup.c_str()) != 0) {
    unlink(temp.c_str());
    return false;
  }
  if (rename(temp.c_str(), target->c_str()) != 0) {
    if (rename(backup.c_str(), target->c_str()) != 0)
      Trace::Error("FILESYSTEM", "Could not restore copy destination: %s",
                   target->c_str());
    unlink(temp.c_str());
    return false;
  }
  if (unlink(backup.c_str()) != 0)
    Trace::Error("FILESYSTEM", "Copy backup cleanup deferred: %s",
                 backup.c_str());
  return true;
}

bool NodeFileSystem::MoveFile(const char *src, const char *dest) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto source = NodePath::Resolve(cwd_, src);
  const auto target = NodePath::Resolve(cwd_, dest);
  if (!source || !target || !IsSafeExistingPath(*source) ||
      !IsSafeCreatablePath(*target) || !EnsureParentDirs(*target) ||
      !IsSafeCreatablePath(*target)) {
    return false;
  }
  return rename(source->c_str(), target->c_str()) == 0;
}

bool NodeFileSystem::isExFat() { return false; }

// -------- VfsFile -----------

VfsFile::VfsFile(FILE *f) : f_(f) {}
VfsFile::~VfsFile() { Close(); }

int VfsFile::Read(void *ptr, int size) { return static_cast<int>(fread(ptr, 1, size, f_)); }
int VfsFile::GetC() { return fgetc(f_); }
int VfsFile::Write(const void *ptr, int size, int nmemb) {
  return static_cast<int>(fwrite(ptr, size, nmemb, f_));
}
void VfsFile::Seek(long offset, int whence) { fseek(f_, offset, whence); }
long VfsFile::Tell() { return ftell(f_); }
bool VfsFile::Close() {
  if (!f_)
    return true;
  FILE *file = f_;
  f_ = nullptr;
  return fclose(file) == 0;
}
int VfsFile::Error() { return f_ ? ferror(f_) : -1; }
bool VfsFile::Sync() {
  return f_ && fflush(f_) == 0 && fsync(fileno(f_)) == 0;
}
void VfsFile::Dispose() { delete this; }
