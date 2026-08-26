#ifndef _NODE_PATH_RESOLVER_H_
#define _NODE_PATH_RESOLVER_H_

#include <optional>
#include <string>

namespace NodePath {
std::optional<std::string> Resolve(const std::string &cwd, const char *path);

// Checks an already lexically-resolved path without following symbolic links.
// Every existing component, including the mount point, must be a real
// directory (except for the final component, which may be a regular file).
// When allowMissingSuffix is true, a not-yet-created suffix is accepted after
// all of its existing ancestors have passed the no-symlink check.
//
// Keeping this helper independent of the fixed Node mount makes the security
// policy host-testable while the adapter continues to use /sdcard.
bool IsContainedWithoutSymlinks(const std::string &mountPoint,
                                const std::string &resolvedPath,
                                bool allowMissingSuffix);
}

#endif
