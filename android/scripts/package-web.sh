#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
output="${1:?Pass generated assets output directory}"
case "$output" in
  "$root/android/app/build/"*) ;;
  *) echo "Generated assets must be inside android/app/build" >&2; exit 1 ;;
esac
cd "$root/web"
if [[ ! -x node_modules/.bin/vite ]]; then
  pnpm install --frozen-lockfile
fi
pnpm exec vite build --base ./
rm -rf "$output/web"
mkdir -p "$output/web"
cp -R dist/. "$output/web/"
rm -rf "$output/web/wasm" "$output/web/oracle.html"
python3 - "$output/web/index.html" "$root/android/app/src/main/native-bootstrap.js" <<'PY'
import sys
from pathlib import Path
page=Path(sys.argv[1]); script=Path(sys.argv[2]).read_text()
page.write_text(page.read_text().replace('<head>', '<head><script>'+script+'</script>', 1))
PY
