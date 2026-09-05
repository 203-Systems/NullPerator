/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "PosixFileSystem.h"

#include "PosixFile.h"
#include "StoragePolicy.h"
#include "System/Console/Profiler.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/CopyFileJournal.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace fs = std::filesystem;

namespace {
bool IsWithin(const fs::path &root, const fs::path &candidate) {
  auto rootPart = root.begin();
  auto candidatePart = candidate.begin();
  for (; rootPart != root.end(); ++rootPart, ++candidatePart) {
    if (candidatePart == candidate.end() || *candidatePart != *rootPart) {
      return false;
    }
  }
  return true;
}
} // namespace

PosixFileSystem::PosixFileSystem(std::string mountPoint, StoragePolicy policy)
    : policy_(policy),
      root_(fs::path(std::move(mountPoint)).lexically_normal().string()),
      cwd_(root_) {
  std::error_code error;
  fs::create_directories(root_, error);
  if (error) {
    Trace::Error("POSIX_FILESYSTEM", "cannot create %s: %s", root_.c_str(),
                 error.message().c_str());
    return;
  }
  const fs::path canonicalRoot = fs::weakly_canonical(root_, error);
  if (error) {
    Trace::Error("POSIX_FILESYSTEM", "cannot resolve %s: %s", root_.c_str(),
                 error.message().c_str());
    return;
  }
  root_ = canonicalRoot.string();
  cwd_ = root_;
}

bool PosixFileSystem::Resolve(const char *path, std::string &resolved) const {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }

  fs::path candidate;
  if (path[0] == '/') {
    candidate = fs::path(root_) / fs::path(path).relative_path();
  } else {
    candidate = fs::path(cwd_) / path;
  }
  candidate = candidate.lexically_normal();

  const fs::path root = fs::path(root_).lexically_normal();
  if (!IsWithin(root, candidate)) {
    Trace::Error("POSIX_FILESYSTEM", "path escapes /data: %s", path);
    return false;
  }

  std::error_code error;
  const fs::path canonicalCandidate = fs::weakly_canonical(candidate, error);
  if (error || !IsWithin(root, canonicalCandidate)) {
    Trace::Error("POSIX_FILESYSTEM", "path follows outside /data: %s", path);
    return false;
  }
  resolved = canonicalCandidate.string();
  return true;
}

bool PosixFileSystem::EnsureParentDirectories(
    const std::string &path, std::vector<std::string> &created) {
  created.clear();
  const fs::path parent = fs::path(path).parent_path();
  if (parent.empty()) {
    return true;
  }
  std::vector<fs::path> missing;
  fs::path cursor = parent;
  std::error_code error;
  while (!cursor.empty() && !fs::exists(cursor, error)) {
    if (error) {
      return false;
    }
    missing.push_back(cursor);
    const fs::path next = cursor.parent_path();
    if (next == cursor) {
      break;
    }
    cursor = next;
  }
  if (error) {
    return false;
  }
  error.clear();
  fs::create_directories(parent, error);
  for (const fs::path &directory : missing) {
    std::error_code existsError;
    if (fs::is_directory(directory, existsError) && !existsError) {
      created.push_back(directory.string());
    }
  }
  return !error;
}

bool PosixFileSystem::RollbackCreatedDirectories(
    const std::vector<std::string> &created) {
  bool removedAll = true;
  // missing is discovered leaf-to-root, which is the safe inverse removal
  // order for newly-created nested parents.
  for (const std::string &directory : created) {
    std::error_code error;
    if (!fs::exists(directory, error)) {
      removedAll = removedAll && !error;
      continue;
    }
    if (error || !fs::remove(directory, error) || error) {
      removedAll = false;
    }
  }
  return removedAll;
}

/*
 * Parent directories are part of the same persistence mutation as an Open,
 * Copy, or Move. A subsequent failed operation must leave no phantom tree.
 */
FileHandle PosixFileSystem::Open(const char *name, const char *mode) {
  PROFILE_SCOPE(TraceCategory::Files, TraceName::FileOpen);
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (mode == nullptr) {
    return FileHandle();
  }
  std::string path;
  if (!Resolve(name, path)) {
    return FileHandle();
  }
  const bool truncates = std::strchr(mode, 'w') != nullptr;
  const bool appends = std::strchr(mode, 'a') != nullptr;
  const bool createsParents = truncates || appends;
  std::vector<std::string> createdParents;
  if (createsParents && !EnsureParentDirectories(path, createdParents)) {
    if (!createdParents.empty() &&
        !RollbackCreatedDirectories(createdParents)) {
      policy_.NotifyMutation();
    }
    Trace::Error("POSIX_FILESYSTEM", "cannot create parent directories: %s",
                 path.c_str());
    return FileHandle();
  }
  std::error_code existsError;
  const bool existed = fs::exists(path, existsError) && !existsError;
  std::FILE *file = std::fopen(path.c_str(), mode);
  if (file == nullptr) {
    if (!createdParents.empty() &&
        !RollbackCreatedDirectories(createdParents)) {
      policy_.NotifyMutation();
    }
    Trace::Error("POSIX_FILESYSTEM", "open failed: %s (%s)", path.c_str(),
                 std::strerror(errno));
    return FileHandle();
  }
  if (createsParents && !SyncParents(path)) {
    std::fclose(file);
    return {};
  }
  // Opening with "w" truncates immediately; appending creates a file when
  // absent. Parent creation is also a mutation. Defer their notification to a
  // successful Sync/Close just like actual Write calls.
  return MakeFileHandle(new PosixFile(
      file, truncates || (appends && !existed) || !createdParents.empty(),
      policy_));
}

bool PosixFileSystem::chdir(const char *path) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string resolved;
  if (!Resolve(path, resolved)) {
    return false;
  }
  std::error_code error;
  if (!fs::is_directory(resolved, error) || error) {
    return false;
  }
  cwd_ = resolved;
  return true;
}

bool PosixFileSystem::RefreshDirectory(const char *filter, bool subDirOnly,
                                       bool includeHidden,
                                       bool retainDirectories) {
  PROFILE_SCOPE(TraceCategory::Files, TraceName::FileScan);
  entries_.clear();
  if (cwd_ != root_) {
    entries_.push_back({"..", PFT_DIR, 0});
  }

  std::string loweredFilter = filter == nullptr ? "" : filter;
  std::transform(loweredFilter.begin(), loweredFilter.end(),
                 loweredFilter.begin(), [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });

  std::error_code error;
  fs::directory_iterator iterator(cwd_, error);
  const fs::directory_iterator end;
  while (!error && iterator != end) {
    const fs::directory_entry &entry = *iterator;
    const std::string name = entry.path().filename().string();
    const bool hidden = !name.empty() && name.front() == '.';
    const bool directory = entry.is_directory(error);
    if (error) {
      break;
    }
    if ((subDirOnly && !directory) || (!includeHidden && hidden)) {
      iterator.increment(error);
      continue;
    }

    std::string loweredName = name;
    std::transform(loweredName.begin(), loweredName.end(), loweredName.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    if (!loweredFilter.empty() && !(retainDirectories && directory) &&
        loweredName.find(loweredFilter) == std::string::npos) {
      iterator.increment(error);
      continue;
    }

    std::uint64_t size = 0;
    if (!directory) {
      size = entry.file_size(error);
      if (error) {
        break;
      }
    }
    entries_.push_back({name, directory ? PFT_DIR : PFT_FILE,
                        static_cast<std::uint64_t>(size)});
    iterator.increment(error);
  }
  std::sort(entries_.begin() + (cwd_ == root_ ? 0 : 1), entries_.end(),
            [](const DirEntry &left, const DirEntry &right) {
              return left.name < right.name;
            });
  return !error;
}

void PosixFileSystem::list(etl::ivector<int> *fileIndexes, const char *filter,
                           bool subDirOnly, bool includeHidden) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  (void)List_(fileIndexes, filter, subDirOnly, includeHidden);
}

bool PosixFileSystem::listChecked(etl::ivector<int> *fileIndexes,
                                  const char *filter, bool subDirOnly,
                                  bool includeHidden) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return List_(fileIndexes, filter, subDirOnly, includeHidden);
}

bool PosixFileSystem::listBrowserChecked(etl::ivector<int> *fileIndexes,
                                         const char *filter,
                                         bool includeHidden) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return List_(fileIndexes, filter, false, includeHidden, true);
}

bool PosixFileSystem::listPathChecked(const char *path,
                                      FileSystemDirectorySnapshot &snapshot,
                                      const char *filter, bool subDirOnly,
                                      bool includeHidden) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  snapshot.Reset();
  std::string resolved;
  if (!Resolve(path, resolved))
    return false;

  std::string loweredFilter = filter == nullptr ? "" : filter;
  std::transform(loweredFilter.begin(), loweredFilter.end(),
                 loweredFilter.begin(), [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });

  std::error_code error;
  if (!fs::is_directory(resolved, error) || error)
    return false;
  fs::directory_iterator iterator(resolved, error);
  const fs::directory_iterator end;
  while (!error && iterator != end) {
    const fs::directory_entry &entry = *iterator;
    const std::string name = entry.path().filename().string();
    const bool hidden = !name.empty() && name.front() == '.';
    const bool directory = entry.is_directory(error);
    if (error)
      break;
    if ((subDirOnly && !directory) || (!includeHidden && hidden)) {
      iterator.increment(error);
      continue;
    }
    std::string loweredName = name;
    std::transform(loweredName.begin(), loweredName.end(), loweredName.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    if (!loweredFilter.empty() &&
        loweredName.find(loweredFilter) == std::string::npos) {
      iterator.increment(error);
      continue;
    }
    std::uint64_t size = 0U;
    if (!directory) {
      size = entry.file_size(error);
      if (error)
        break;
    }
    if (!snapshot.Add(name.c_str(), directory ? PFT_DIR : PFT_FILE, size))
      return true;
    iterator.increment(error);
  }
  return !error;
}

bool PosixFileSystem::List_(etl::ivector<int> *fileIndexes, const char *filter,
                            bool subDirOnly, bool includeHidden,
                            bool retainDirectories) {
  const bool scanned =
      RefreshDirectory(filter, subDirOnly, includeHidden, retainDirectories);
  if (fileIndexes == nullptr) {
    return false;
  }
  fileIndexes->clear();
  for (std::size_t index = 0; index < entries_.size() && !fileIndexes->full();
       ++index) {
    fileIndexes->push_back(static_cast<int>(index));
  }
  return scanned && fileIndexes->size() == entries_.size();
}

void PosixFileSystem::getFileName(int index, char *name, int length) {
  if (name == nullptr || length <= 0) {
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) {
    name[0] = '\0';
    return;
  }
  std::strncpy(name, entries_[static_cast<std::size_t>(index)].name.c_str(),
               static_cast<std::size_t>(length - 1));
  name[length - 1] = '\0';
}

PicoFileType PosixFileSystem::getFileType(int index) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) {
    return PFT_UNKNOWN;
  }
  return entries_[static_cast<std::size_t>(index)].type;
}

bool PosixFileSystem::isParentRoot() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (cwd_ == root_) {
    return false;
  }
  return fs::path(cwd_).parent_path() == fs::path(root_);
}

bool PosixFileSystem::isCurrentRoot() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return cwd_ == root_;
}

bool PosixFileSystem::DeleteFile(const char *name) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string path;
  if (!Resolve(name, path)) {
    return false;
  }
  std::error_code error;
  const bool removed = fs::is_regular_file(path, error) && !error &&
                       fs::remove(path, error) && !error;
  if (removed) {
    policy_.NotifyMutation();
  }
  return removed && SyncParents(path);
}

bool PosixFileSystem::DeleteDir(const char *name) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string path;
  if (!Resolve(name, path) || path == root_) {
    return false;
  }
  std::error_code error;
  const bool removed = fs::is_directory(path, error) && !error &&
                       fs::remove(path, error) && !error;
  if (removed) {
    policy_.NotifyMutation();
  }
  return removed && SyncParents(path);
}

bool PosixFileSystem::exists(const char *path) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string resolved;
  if (!Resolve(path, resolved)) {
    return false;
  }
  std::error_code error;
  return fs::exists(resolved, error) && !error;
}

bool PosixFileSystem::makeDir(const char *path, bool pFlag) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string resolved;
  if (!Resolve(path, resolved)) {
    return false;
  }
  std::error_code error;
  const bool created = pFlag ? fs::create_directories(resolved, error)
                             : fs::create_directory(resolved, error);
  const bool succeeded = !error && (created || fs::is_directory(resolved));
  if (created) {
    policy_.NotifyMutation();
  }
  return succeeded && SyncParents(resolved);
}

std::uint64_t PosixFileSystem::getFileSize(int index) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) {
    return 0;
  }
  return entries_[static_cast<std::size_t>(index)].size;
}

bool PosixFileSystem::CopyFile(const char *srcFilename,
                               const char *destFilename) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string source;
  std::string destination;
  if (!Resolve(srcFilename, source) || !Resolve(destFilename, destination) ||
      source == destination) {
    return false;
  }
  std::error_code error;
  if (!fs::is_regular_file(source, error) || error) {
    return false;
  }
  error.clear();
  const bool destinationExists = fs::exists(destination, error);
  if (error) {
    return false;
  }
  if (destinationExists && fs::is_directory(destination, error)) {
    return false;
  }
  if (error) {
    return false;
  }
  std::vector<std::string> createdParents;
  if (!EnsureParentDirectories(destination, createdParents)) {
    if (!createdParents.empty() &&
        !RollbackCreatedDirectories(createdParents)) {
      policy_.NotifyMutation();
    }
    return false;
  }

  const std::size_t tempCapacity = FileCopyJournal::SiblingPathCapacity(
      destination.c_str(), FileCopyJournal::TempPrefix);
  const std::size_t backupCapacity = FileCopyJournal::SiblingPathCapacity(
      destination.c_str(), FileCopyJournal::BackupPrefix);
  if (tempCapacity == 0U || backupCapacity == 0U) {
    if (!createdParents.empty() &&
        !RollbackCreatedDirectories(createdParents)) {
      policy_.NotifyMutation();
    }
    return false;
  }

  std::string temporary(tempCapacity, '\0');
  std::string backup(backupCapacity, '\0');
  if (!FileCopyJournal::BuildSiblingPath(destination.c_str(),
                                         FileCopyJournal::TempPrefix,
                                         temporary.data(), temporary.size()) ||
      !FileCopyJournal::BuildSiblingPath(destination.c_str(),
                                         FileCopyJournal::BackupPrefix,
                                         backup.data(), backup.size())) {
    if (!createdParents.empty() &&
        !RollbackCreatedDirectories(createdParents)) {
      policy_.NotifyMutation();
    }
    return false;
  }
  temporary.resize(tempCapacity - 1U);
  backup.resize(backupCapacity - 1U);

  bool journalMutated = false;
  auto notifyFailureMutation = [&]() {
    bool rollbackFailed = false;
    if (!createdParents.empty())
      rollbackFailed = !RollbackCreatedDirectories(createdParents);
    if (journalMutated || rollbackFailed)
      policy_.NotifyMutation();
    return false;
  };
  auto pathExists = [&](const std::string &path, bool &exists) {
    error.clear();
    exists = fs::exists(path, error);
    return !error;
  };
  auto removePath = [&](const std::string &path) {
    error.clear();
    const bool removed = fs::remove(path, error);
    if (removed)
      journalMutated = true;
    return removed && !error;
  };

  // Finish recovery from an interrupted FAT-style replacement before this
  // copy starts. A fully installed destination wins; if it is absent, the
  // synced backup is the only known-good version and is restored.
  bool backupExists = false;
  bool currentDestinationExists = destinationExists;
  if (!pathExists(backup, backupExists))
    return notifyFailureMutation();
  if (backupExists) {
    if (currentDestinationExists) {
      if (!removePath(backup))
        return notifyFailureMutation();
    } else {
      error.clear();
      fs::rename(backup, destination, error);
      if (error)
        return notifyFailureMutation();
      journalMutated = true;
      currentDestinationExists = true;
    }
  }

  bool temporaryExists = false;
  if (!pathExists(temporary, temporaryExists))
    return notifyFailureMutation();
  if (temporaryExists && !removePath(temporary))
    return notifyFailureMutation();

  error.clear();
  const bool copied =
      fs::copy_file(source, temporary, fs::copy_options::overwrite_existing,
                    error) &&
      !error;
  journalMutated = journalMutated || copied;
  if (!copied) {
    bool partialExists = false;
    if (pathExists(temporary, partialExists) && partialExists)
      removePath(temporary);
    return notifyFailureMutation();
  }

  error.clear();
  const std::uintmax_t sourceSize = fs::file_size(source, error);
  if (error) {
    removePath(temporary);
    return notifyFailureMutation();
  }
  error.clear();
  const std::uintmax_t temporarySize = fs::file_size(temporary, error);
  if (error || sourceSize != temporarySize) {
    removePath(temporary);
    return notifyFailureMutation();
  }

  if (!SyncFile(temporary)) {
    removePath(temporary);
    return notifyFailureMutation();
  }

  // POSIX/IDBFS normally installs by atomic rename-overwrite. Filesystems
  // which reject overwrite fall back to a recoverable sibling backup.
  error.clear();
  fs::rename(temporary, destination, error);
  if (!error) {
    policy_.NotifyMutation();
    return SyncParents(destination);
  }
  if (!currentDestinationExists) {
    removePath(temporary);
    return notifyFailureMutation();
  }

  if (!pathExists(backup, backupExists)) {
    removePath(temporary);
    return notifyFailureMutation();
  }
  if (backupExists && !removePath(backup)) {
    removePath(temporary);
    return notifyFailureMutation();
  }
  error.clear();
  fs::rename(destination, backup, error);
  if (error) {
    removePath(temporary);
    return notifyFailureMutation();
  }
  journalMutated = true;
  if (!SyncParents(backup))
    return notifyFailureMutation();

  error.clear();
  fs::rename(temporary, destination, error);
  if (error) {
    std::error_code restoreError;
    fs::rename(backup, destination, restoreError);
    if (restoreError)
      Trace::Error("POSIX_FILESYSTEM", "cannot restore copy destination: %s",
                   destination.c_str());
    removePath(temporary);
    return notifyFailureMutation();
  }
  journalMutated = true;
  if (!removePath(backup))
    Trace::Error("POSIX_FILESYSTEM", "copy backup cleanup deferred: %s",
                 backup.c_str());
  policy_.NotifyMutation();
  return SyncParents(destination);
}

bool PosixFileSystem::MoveFile(const char *srcFilename,
                               const char *destFilename) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string source;
  std::string destination;
  if (!Resolve(srcFilename, source) || !Resolve(destFilename, destination)) {
    return false;
  }
  std::error_code error;
  if (!fs::exists(source, error) || error) {
    return false;
  }
  error.clear();
  const bool destinationExists = fs::exists(destination, error);
  if (error) {
    return false;
  }
  if (destinationExists && fs::is_directory(destination, error)) {
    return false;
  }
  if (error) {
    return false;
  }
  std::vector<std::string> createdParents;
  if (!EnsureParentDirectories(destination, createdParents)) {
    if (!createdParents.empty() &&
        !RollbackCreatedDirectories(createdParents)) {
      policy_.NotifyMutation();
    }
    return false;
  }
  error.clear();
  fs::rename(source, destination, error);
  if (error) {
    if (!createdParents.empty() &&
        !RollbackCreatedDirectories(createdParents)) {
      policy_.NotifyMutation();
    }
    return false;
  }
  policy_.NotifyMutation();
  return SyncParents(source) && SyncParents(destination);
}

bool PosixFileSystem::isExFat() { return false; }

// Sync both the changed directory and any newly created ancestors. Never
// equate fflush/MEMFS completion with browser persistence in IndexedDB.
bool PosixFileSystem::SyncParents(const std::string &path) const {
  if (!policy_.Durable())
    return true;
  auto parent = fs::path(path).parent_path();
  while (!parent.empty()) {
    const int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
      return false;
    const bool synced = ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    if (!synced || !closed)
      return false;
    if (parent == fs::path(root_))
      return true;
    const auto next = parent.parent_path();
    if (next == parent)
      break;
    parent = next;
  }
  return true;
}

bool PosixFileSystem::SyncFile(const std::string &path) const {
  if (!policy_.Durable())
    return true;
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0)
    return false;
  const bool synced = ::fsync(fd) == 0;
  const bool closed = ::close(fd) == 0;
  return synced && closed;
}
