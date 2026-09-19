#!/bin/bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
product_version_header="${1:?Pass the generated header path}"
product_version="$(sed -nE \
  's/.*inline constexpr char Version\[\] = "([^"]+)";.*/\1/p' \
  "$repo_root/sources/ProductVersion.h" | head -n 1)"
if [[ ! "$product_version" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
  echo "Invalid NullPerator product version: $product_version" >&2
  exit 1
fi
product_build="$(sed -nE \
  's/.*inline constexpr unsigned Build = ([0-9]+);.*/\1/p' \
  "$repo_root/sources/ProductVersion.h" | head -n 1)"
if [[ ! "$product_build" =~ ^[1-9][0-9]*$ ]] || (( product_build > 2100000000 )); then
  echo "Invalid NullPerator product build: $product_build" >&2
  exit 1
fi
mkdir -p "$(dirname "$product_version_header")"
product_version_temp="${product_version_header}.tmp"
printf '#define NULLPERATOR_IOS_PRODUCT_VERSION %s\n#define NULLPERATOR_IOS_PRODUCT_BUILD %s\n' \
  "$product_version" "$product_build" > "$product_version_temp"
if [[ -f "$product_version_header" ]] && \
    cmp -s "$product_version_temp" "$product_version_header"; then
  rm "$product_version_temp"
else
  mv "$product_version_temp" "$product_version_header"
fi
