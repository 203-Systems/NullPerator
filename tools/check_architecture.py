#!/usr/bin/env python3
"""Check concrete platform dependencies without requiring a target SDK."""
from __future__ import annotations
import argparse
from pathlib import Path
import posixpath
import re

CORE = {"Application", "Services", "System", "Foundation", "UI2"}
ALLOWED = {
    "node": {"node", "common"},
    "ios": {"ios", "common", "posix"},
    "wasm": {"wasm", "common", "posix"},
    "common": {"common"},
    "posix": {"posix", "common"},
}
SDK = re.compile(r"^(esp[_/]|freertos/|emscripten[/.]|AudioToolbox/|UIKit/|CoreMIDI/)")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]')


def violations(path: str, content: str) -> list[str]:
    parts = path.split("/")
    shared = parts[0] in CORE
    owner = parts[1] if len(parts) > 1 and parts[0] == "Adapters" else None
    if owner and owner not in ALLOWED:
        return [f"{path}:1: declare the new adapter's dependencies in ALLOWED"]
    found = []
    for number, line in enumerate(content.splitlines(), 1):
        reason = None
        match = INCLUDE.match(line)
        if match:
            include = match[1]
            resolved = posixpath.normpath(posixpath.join(posixpath.dirname(path), include))
            target = include if include.startswith("Adapters/") else resolved
            adapter = re.match(r"Adapters/([^/]+)/", target)
            if shared and (adapter or SDK.match(include) or include == "platform.h"):
                reason = "shared code must use a System/Services contract, not a platform implementation"
            elif path.startswith("Services/Audio/") and (
                include.startswith("Application/") or resolved.startswith("Application/")
            ):
                reason = "audio services must receive application policy through a contract"
            elif owner and adapter and adapter[1] not in ALLOWED[owner]:
                reason = f"{owner} must not include the {adapter[1]} adapter"
            elif owner in {"common", "posix"} and SDK.match(include):
                reason = "shared adapters must not import a target SDK"
        if owner in {"common", "posix"} and re.search(
            r"\b(__EMSCRIPTEN__|ESP_PLATFORM|EM_JS|EMSCRIPTEN_KEEPALIVE|PicoTracker_Wasm_\w*)\b", line
        ):
            reason = "target bridges and target conditional code belong to that target's adapter"
        if path.endswith("CMakeLists.txt") and owner:
            references = re.findall(r"(?:Adapters/|\.\./)(node|wasm|ios|posix|common)/", line)
            if any(target not in ALLOWED[owner] for target in references):
                reason = "build source lists must respect adapter boundaries"
            if owner in {"ios", "wasm"} and re.search(r"(?:definitions|D).*\bNODE\b", line):
                reason = "host builds must not impersonate the Node hardware target"
        if reason:
            found.append(f"{path}:{number}: {reason}")
    return found


def check(root: Path) -> list[str]:
    errors = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if any(part in {"Externals", "managed_components", "build", "CMakeFiles"} for part in relative.parts):
            continue
        if path.is_file() and (path.suffix in {".h", ".cpp", ".cc", ".c"} or path.name == "CMakeLists.txt"):
            errors.extend(violations(relative.as_posix(), path.read_text()))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, default=Path(__file__).resolve().parents[1] / "sources")
    args = parser.parse_args()
    errors = check(args.sources)
    if errors:
        print("\n".join(errors))
        return 1
    print("Adapter boundaries: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
