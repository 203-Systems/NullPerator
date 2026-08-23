#!/usr/bin/env bash

set -euo pipefail

build_type="${1:-Release}"
case "$build_type" in
  Debug|Release|RelWithDebInfo) ;;
  *)
    echo "usage: $0 [Debug|Release|RelWithDebInfo]" >&2
    exit 2
    ;;
esac

if ! command -v emcmake >/dev/null 2>&1 || ! command -v em++ >/dev/null 2>&1; then
  echo "Emscripten is required. Activate emsdk before running this script." >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build-wasm/${build_type}"

emcmake cmake \
  -S "$repo_root/sources" \
  -B "$build_dir" \
  -DWASM=ON \
  -DCMAKE_BUILD_TYPE="$build_type"
# Keep local workbench builds predictable on developer machines. Callers that
# explicitly want more workers can opt in with CMAKE_BUILD_PARALLEL_LEVEL.
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-1}"
