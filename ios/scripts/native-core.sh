#!/bin/bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
platform_name="${PLATFORM_NAME:-iphonesimulator}"
arch_name="${CURRENT_ARCH:-}"
if [[ -z "$arch_name" || "$arch_name" == "undefined_arch" ]]; then
  arch_name="${NATIVE_ARCH_ACTUAL:-${ARCHS:-arm64}}"
  arch_name="${arch_name%% *}"
fi
configuration="${CONFIGURATION:-Debug}"
build_root="$repo_root/ios/.build/native-core/$platform_name-$arch_name"
archive="${NULLPERATOR_NATIVE_CORE_OUTPUT:-$build_root/libNullPeratorIOSCore.a}"
mkdir -p "$(dirname "$archive")"

product_version_header="${NULLPERATOR_PRODUCT_VERSION_HEADER:-}"
if [[ -n "$product_version_header" ]]; then
  product_version="$(sed -nE \
    's/.*inline constexpr char Version\[\] = "([^"]+)";.*/\1/p' \
    "$repo_root/sources/ProductVersion.h" | head -n 1)"
  if [[ ! "$product_version" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
    echo "Invalid NullPerator product version: $product_version" >&2
    exit 1
  fi
  mkdir -p "$(dirname "$product_version_header")"
  product_version_temp="${product_version_header}.tmp"
  printf '#define NULLPERATOR_IOS_PRODUCT_VERSION %s\n' \
    "$product_version" > "$product_version_temp"
  if [[ -f "$product_version_header" ]] && \
      cmp -s "$product_version_temp" "$product_version_header"; then
    rm "$product_version_temp"
  else
    mv "$product_version_temp" "$product_version_header"
  fi
fi

if [[ "$platform_name" == *simulator* ]]; then
  sdk="iphonesimulator"
else
  sdk="iphoneos"
fi

cmake -S "$repo_root/sources" -B "$build_root/cmake" -G Xcode \
  -DIOS=true \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT="$sdk" \
  -DCMAKE_OSX_ARCHITECTURES="$arch_name" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${IPHONEOS_DEPLOYMENT_TARGET:-17.0}"

cmake --build "$build_root/cmake" \
  --config "$configuration" \
  --target nullperator_ios_core

libraries=()
archive_manifest="$build_root/cmake/native-core-archives-$configuration.txt"
if [[ ! -s "$archive_manifest" ]]; then
  echo "Native core archive manifest is missing: $archive_manifest" >&2
  exit 1
fi
while IFS= read -r library; do
  # Xcode preserves this build-setting token in CMake's generated target path.
  effective_platform_token='${EFFECTIVE_PLATFORM_NAME}'
  library="${library//$effective_platform_token/-$platform_name}"
  if [[ ! -f "$library" ]]; then
    echo "Native core archive was not built: $library" >&2
    exit 1
  fi
  libraries+=("$library")
done < "$archive_manifest"
if (( ${#libraries[@]} == 0 )); then
  echo "No native core archives were produced" >&2
  exit 1
fi
xcrun libtool -static -o "$archive" "${libraries[@]}"
echo "$archive"
