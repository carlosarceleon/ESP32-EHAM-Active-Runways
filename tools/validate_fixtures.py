#!/usr/bin/env python3
"""Validates fixtures/timestamps.txt against Python's stdlib ISO-8601 parsing.

This is an independent cross-check of the expected values in the fixtures
file (used to hand-verify the hardcoded vectors in
src/services/clock_service.cpp's runClockSelfTest) — it does not build or
run any device code.
"""

import re
import sys
from datetime import datetime
from pathlib import Path

FIXTURES_PATH = Path(__file__).resolve().parent.parent / "fixtures" / "timestamps.txt"

# Mirrors the grammar accepted by services::clock::parseIso8601ToUtc: a
# timezone (Z or +HH:MM/-HH:MM) is mandatory, unlike Python's own
# datetime.fromisoformat which happily accepts naive timestamps.
_PATTERN = re.compile(
    r"^\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})$"
)


def parse_iso8601(text: str) -> datetime | None:
    if not _PATTERN.match(text):
        return None
    normalized = text.replace("Z", "+00:00")
    try:
        return datetime.fromisoformat(normalized)
    except ValueError:
        return None


def main() -> int:
    lines = FIXTURES_PATH.read_text().splitlines()
    failures = 0
    checked = 0

    for lineno, raw_line in enumerate(lines, start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        parts = raw_line.rstrip("\n").split("\t")
        if len(parts) != 2:
            print(f"{FIXTURES_PATH}:{lineno}: expected 'input<TAB>expected', got: {raw_line!r}")
            failures += 1
            continue

        input_text, expected = parts
        checked += 1
        parsed = parse_iso8601(input_text)

        if expected == "INVALID":
            if parsed is not None:
                print(f"{FIXTURES_PATH}:{lineno}: expected INVALID for {input_text!r}, "
                      f"but it parsed as {parsed.isoformat()}")
                failures += 1
            continue

        if parsed is None:
            print(f"{FIXTURES_PATH}:{lineno}: expected {expected!r} for {input_text!r}, "
                  "but it failed to parse")
            failures += 1
            continue

        expected_dt = parse_iso8601(expected)
        if expected_dt is None or parsed.astimezone(expected_dt.tzinfo).replace(
            microsecond=0
        ) != expected_dt:
            print(f"{FIXTURES_PATH}:{lineno}: {input_text!r} parsed as {parsed.isoformat()}, "
                  f"expected {expected!r}")
            failures += 1

    if failures:
        print(f"\n{failures}/{checked} fixture(s) FAILED")
        return 1

    print(f"{checked}/{checked} fixtures OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
