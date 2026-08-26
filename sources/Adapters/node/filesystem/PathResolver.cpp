#include "PathResolver.h"

#include <cstring>
#include <errno.h>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace {
constexpr const char *kMountPoint = "/sdcard";

bool HasMountPrefix(const char *path) {
  constexpr size_t kMountLength = 7;
  return std::strncmp(path, kMountPoint, kMountLength) == 0 &&
         (path[kMountLength] == '\0' || path[kMountLength] == '/');
}

int NoFollowStat(const char *path, struct stat *state) {
#if defined(ESP_PLATFORM)
  // ESP-IDF's FAT VFS has no lstat and the on-device filesystem has no
  // symbolic-link type. Desktop/POSIX builds use lstat below.
  return stat(path, state);
#else
  return lstat(path, state);
#endif
}
} // namespace

namespace NodePath {
std::optional<std::string> Resolve(const std::string &cwd, const char *path) {
  std::string combined;
  if (path == nullptr) {
    combined = cwd;
  } else if (path[0] == '/') {
    combined = HasMountPrefix(path) ? path : std::string(kMountPoint) + path;
  } else {
    combined = cwd;
    if (!combined.empty() && combined.back() != '/') {
      combined.push_back('/');
    }
    combined += path;
  }

  if (!HasMountPrefix(combined.c_str())) {
    return std::nullopt;
  }

  std::vector<std::string> components;
  std::stringstream stream(combined.substr(std::strlen(kMountPoint)));
  std::string component;
  while (std::getline(stream, component, '/')) {
    if (component.empty() || component == ".") {
      continue;
    }
    if (component == "..") {
      if (components.empty()) {
        return std::nullopt;
      }
      components.pop_back();
      continue;
    }
    components.push_back(component);
  }

  std::string resolved = kMountPoint;
  for (const std::string &part : components) {
    resolved.push_back('/');
    resolved += part;
  }
  return resolved;
}

bool IsContainedWithoutSymlinks(const std::string &mountPoint,
                                const std::string &resolvedPath,
                                bool allowMissingSuffix) {
  if (mountPoint.empty() || mountPoint.front() != '/' ||
      resolvedPath.size() < mountPoint.size() ||
      resolvedPath.compare(0, mountPoint.size(), mountPoint) != 0 ||
      (resolvedPath.size() != mountPoint.size() &&
       resolvedPath[mountPoint.size()] != '/')) {
    return false;
  }

  struct stat state {};
  if (NoFollowStat(mountPoint.c_str(), &state) != 0 ||
      S_ISLNK(state.st_mode) || !S_ISDIR(state.st_mode)) {
    return false;
  }

  std::string current = mountPoint;
  std::stringstream stream(resolvedPath.substr(mountPoint.size()));
  std::string component;
  while (std::getline(stream, component, '/')) {
    if (component.empty()) {
      continue;
    }
    current.push_back('/');
    current += component;

    if (NoFollowStat(current.c_str(), &state) == 0) {
      if (S_ISLNK(state.st_mode)) {
        return false;
      }
      const bool isFinal = current.size() == resolvedPath.size();
      if (!isFinal && !S_ISDIR(state.st_mode)) {
        return false;
      }
      continue;
    }

    if (errno != ENOENT || !allowMissingSuffix) {
      return false;
    }

    // The first missing component makes the remainder a purely lexical suffix
    // below a previously verified real directory. The caller may create it.
    return true;
  }
  return true;
}
} // namespace NodePath
