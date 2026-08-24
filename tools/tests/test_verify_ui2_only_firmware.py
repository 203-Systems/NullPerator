from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "verify_ui2_only_firmware.py"

GOOD_COMPILE_COMMANDS = [
    {
        "directory": "/repo/build/node",
        "file": "/repo/sources/Adapters/node/ui2/NodeUi2Platform.cpp",
        "command": "c++ -c NodeUi2Platform.cpp",
    },
    {
        "directory": "/repo/build/node",
        "file": "/repo/sources/Application/UI2/Ui2ApplicationRuntime.cpp",
        "command": "c++ -c Ui2ApplicationRuntime.cpp",
    },
    {
        "directory": "/repo/build/node",
        "file": "/repo/sources/UI2/UiEngine.cpp",
        "command": "c++ -c UiEngine.cpp",
    },
    {
        "directory": "/repo/build/node",
        "file": "/repo/sources/UI2/Render/UiRgb565Presenter.cpp",
        "command": "c++ -c UiRgb565Presenter.cpp",
    },
]

GOOD_LINK_MAP = """
LOAD /repo/build/node/libui2.a(UiEngine.cpp.obj)
LOAD /repo/build/node/libapplication_ui2.a(Ui2ApplicationRuntime.cpp.obj)
LOAD /repo/build/node/libplatform_ui2.a(NodeUi2Platform.cpp.obj)
LOAD /repo/build/node/libui2.a(UiRgb565Presenter.cpp.obj)
"""

GOOD_NM = """
40380000 T ui2::UiApplicationRuntime::Present(AppWindow&)
40380100 T ui2::UiRgb565Presenter::Present(ui2::UiIndexedSurface const&, ui2::UiPalette const&, std::span<ui2::DirtyStrip const>)
40380200 T display_draw_rgb565_region
"""


class Ui2OnlyFirmwareCheckerTest(unittest.TestCase):
    def run_checker(
        self,
        compile_commands=GOOD_COMPILE_COMMANDS,
        link_map=GOOD_LINK_MAP,
        nm_output=GOOD_NM,
        require_complete=True,
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arguments = [sys.executable, str(SCRIPT)]
            if require_complete:
                arguments.append("--require-complete")
            if compile_commands is not None:
                compile_path = root / "compile_commands.json"
                compile_path.write_text(json.dumps(compile_commands), encoding="utf-8")
                arguments.extend(("--compile-commands", str(compile_path)))
            if link_map is not None:
                map_path = root / "picoTracker.map"
                map_path.write_text(link_map, encoding="utf-8")
                arguments.extend(("--link-map", str(map_path)))
            if nm_output is not None:
                nm_path = root / "picoTracker.nm"
                nm_path.write_text(nm_output, encoding="utf-8")
                arguments.extend(("--nm-output", str(nm_path)))
            return subprocess.run(arguments, capture_output=True, text=True, check=False)

    def test_complete_ui2_only_evidence_passes(self) -> None:
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("passed (complete evidence)", result.stdout)

    def test_forbidden_legacy_source_fails(self) -> None:
        commands = GOOD_COMPILE_COMMANDS + [
            {
                "directory": "C:\\repo\\build\\node",
                "file": "C:\\repo\\sources\\Application\\Views\\SongView.cpp",
                "command": "c++ -c SongView.cpp",
            }
        ]
        result = self.run_checker(compile_commands=commands)
        self.assertEqual(result.returncode, 1)
        self.assertIn("forbidden legacy-view-sources", result.stderr)
        self.assertIn("SongView.cpp", result.stderr)

    def test_forbidden_legacy_product_shells_fail(self) -> None:
        commands = GOOD_COMPILE_COMMANDS + [
            {
                "directory": "/repo/build/node",
                "file": "/repo/sources/Application/Application.cpp",
                "command": "c++ -c Application.cpp",
            },
            {
                "directory": "/repo/build/node",
                "file": "/repo/sources/Adapters/node/gui/EventManager.cpp",
                "command": "c++ -c EventManager.cpp",
            },
            {
                "directory": "/repo/build/wasm",
                "file": "/repo/sources/Adapters/wasm/gui/WasmEventManager.cpp",
                "command": "c++ -c WasmEventManager.cpp",
            },
        ]
        result = self.run_checker(compile_commands=commands)
        self.assertEqual(result.returncode, 1)
        self.assertIn("forbidden legacy-application-shell", result.stderr)
        self.assertIn("forbidden legacy-node-gui", result.stderr)
        self.assertIn("forbidden legacy-wasm-event-loop", result.stderr)

    def test_forbidden_archive_and_object_fail(self) -> None:
        link_map = GOOD_LINK_MAP + """
LOAD /repo/build/node/libapplication_views.a(SongView.cpp.obj)
LOAD /repo/build/node/libplatform_gui.a(GUIWindowImp.cpp.obj)
LOAD /repo/build/node/libapplication_legacy_reference.a(Application.cpp.obj)
"""
        result = self.run_checker(link_map=link_map)
        self.assertEqual(result.returncode, 1)
        self.assertIn("forbidden legacy-view-archive", result.stderr)
        self.assertIn("forbidden legacy-node-window-object", result.stderr)
        self.assertIn("forbidden legacy-application-archive", result.stderr)

    def test_forbidden_defined_symbol_fails(self) -> None:
        nm_output = GOOD_NM + """
40381000 T AppWindow::Flush()
40381100 T display_putc
"""
        result = self.run_checker(nm_output=nm_output)
        self.assertEqual(result.returncode, 1)
        self.assertIn("forbidden legacy-app-window-rendering", result.stderr)
        self.assertIn("forbidden legacy-display-functions", result.stderr)

    def test_missing_required_ui2_evidence_fails_closed(self) -> None:
        commands = GOOD_COMPILE_COMMANDS[:-1]
        result = self.run_checker(compile_commands=commands)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing ui2-rgb565-presenter", result.stderr)

    def test_complete_mode_requires_all_three_artifact_types(self) -> None:
        result = self.run_checker(link_map=None, nm_output=None)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing linker map", result.stderr)
        self.assertIn("missing ELF or demangled", result.stderr)

    def test_partial_mode_is_explicit(self) -> None:
        result = self.run_checker(
            link_map=None, nm_output=None, require_complete=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("passed (partial evidence)", result.stdout)
        self.assertIn("release acceptance requires --require-complete", result.stderr)


if __name__ == "__main__":
    unittest.main()
