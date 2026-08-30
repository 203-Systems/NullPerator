#include "PathResolver.h"

#include <cstring>
#include <errno.h>
#include <string_view>
#include <sys/stat.h>

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

bool NextPathComponent(std::string_view path, std::size_t &cursor,
                       std::string_view &component) {
  while (cursor < path.size() && path[cursor] == '/')
    ++cursor;
  if (cursor == path.size())
    return false;

  const std::size_t begin = cursor;
  while (cursor < path.size() && path[cursor] != '/')
    ++cursor;
  component = path.substr(begin, cursor - begin);
  return true;
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

  std::string resolved = kMountPoint;
  const std::string_view suffix(combined.data() + std::strlen(kMountPoint),
                                combined.size() - std::strlen(kMountPoint));
  std::size_t cursor = 0;
  std::string_view component;
  while (NextPathComponent(suffix, cursor, component)) {
    if (component == ".") {
      continue;
    }
    if (component == "..") {
      if (resolved.size() == std::strlen(kMountPoint)) {
        return std::nullopt;
      }
      resolved.resize(resolved.find_last_of('/'));
      continue;
    }
    resolved.push_back('/');
    resolved.append(component.data(), component.size());
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
  const std::string_view suffix(resolvedPath.data() + mountPoint.size(),
                                resolvedPath.size() - mountPoint.size());
  std::size_t cursor = 0;
  std::string_view component;
  while (NextPathComponent(suffix, cursor, component)) {
    current.push_back('/');
    current.append(component.data(), component.size());

    if (NoFollowStat(current.c_str(), &state) == 0) {
      if (S_ISLNK(state.st_mode)) {
        return false;
      }
      std::size_t nextCursor = cursor;
      std::string_view nextComponent;
      const bool isFinal =
          !NextPathComponent(suffix, nextCursor, nextComponent);
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
