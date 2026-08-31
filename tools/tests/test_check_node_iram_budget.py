from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "check_node_iram_budget.py"
IRAM_START = 0x40374000
PRODUCT_BUDGET_BYTES = 96 * 1024


def linker_map(iram_end: int, extra_lines: str = "") -> str:
    return f"""
Memory Configuration

Name             Origin             Length             Attributes
iram0_0_seg      0x40374000         0x00057700         xr

.iram0.vectors  0x40374000      0x403
                0x40374000                        _iram_start = ABSOLUTE (.)

.iram0.text     0x40374404    0x143db
{extra_lines}
                0x{iram_end:x}                        _iram_end = ABSOLUTE (.)
"""


class NodeIramBudgetCheckerTest(unittest.TestCase):
    def run_checker(
        self, map_text: str, budget: int = PRODUCT_BUDGET_BYTES
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            map_path = Path(directory) / "picoTracker.map"
            map_path.write_text(map_text, encoding="utf-8")
            return subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--link-map",
                    str(map_path),
                    "--budget-bytes",
                    str(budget),
                ],
                capture_output=True,
                text=True,
                check=False,
            )

    def test_complete_span_including_linker_alignment_passes(self) -> None:
        result = self.run_checker(linker_map(0x40388800))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("83968 / 98304 bytes", result.stdout)
        self.assertIn("14336 bytes remaining", result.stdout)

    def test_exact_budget_boundary_passes(self) -> None:
        result = self.run_checker(linker_map(IRAM_START + PRODUCT_BUDGET_BYTES))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0 bytes remaining", result.stdout)

    def test_one_byte_over_budget_fails(self) -> None:
        result = self.run_checker(
            linker_map(IRAM_START + PRODUCT_BUDGET_BYTES + 1)
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("Node IRAM budget exceeded", result.stderr)
        self.assertIn("1 bytes over", result.stderr)

    def test_missing_symbol_fails_closed(self) -> None:
        result = self.run_checker(
            "0x40374000 _iram_start = ABSOLUTE (.)\n"
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing linker symbol(s): _iram_end", result.stderr)

    def test_duplicate_symbol_fails_closed(self) -> None:
        result = self.run_checker(
            linker_map(
                0x40388800,
                "0x40374000 _iram_start = ABSOLUTE (.)",
            )
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("duplicate linker symbol _iram_start", result.stderr)

    def test_reversed_span_fails_closed(self) -> None:
        result = self.run_checker(linker_map(IRAM_START - 4))
        self.assertEqual(result.returncode, 2)
        self.assertIn("invalid IRAM span", result.stderr)


if __name__ == "__main__":
    unittest.main()
