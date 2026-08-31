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
while IFS= read -r library; do
  libraries+=("$library")
done < <(find "$build_root/cmake" -type f -name '*.a' \
  ! -path "$archive" -print | sort)
if (( ${#libraries[@]} == 0 )); then
  echo "No native core archives were produced" >&2
  exit 1
fi
xcrun libtool -static -o "$archive" "${libraries[@]}"
echo "$archive"
