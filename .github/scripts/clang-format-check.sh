#!/usr/bin/env bash

set -euo pipefail

CLANG_FORMAT_BIN="${CLANG_FORMAT_BIN:-clang-format-17}"

if ! command -v "${CLANG_FORMAT_BIN}" >/dev/null 2>&1; then
  echo "error: ${CLANG_FORMAT_BIN} not found in PATH"
  exit 1
fi

mapfile -d '' files < <(
  git ls-files -z -- sources \
    ':!:sources/Externals/**' \
    ':!:sources/Adapters/node/managed_components/**' |
    while IFS= read -r -d '' path; do
      [[ -f "$path" ]] || continue
      case "$path" in
        *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*.proto)
          printf '%s\0' "$path" ;;
      esac
    done
)

if [ "${#files[@]}" -eq 0 ]; then
  echo "No matching source files found under sources/"
  exit 0
fi

echo "Checking ${#files[@]} files with ${CLANG_FORMAT_BIN}..."
"${CLANG_FORMAT_BIN}" \
  --style=file \
  --fallback-style=llvm \
  --dry-run \
  --Werror \
  "${files[@]}"
