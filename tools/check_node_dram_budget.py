#!/usr/bin/env python3
"""Budget Node's static data/BSS reservation, including linker padding.

This excludes instruction RAM (budgeted separately), dynamic allocations,
and task stacks. It is not a measurement of free runtime heap.
Exit codes: 0 within budget, 1 over budget, 2 invalid map/arguments.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional, Sequence

_SYMBOLS = ("_data_start", "_data_end", "_bss_start", "_bss_end")
_SYMBOL_RE = re.compile(
    r"^\s*(0[xX][0-9a-fA-F]+)\s+(_(?:data|bss)_(?:start|end))\s*="
)
_REGION_RE = re.compile(
    r"^dram0_0_seg\s+(0[xX][0-9a-fA-F]+)\s+(0[xX][0-9a-fA-F]+)\s"
)


@dataclass(frozen=True)
class DramLayout:
    data_start: int
    data_end: int
    bss_start: int
    bss_end: int

    @property
    def used_bytes(self) -> int:
        return self.bss_end - self.data_start


def parse_dram_layout(lines: Iterable[str]) -> DramLayout:
    addresses: dict[str, int] = {}
    region = None
    for line in lines:
        match = _REGION_RE.match(line)
        if match:
            if region is not None:
                raise ValueError("duplicate dram0_0_seg region")
            origin, length = (int(value, 0) for value in match.groups())
            region = (origin, origin + length)
        match = _SYMBOL_RE.match(line)
        if match:
            address, symbol = match.groups()
            if symbol in addresses:
                raise ValueError(f"duplicate linker symbol {symbol}")
            addresses[symbol] = int(address, 0)
    missing = [symbol for symbol in _SYMBOLS if symbol not in addresses]
    if missing:
        raise ValueError(f"missing linker symbol(s): {', '.join(missing)}")
    if region is None:
        raise ValueError("missing dram0_0_seg region")
    layout = DramLayout(*(addresses[symbol] for symbol in _SYMBOLS))
    if not (region[0] <= layout.data_start <= layout.data_end
            <= layout.bss_start <= layout.bss_end <= region[1]):
        raise ValueError("unordered static DRAM boundaries or outside dram0_0_seg")
    if layout.used_bytes <= 0:
        raise ValueError("empty static DRAM reservation")
    return layout


def positive_integer(value: str) -> int:
    try:
        result = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected an integer") from error
    if result <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return result


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-map", required=True, type=Path)
    parser.add_argument("--budget-bytes", required=True, type=positive_integer)
    arguments = parser.parse_args(argv)
    try:
        with arguments.link_map.open(encoding="utf-8", errors="replace") as stream:
            layout = parse_dram_layout(stream)
    except (OSError, ValueError) as error:
        print(f"Node static DRAM check failed: {error}", file=sys.stderr)
        return 2
    remaining = arguments.budget_bytes - layout.used_bytes
    passed = remaining >= 0
    print(
        f"Node static DRAM budget {'passed' if passed else 'exceeded'}: "
        f"{layout.used_bytes} / {arguments.budget_bytes} bytes "
        f"({abs(remaining)} bytes {'remaining' if passed else 'over'}; "
        f"data={layout.data_end - layout.data_start}, "
        f"bss={layout.bss_end - layout.bss_start}, "
        f"padding={layout.bss_start - layout.data_end})",
        file=sys.stdout if passed else sys.stderr,
    )
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
