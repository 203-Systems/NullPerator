#include "PathResolver.h"

#include <cstring>
#include <sstream>
#include <vector>

namespace {
constexpr const char *kMountPoint = "/sdcard";

bool HasMountPrefix(const char *path) {
  constexpr size_t kMountLength = 7;
  return std::strncmp(path, kMountPoint, kMountLength) == 0 &&
         (path[kMountLength] == '\0' || path[kMountLength] == '/');
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
} // namespace NodePath
