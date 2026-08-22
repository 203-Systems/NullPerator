/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmFileSystem.h"

#include "WasmFile.h"
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

bool WasmFileSystem::EnsureParentDirectories(const std::string &path) {
  const fs::path parent = fs::path(path).parent_path();
  if (parent.empty()) {
    return true;
  }
  std::error_code error;
  fs::create_directories(parent, error);
  return !error;
}

FileHandle WasmFileSystem::Open(const char *name, const char *mode) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (mode == nullptr) {
    return FileHandle();
  }
  std::string path;
  if (!Resolve(name, path)) {
    return FileHandle();
  }
  if ((std::strchr(mode, 'w') != nullptr || std::strchr(mode, 'a') != nullptr ||
       std::strchr(mode, '+') != nullptr) &&
      !EnsureParentDirectories(path)) {
    Trace::Error("WASM_FILESYSTEM", "cannot create parent directories: %s",
                 path.c_str());
    return FileHandle();
  }
  std::FILE *file = std::fopen(path.c_str(), mode);
  if (file == nullptr) {
    Trace::Error("WASM_FILESYSTEM", "open failed: %s (%s)", path.c_str(),
                 std::strerror(errno));
    return FileHandle();
  }
  return MakeFileHandle(new WasmFile(file));
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
  return fs::is_regular_file(path, error) && !error && fs::remove(path, error) &&
         !error;
}

bool WasmFileSystem::DeleteDir(const char *name) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string path;
  if (!Resolve(name, path) || path == root_) {
    return false;
  }
  std::error_code error;
  return fs::is_directory(path, error) && !error && fs::remove(path, error) &&
         !error;
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
  return !error && (created || fs::is_directory(resolved));
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
  if (!Resolve(srcFilename, source) || !Resolve(destFilename, destination) ||
      !EnsureParentDirectories(destination)) {
    return false;
  }
  std::error_code error;
  return fs::copy_file(source, destination, fs::copy_options::overwrite_existing,
                       error) &&
         !error;
}

bool WasmFileSystem::MoveFile(const char *srcFilename,
                              const char *destFilename) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string source;
  std::string destination;
  if (!Resolve(srcFilename, source) || !Resolve(destFilename, destination) ||
      !EnsureParentDirectories(destination)) {
    return false;
  }
  std::error_code error;
  fs::rename(source, destination, error);
  return !error;
}

bool WasmFileSystem::isExFat() { return false; }
