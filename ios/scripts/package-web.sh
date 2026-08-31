#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
web_root="$repo_root/web"
destination="$repo_root/ios/NullPeratorIOS/Resources/WebApp"

if [[ ! -x "$web_root/node_modules/.bin/vite" ]]; then
  echo "Missing web dependencies. Run pnpm install in web/." >&2
  exit 1
fi

(cd "$web_root" && pnpm build)
rm -rf "$destination"
mkdir -p "$(dirname "$destination")"
cp -R "$web_root/dist" "$destination"
rm -rf "$destination/wasm" "$destination/oracle.html"
echo "Packaged WebApp at $destination"
