/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmFileSystem.h"

#include "WasmFile.h"
#include "WasmStorageBridge.h"
#include "System/Console/Trace.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
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

WasmFileSystem::WasmFileSystem(std::string mountPoint)
    : root_(fs::path(std::move(mountPoint)).lexically_normal().string()),
      cwd_(root_) {
  std::error_code error;
  fs::create_directories(root_, error);
  if (error) {
    Trace::Error("WASM_FILESYSTEM", "cannot create %s: %s", root_.c_str(),
                 error.message().c_str());
    return;
  }
  const fs::path canonicalRoot = fs::weakly_canonical(root_, error);
  if (error) {
    Trace::Error("WASM_FILESYSTEM", "cannot resolve %s: %s", root_.c_str(),
                 error.message().c_str());
    return;
  }
  root_ = canonicalRoot.string();
  cwd_ = root_;
}

bool WasmFileSystem::Resolve(const char *path, std::string &resolved) const {
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
    Trace::Error("WASM_FILESYSTEM", "path escapes /data: %s", path);
    return false;
  }

  std::error_code error;
  const fs::path canonicalCandidate = fs::weakly_canonical(candidate, error);
  if (error || !IsWithin(root, canonicalCandidate)) {
    Trace::Error("WASM_FILESYSTEM", "path follows outside /data: %s", path);
    return false;
  }
  resolved = canonicalCandidate.string();
  return true;
}

bool WasmFileSystem::EnsureParentDirectories(
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

bool WasmFileSystem::RollbackCreatedDirectories(
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
FileHandle WasmFileSystem::Open(const char *name, const char *mode) {
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
    if (!createdParents.empty() && !RollbackCreatedDirectories(createdParents)) {
      WasmStorage_NotifyMutation();
    }
    Trace::Error("WASM_FILESYSTEM", "cannot create parent directories: %s",
                 path.c_str());
    return FileHandle();
  }
  std::error_code existsError;
  const bool existed = fs::exists(path, existsError) && !existsError;
  std::FILE *file = std::fopen(path.c_str(), mode);
  if (file == nullptr) {
    if (!createdParents.empty() && !RollbackCreatedDirectories(createdParents)) {
      WasmStorage_NotifyMutation();
    }
    Trace::Error("WASM_FILESYSTEM", "open failed: %s (%s)", path.c_str(),
                 std::strerror(errno));
    return FileHandle();
  }
  // Opening with "w" truncates immediately; appending creates a file when
  // absent. Parent creation is also a mutation. Defer their notification to a
  // successful Sync/Close just like actual Write calls.
  return MakeFileHandle(new WasmFile(file, truncates || (appends && !existed) ||
                                           !createdParents.empty()));
}

bool WasmFileSystem::chdir(const char *path) {
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

void WasmFileSystem::RefreshDirectory(const char *filter, bool subDirOnly,
                                      bool includeHidden) {
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
    if (!loweredFilter.empty() &&
        loweredName.find(loweredFilter) == std::string::npos) {
      iterator.increment(error);
      continue;
    }

    std::uint64_t size = 0;
    if (!directory) {
      size = entry.file_size(error);
      if (error) {
        error.clear();
        size = 0;
      }
    }
    entries_.push_back(
        {name, directory ? PFT_DIR : PFT_FILE, static_cast<std::uint64_t>(size)});
    iterator.increment(error);
  }
  std::sort(entries_.begin() + (cwd_ == root_ ? 0 : 1), entries_.end(),
            [](const DirEntry &left, const DirEntry &right) {
              return left.name < right.name;
            });
}

void WasmFileSystem::list(etl::ivector<int> *fileIndexes, const char *filter,
                          bool subDirOnly, bool includeHidden) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  RefreshDirectory(filter, subDirOnly, includeHidden);
  if (fileIndexes == nullptr) {
    return;
  }
  fileIndexes->clear();
  for (std::size_t index = 0;
       index < entries_.size() && !fileIndexes->full(); ++index) {
    fileIndexes->push_back(static_cast<int>(index));
  }
}

void WasmFileSystem::getFileName(int index, char *name, int length) {
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

PicoFileType WasmFileSystem::getFileType(int index) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) {
    return PFT_UNKNOWN;
  }
  return entries_[static_cast<std::size_t>(index)].type;
}

bool WasmFileSystem::isParentRoot() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (cwd_ == root_) {
    return false;
  }
  return fs::path(cwd_).parent_path() == fs::path(root_);
}

bool WasmFileSystem::isCurrentRoot() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return cwd_ == root_;
}

bool WasmFileSystem::DeleteFile(const char *name) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string path;
  if (!Resolve(name, path)) {
    return false;
  }
  std::error_code error;
  const bool removed = fs::is_regular_file(path, error) && !error &&
                       fs::remove(path, error) && !error;
  if (removed) {
    WasmStorage_NotifyMutation();
  }
  return removed;
}

bool WasmFileSystem::DeleteDir(const char *name) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string path;
  if (!Resolve(name, path) || path == root_) {
    return false;
  }
  std::error_code error;
  const bool removed = fs::is_directory(path, error) && !error &&
                       fs::remove(path, error) && !error;
  if (removed) {
    WasmStorage_NotifyMutation();
  }
  return removed;
}

bool WasmFileSystem::exists(const char *path) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string resolved;
  if (!Resolve(path, resolved)) {
    return false;
  }
  std::error_code error;
  return fs::exists(resolved, error) && !error;
}

bool WasmFileSystem::makeDir(const char *path, bool pFlag) {
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
    WasmStorage_NotifyMutation();
  }
  return succeeded;
}

std::uint64_t WasmFileSystem::getFileSize(int index) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) {
    return 0;
  }
  return entries_[static_cast<std::size_t>(index)].size;
}

bool WasmFileSystem::CopyFile(const char *srcFilename,
                              const char *destFilename) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string source;
  std::string destination;
  if (!Resolve(srcFilename, source) || !Resolve(destFilename, destination)) {
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
    if (!createdParents.empty() && !RollbackCreatedDirectories(createdParents)) {
      WasmStorage_NotifyMutation();
    }
    return false;
  }
  error.clear();
  const bool copied = fs::copy_file(source, destination,
                                    fs::copy_options::overwrite_existing, error) &&
                      !error;
  if (!copied) {
    if (!createdParents.empty() && !RollbackCreatedDirectories(createdParents)) {
      WasmStorage_NotifyMutation();
    }
    return false;
  }
  WasmStorage_NotifyMutation();
  return true;
}

bool WasmFileSystem::MoveFile(const char *srcFilename,
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
    if (!createdParents.empty() && !RollbackCreatedDirectories(createdParents)) {
      WasmStorage_NotifyMutation();
    }
    return false;
  }
  error.clear();
  fs::rename(source, destination, error);
  if (error) {
    if (!createdParents.empty() && !RollbackCreatedDirectories(createdParents)) {
      WasmStorage_NotifyMutation();
    }
    return false;
  }
  WasmStorage_NotifyMutation();
  return true;
}

bool WasmFileSystem::isExFat() { return false; }
