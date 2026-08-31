#!/usr/bin/env python3
"""Enforce the ESP32-S3 Node firmware instruction-RAM span budget.

ESP-IDF's size summary reports the ESP32-S3's first dedicated 16 KiB as
``IRAM`` and reports instruction bytes beyond that boundary as ``DIRAM``.
Once code crosses the boundary, the displayed IRAM remainder is therefore not
a useful regression metric.  The linker-defined ``_iram_start`` and
``_iram_end`` symbols cover the complete instruction-RAM reservation, including
vectors, shared DIRAM, and linker alignment.  This checker budgets that span.

Exit status is 0 when the artifact is within budget, 1 when it exceeds the
budget, and 2 when the map or command line cannot be validated.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional, Sequence


_LINKER_SYMBOL_RE = re.compile(
    r"^\s*(0[xX][0-9A-Fa-f]+)\s+(_iram_(?:start|end))\s*="
)


class MapParseError(ValueError):
    """Raised when a linker map cannot prove a single valid IRAM span."""


@dataclass(frozen=True)
class IramLayout:
    start: int
    end: int

    @property
    def used_bytes(self) -> int:
        return self.end - self.start


def parse_iram_layout(lines: Iterable[str]) -> IramLayout:
    """Parse the authoritative IRAM reservation symbols from a GNU ld map."""

    addresses: dict[str, tuple[int, int]] = {}
    for line_number, line in enumerate(lines, start=1):
        match = _LINKER_SYMBOL_RE.match(line)
        if match is None:
            continue

        address = int(match.group(1), 0)
        symbol = match.group(2)
        if symbol in addresses:
            previous_line = addresses[symbol][1]
            raise MapParseError(
                f"duplicate linker symbol {symbol} on lines "
                f"{previous_line} and {line_number}"
            )
        addresses[symbol] = (address, line_number)

    missing = [
        symbol for symbol in ("_iram_start", "_iram_end") if symbol not in addresses
    ]
    if missing:
        raise MapParseError(f"missing linker symbol(s): {', '.join(missing)}")

    layout = IramLayout(
        start=addresses["_iram_start"][0],
        end=addresses["_iram_end"][0],
    )
    if layout.end <= layout.start:
        raise MapParseError(
            "invalid IRAM span: "
            f"_iram_end 0x{layout.end:x} is not above "
            f"_iram_start 0x{layout.start:x}"
        )
    return layout


def _positive_integer(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"not an integer: {value}") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def _argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Check the complete ESP32-S3 instruction-RAM reservation from "
            "_iram_start through _iram_end."
        )
    )
    parser.add_argument("--link-map", required=True, type=Path)
    parser.add_argument(
        "--budget-bytes",
        required=True,
        type=_positive_integer,
        help="maximum allowed _iram_end - _iram_start span",
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = _argument_parser().parse_args(argv)
    try:
        with arguments.link_map.open(
            "r", encoding="utf-8", errors="replace"
        ) as map_file:
            layout = parse_iram_layout(map_file)
    except (OSError, MapParseError) as error:
        print(f"Node IRAM budget check failed: {error}", file=sys.stderr)
        return 2

    used_bytes = layout.used_bytes
    if used_bytes > arguments.budget_bytes:
        over_bytes = used_bytes - arguments.budget_bytes
        print(
            "Node IRAM budget exceeded: "
            f"{used_bytes} > {arguments.budget_bytes} bytes "
            f"({over_bytes} bytes over; span "
            f"0x{layout.start:x}-0x{layout.end:x})",
            file=sys.stderr,
        )
        return 1

    remaining_bytes = arguments.budget_bytes - used_bytes
    print(
        "Node IRAM budget passed: "
        f"{used_bytes} / {arguments.budget_bytes} bytes "
        f"({remaining_bytes} bytes remaining; span "
        f"0x{layout.start:x}-0x{layout.end:x})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
