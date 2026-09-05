from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "check_node_dram_budget.py"
MAP = """
dram0_0_seg 0x3fc88000 0x53700 rw
 0x3fc98800 _data_start = ABSOLUTE (.)
 0x3fc9f568 _data_end = ABSOLUTE (.)
 0x3fc9f580 _bss_start = ABSOLUTE (.)
 0x3fcba158 _bss_end = ABSOLUTE (.)
"""


class NodeDramBudgetCheckerTest(unittest.TestCase):
    def run_checker(self, text=MAP, budget=137560):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.map"
            path.write_text(text)
            return subprocess.run(
                [sys.executable, str(SCRIPT), "--link-map", str(path),
                 "--budget-bytes", str(budget)],
                capture_output=True, text=True, check=False,
            )

    def test_alignment_and_exact_boundary(self):
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("137560 / 137560 bytes", result.stdout)
        self.assertIn("padding=24", result.stdout)
        self.assertIn("0 bytes remaining", result.stdout)

    def test_over_budget(self):
        result = self.run_checker(budget=137559)
        self.assertEqual(result.returncode, 1)
        self.assertIn("1 bytes over", result.stderr)

    def test_missing_symbol(self):
        result = self.run_checker(MAP.replace("_bss_end", "_unrelated_end"))
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing linker symbol", result.stderr)

    def test_duplicate_symbol(self):
        result = self.run_checker(MAP + "0x3fcba158 _bss_end = .\n")
        self.assertEqual(result.returncode, 2)
        self.assertIn("duplicate linker symbol", result.stderr)

    def test_reversed_or_overlapping_sections(self):
        for old, new in [("0x3fc98800 _data_start", "0x3fcba159 _data_start"),
                         ("0x3fc9f580 _bss_start", "0x3fc9f560 _bss_start")]:
            with self.subTest(old=old):
                self.assertEqual(self.run_checker(MAP.replace(old, new)).returncode, 2)

    def test_outside_region(self):
        self.assertEqual(self.run_checker(MAP.replace("0x3fcba158", "0x50000000")).returncode, 2)

    def test_missing_or_duplicate_region(self):
        for text in [MAP.replace("dram0_0_seg", "other_seg"),
                     MAP + "dram0_0_seg 0x3fc88000 0x53700 rw\n"]:
            self.assertEqual(self.run_checker(text).returncode, 2)

    def test_invalid_budget(self):
        for budget in [0, -1, "nonsense"]:
            self.assertEqual(self.run_checker(budget=budget).returncode, 2)
