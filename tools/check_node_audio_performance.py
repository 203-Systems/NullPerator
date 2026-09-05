#!/usr/bin/env python3
"""Check a continuous AUDIO_PERF serial capture from one ESP32-S3 boot."""

import argparse
import json
import re
import sys
from pathlib import Path

FIELDS = (
    "uptime_ms", "blocks", "frames", "max_render_us", "max_load_permille",
    "deadline_misses", "producer_starvations", "write_errors",
)


def read_metrics(text):
    rows = []
    for line in text.splitlines():
        if "AUDIO_PERF" not in line:
            continue
        pairs = re.findall(r"([a-z_]+)=(\d+)", line.split("AUDIO_PERF", 1)[1])
        row = {key: int(value) for key, value in pairs}
        if any(key not in row for key in FIELDS):
            raise ValueError("incomplete AUDIO_PERF record")
        if len(pairs) != len(row):
            raise ValueError("duplicate AUDIO_PERF field")
        if rows and any(row[key] < rows[-1][key] for key in FIELDS):
            raise ValueError("counters went backwards; use a capture from one boot without wrap")
        rows.append(row)
    if len(rows) < 2:
        raise ValueError("at least two complete AUDIO_PERF records are required")
    return rows


def evaluate(rows, min_seconds=300, max_load_percent=80):
    first, last = rows[0], rows[-1]
    seconds = (last["uptime_ms"] - first["uptime_ms"]) / 1000
    frames = last["frames"] - first["frames"]
    failures = []
    if seconds < min_seconds:
        failures.append(f"capture spans {seconds:g}s; need {min_seconds:g}s")
    if last["blocks"] == first["blocks"] or frames < seconds * 44100 * 0.95:
        failures.append("insufficient continuously rendered audio during capture")
    maximum = last["max_load_permille"] / 10
    if maximum > max_load_percent:
        failures.append(f"worst render load {maximum:g}% exceeds {max_load_percent:g}%")
    # Lifetime maxima and failures are intentionally conservative. Capturing
    # only the last minute must not hide an earlier fault in the same boot.
    for key in ("deadline_misses", "producer_starvations", "write_errors"):
        if last[key]:
            failures.append(f"{key}={last[key]}")
    return {
        "passed": not failures,
        "seconds": seconds,
        "rendered_frames": frames,
        "max_render_us": last["max_render_us"],
        "max_load_percent": maximum,
        "failures": failures,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="serial log path, or - for stdin")
    parser.add_argument("--min-seconds", type=float, default=300)
    parser.add_argument("--max-load-percent", type=float, default=80)
    args = parser.parse_args()
    if not 0 < args.min_seconds < float("inf") or not 0 < args.max_load_percent <= 100:
        parser.error("min-seconds must be finite and positive; max-load-percent must be in (0, 100]")
    try:
        text = sys.stdin.read() if args.log == "-" else Path(args.log).read_text(errors="replace")
        result = evaluate(read_metrics(text), args.min_seconds, args.max_load_percent)
    except (OSError, ValueError) as error:
        print(f"Cannot validate audio performance: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
