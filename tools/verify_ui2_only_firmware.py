#!/usr/bin/env python3
"""Verify that a firmware artifact is a strict UI2-only build.

The checker deliberately inspects three independent build products:

* compile_commands.json proves forbidden sources were not compiled;
* the linker map proves forbidden archives/objects were not selected; and
* demangled, defined ELF symbols prove legacy rendering code is absent even
  when it arrived through a renamed or prebuilt archive.

The product build is UI2-only by default.  This script is the release gate that
proves an opt-in legacy-reference build did not leak into a product artifact.
"""

from __future__ import annotations

import argparse
import json
import os
import posixpath
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


@dataclass(frozen=True)
class Rule:
    key: str
    pattern: re.Pattern[str]
    description: str


def _rule(key: str, pattern: str, description: str) -> Rule:
    return Rule(key, re.compile(pattern), description)


# Strict product contract.  Controller/state code must move out of these mixed
# legacy translation units before a UI2-only build can pass.
FORBIDDEN_COMPILE_RULES: tuple[Rule, ...] = (
    _rule(
        "legacy-view-sources",
        r"(?:^|/)(?:sources/)?Application/Views/",
        "legacy View, FieldView, and modal implementation sources",
    ),
    _rule(
        "legacy-ui-framework-sources",
        r"(?:^|/)(?:sources/)?UIFramework/",
        "legacy GUIWindow and graphics framework sources",
    ),
    _rule(
        "mixed-app-window",
        r"(?:^|/)(?:sources/)?Application/AppWindow\.cpp$",
        "mixed AppWindow controller and character renderer",
    ),
    _rule(
        "legacy-application-shell",
        r"(?:^|/)(?:sources/)?Application/Application\.cpp$",
        "legacy Application/AppWindow lifecycle shell",
    ),
    _rule(
        "legacy-node-gui",
        r"(?:^|/)(?:sources/)?Adapters/node/gui/",
        "Node legacy GUI factory, event loop, and character window",
    ),
    _rule(
        "mixed-node-display",
        r"(?:^|/)(?:sources/)?Adapters/node/display/display\.c$",
        "mixed character renderer and RGB565 panel transport",
    ),
    _rule(
        "legacy-remote-ui-protocol",
        r"(?:^|/)(?:sources/)?System/RemoteUI/",
        "legacy character Remote UI protocol",
    ),
    _rule(
        "legacy-wasm-gui-factory",
        r"(?:^|/)(?:sources/)?Adapters/wasm/gui/GUIFactory\.cpp$",
        "WASM legacy GUI factory",
    ),
    _rule(
        "legacy-wasm-event-loop",
        r"(?:^|/)(?:sources/)?Adapters/wasm/gui/WasmEventManager\.cpp$",
        "WASM legacy EventManager/AppWindow loop",
    ),
    _rule(
        "legacy-wasm-window",
        r"(?:^|/)(?:sources/)?Adapters/wasm/gui/WasmGUIWindowImp\.cpp$",
        "WASM mixed character/UI2 window",
    ),
)

REQUIRED_COMPILE_RULES: tuple[Rule, ...] = (
    _rule(
        "ui2-node-platform",
        r"(?:^|/)(?:sources/)?Adapters/node/ui2/NodeUi2Platform\.cpp$",
        "ESP32 UI2-native input/display loop",
    ),
    _rule(
        "ui2-application-runtime",
        r"(?:^|/)(?:sources/)?Application/UI2/Ui2ApplicationRuntime\.cpp$",
        "shared UI2 application runtime",
    ),
    _rule(
        "ui2-engine",
        r"(?:^|/)(?:sources/)?UI2/UiEngine\.cpp$",
        "shared UI2 engine",
    ),
    _rule(
        "ui2-rgb565-presenter",
        r"(?:^|/)(?:sources/)?UI2/Render/UiRgb565Presenter\.cpp$",
        "ESP32 RGB565 presenter",
    ),
)

FORBIDDEN_MAP_RULES: tuple[Rule, ...] = (
    _rule(
        "legacy-view-archive",
        r"(?:^|[/\\])libapplication_views\.a(?:\(|\s|$)",
        "legacy concrete View archive",
    ),
    _rule(
        "legacy-view-baseclasses-archive",
        r"(?:^|[/\\])libapplication_views_baseclasses\.a(?:\(|\s|$)",
        "legacy View/Field base-class archive",
    ),
    _rule(
        "legacy-modal-archive",
        r"(?:^|[/\\])libapplication_views_modaldialogs\.a(?:\(|\s|$)",
        "legacy modal archive",
    ),
    _rule(
        "legacy-ui-framework-archive",
        r"(?:^|[/\\])libuiframework_[A-Za-z0-9_]+\.a(?:\(|\s|$)",
        "legacy UIFramework archive",
    ),
    _rule(
        "legacy-application-archive",
        r"(?:^|[/\\])libapplication_legacy_reference\.a(?:\(|\s|$)",
        "opt-in legacy Application/AppWindow reference archive",
    ),
    _rule(
        "legacy-platform-archive",
        r"(?:^|[/\\])libplatform_legacy_reference\.a(?:\(|\s|$)",
        "opt-in legacy platform GUI/display reference archive",
    ),
    _rule(
        "legacy-application-object",
        r"(?:^|[/\\(])Application\.cpp\.(?:o|obj)(?:\)|\s|$)",
        "legacy Application lifecycle object",
    ),
    _rule(
        "mixed-app-window-object",
        r"(?:^|[/\\(])AppWindow\.cpp\.(?:o|obj)(?:\)|\s|$)",
        "mixed AppWindow object",
    ),
    _rule(
        "legacy-node-window-object",
        r"(?:^|[/\\(])GUIWindowImp\.cpp\.(?:o|obj)(?:\)|\s|$)",
        "Node legacy GUI window object",
    ),
    _rule(
        "mixed-node-display-object",
        r"(?:^|[/\\(])display\.c\.(?:o|obj)(?:\)|\s|$)",
        "mixed Node character/display transport object",
    ),
    _rule(
        "legacy-remote-ui-archive",
        r"(?:^|[/\\])libremote_ui\.a(?:\(|\s|$)",
        "legacy character Remote UI protocol archive",
    ),
    _rule(
        "legacy-remote-ui-object",
        r"(?:^|[/\\(])RemoteUIProtocol\.cpp\.(?:o|obj)(?:\)|\s|$)",
        "legacy character Remote UI protocol object",
    ),
    _rule(
        "legacy-wasm-event-loop-object",
        r"(?:^|[/\\(])WasmEventManager\.cpp\.(?:o|obj)(?:\)|\s|$)",
        "WASM legacy EventManager/AppWindow loop object",
    ),
    _rule(
        "legacy-wasm-window-object",
        r"(?:^|[/\\(])WasmGUIWindowImp\.cpp\.(?:o|obj)(?:\)|\s|$)",
        "WASM mixed character/UI2 window object",
    ),
)

REQUIRED_MAP_RULES: tuple[Rule, ...] = (
    _rule("ui2-archive", r"(?:^|[/\\])libui2\.a(?:\(|\s|$)", "UI2 archive"),
    _rule(
        "ui2-application-archive",
        r"(?:^|[/\\])libapplication_ui2\.a(?:\(|\s|$)",
        "native UI2 application archive",
    ),
    _rule(
        "ui2-platform-archive",
        r"(?:^|[/\\])libplatform_ui2\.a(?:\(|\s|$)",
        "platform-native UI2 loop/presenter archive",
    ),
    _rule(
        "ui2-runtime-object",
        r"(?:^|[/\\(])Ui2ApplicationRuntime\.cpp\.(?:o|obj)(?:\)|\s|$)",
        "UI2 application runtime object",
    ),
    _rule(
        "ui2-engine-object",
        r"(?:^|[/\\(])UiEngine\.cpp\.(?:o|obj)(?:\)|\s|$)",
        "UI2 engine object",
    ),
    _rule(
        "ui2-rgb565-object",
        r"(?:^|[/\\(])UiRgb565Presenter\.cpp\.(?:o|obj)(?:\)|\s|$)",
        "UI2 RGB565 presenter object",
    ),
)

FORBIDDEN_SYMBOL_RULES: tuple[Rule, ...] = (
    _rule(
        "legacy-app-window-buffers",
        r"\bAppWindow::_(?:charScreen|charScreenProp|preScreen|preScreenProp)\b",
        "legacy character-frame buffers",
    ),
    _rule(
        "legacy-app-window-rendering",
        r"\bAppWindow::(?:Flush|DrawString|ClearTextRect|InvalidateTextCache)\(",
        "legacy AppWindow rendering methods",
    ),
    _rule(
        "legacy-application-shell",
        r"\bApplication::(?:Init|Quit|GetWindow|GetInstance)\(",
        "legacy Application/AppWindow lifecycle shell",
    ),
    _rule(
        "legacy-view-rendering",
        r"\b(?:SongView|ChainView|PhraseView|TableView|InstrumentView|"
        r"ProjectView|DeviceView|ThemeView|GrooveView|MixerView|ImportView|"
        r"InstrumentImportView|ThemeImportView|SelectProjectView|RecordView|"
        r"SampleEditorView|SampleSlicesView|FieldView|ScreenView|View)::"
        r"(?:DrawView|Redraw|DrawString|DrawRect|Clear|ForceClear|AnimationUpdate)\(",
        "legacy concrete/base View rendering methods",
    ),
    _rule(
        "legacy-modal-classes",
        r"\b(?:ModalView|MessageBox|FullScreenBox|TextInputModalView|"
        r"RenameModalView|RenderProgressModal)::",
        "legacy modal classes",
    ),
    _rule(
        "legacy-field-classes",
        r"\b(?:UIField|UIIntField|UIIntVarField|UIIntVarOffField|"
        r"UINoteVarField|UIStaticField|UITempoField|UISwatchField|"
        r"UIBitmaskVarField|UIActionField|UIBigHexVarField)::",
        "legacy character-grid field classes",
    ),
    _rule(
        "legacy-node-window-rendering",
        r"\bNodeGUIWindowImp::(?:DrawChar|DrawString|ClearTextRect|Flush|"
        r"RestoreLegacyFrame|SetColor)\(",
        "Node legacy window/fallback rendering methods",
    ),
    _rule(
        "legacy-display-functions",
        r"\bdisplay_(?:clear|set_foreground|set_background|set_font_index|"
        r"set_cursor|putc|print|draw_region|draw_sub_region|draw_changed|"
        r"draw_screen|set_palette_color|fill_rect)\b",
        "Node character-mode display functions",
    ),
)

REQUIRED_SYMBOL_RULES: tuple[Rule, ...] = (
    _rule(
        "ui2-runtime-present",
        r"\bui2::UiApplicationRuntime::Present\(",
        "UI2 application runtime presenter entrypoint",
    ),
    _rule(
        "ui2-rgb565-present",
        r"\bui2::UiRgb565Presenter::Present\(",
        "UI2 RGB565 presenter entrypoint",
    ),
    _rule(
        "node-rgb565-transport",
        r"\bdisplay_draw_rgb565_region\b",
        "Node direct RGB565 panel transport",
    ),
)


def _normalize_path(value: str) -> str:
    normalized = value.replace("\\", "/")
    return posixpath.normpath(normalized)


def _is_absolute_path(value: str) -> bool:
    return value.startswith("/") or re.match(r"^[A-Za-z]:/", value) is not None


def _load_compile_sources(path: Path) -> list[str]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read compile database {path}: {error}") from error
    if not isinstance(payload, list):
        raise ValueError(f"compile database {path} must contain a JSON array")

    sources: list[str] = []
    for index, entry in enumerate(payload):
        if not isinstance(entry, dict) or not isinstance(entry.get("file"), str):
            raise ValueError(
                f"compile database {path} entry {index} has no string 'file'"
            )
        source = _normalize_path(entry["file"])
        directory = entry.get("directory")
        if not _is_absolute_path(source) and isinstance(directory, str):
            source = _normalize_path(f"{directory}/{source}")
        sources.append(source)
    return sources


def _read_text(path: Path, label: str) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise ValueError(f"cannot read {label} {path}: {error}") from error


def _run_nm(elf: Path, nm_tool: str) -> str:
    try:
        result = subprocess.run(
            [nm_tool, "-C", "--defined-only", str(elf)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        raise ValueError(f"cannot execute nm tool {nm_tool}: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic"
        raise ValueError(
            f"nm tool {nm_tool} failed for {elf} ({result.returncode}): {detail}"
        )
    return result.stdout


def _check_values(
    label: str,
    values: Sequence[str],
    forbidden: Sequence[Rule],
    required: Sequence[Rule],
) -> list[str]:
    failures: list[str] = []
    for rule in forbidden:
        matches = [value for value in values if rule.pattern.search(value)]
        if matches:
            evidence = "; ".join(match.strip()[:240] for match in matches[:3])
            if len(matches) > 3:
                evidence += f"; ... ({len(matches) - 3} more)"
            failures.append(
                f"[{label}] forbidden {rule.key}: {rule.description}: {evidence}"
            )
    for rule in required:
        if not any(rule.pattern.search(value) for value in values):
            failures.append(
                f"[{label}] missing {rule.key}: {rule.description}"
            )
    return failures


def _print_rule_group(title: str, rules: Sequence[Rule]) -> None:
    print(title)
    for rule in rules:
        print(f"  {rule.key}: {rule.description}")
        print(f"    regex: {rule.pattern.pattern}")


def print_contract() -> None:
    _print_rule_group("Forbidden compile sources", FORBIDDEN_COMPILE_RULES)
    _print_rule_group("Required compile sources", REQUIRED_COMPILE_RULES)
    _print_rule_group("Forbidden link-map entries", FORBIDDEN_MAP_RULES)
    _print_rule_group("Required link-map entries", REQUIRED_MAP_RULES)
    _print_rule_group("Forbidden defined symbols", FORBIDDEN_SYMBOL_RULES)
    _print_rule_group("Required defined symbols", REQUIRED_SYMBOL_RULES)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="verify a clean firmware build contains UI2 and no legacy UI"
    )
    parser.add_argument("--compile-commands", type=Path)
    parser.add_argument("--link-map", type=Path)
    symbols = parser.add_mutually_exclusive_group()
    symbols.add_argument(
        "--nm-output",
        type=Path,
        help="text produced by nm -C --defined-only",
    )
    symbols.add_argument("--elf", type=Path, help="firmware ELF to inspect with nm")
    parser.add_argument(
        "--nm-tool",
        default=os.environ.get("NM", "xtensa-esp32s3-elf-nm"),
        help="nm executable used with --elf",
    )
    parser.add_argument(
        "--require-complete",
        action="store_true",
        help="require compile database, link map, and ELF/nm evidence",
    )
    parser.add_argument(
        "--print-contract",
        action="store_true",
        help="print the forbidden and required rule lists",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if args.print_contract:
        print_contract()

    has_symbols = args.nm_output is not None or args.elf is not None
    provided_count = sum(
        (args.compile_commands is not None, args.link_map is not None, has_symbols)
    )
    if provided_count == 0:
        if args.print_contract:
            return 0
        print("UI2-only acceptance input error: no build artifacts supplied", file=sys.stderr)
        return 2

    failures: list[str] = []
    if args.require_complete:
        if args.compile_commands is None:
            failures.append("[input] missing compile_commands.json")
        if args.link_map is None:
            failures.append("[input] missing linker map")
        if not has_symbols:
            failures.append("[input] missing ELF or demangled defined-symbol output")

    try:
        if args.compile_commands is not None:
            sources = _load_compile_sources(args.compile_commands)
            failures.extend(
                _check_values(
                    "compile",
                    sources,
                    FORBIDDEN_COMPILE_RULES,
                    REQUIRED_COMPILE_RULES,
                )
            )
        if args.link_map is not None:
            link_map = _read_text(args.link_map, "linker map")
            failures.extend(
                _check_values(
                    "link-map",
                    link_map.splitlines(),
                    FORBIDDEN_MAP_RULES,
                    REQUIRED_MAP_RULES,
                )
            )
        if args.nm_output is not None:
            symbols_text = _read_text(args.nm_output, "nm output")
            failures.extend(
                _check_values(
                    "symbols",
                    symbols_text.splitlines(),
                    FORBIDDEN_SYMBOL_RULES,
                    REQUIRED_SYMBOL_RULES,
                )
            )
        elif args.elf is not None:
            symbols_text = _run_nm(args.elf, args.nm_tool)
            failures.extend(
                _check_values(
                    "symbols",
                    symbols_text.splitlines(),
                    FORBIDDEN_SYMBOL_RULES,
                    REQUIRED_SYMBOL_RULES,
                )
            )
    except ValueError as error:
        print(f"UI2-only acceptance input error: {error}", file=sys.stderr)
        return 2

    if failures:
        print("UI2-only firmware acceptance FAILED", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    scope = "complete" if provided_count == 3 else "partial"
    print(f"UI2-only firmware acceptance passed ({scope} evidence)")
    if scope == "partial":
        print("warning: release acceptance requires --require-complete", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
