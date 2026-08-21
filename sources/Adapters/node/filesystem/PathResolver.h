#ifndef _NODE_PATH_RESOLVER_H_
#define _NODE_PATH_RESOLVER_H_

#include <optional>
#include <string>

namespace NodePath {
std::optional<std::string> Resolve(const std::string &cwd, const char *path);
}

#endif
