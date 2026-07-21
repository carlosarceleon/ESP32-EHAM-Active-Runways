#!/usr/bin/env python3
"""Fetch a live dutchplanespotters EHAM runway response and save a sanitized
fixture for the host-side parser and firmware self-tests.

The response is public and contains no personal or credential data, so
"sanitizing" here only means trimming to a manageable, documented slice --
not redacting secrets.

Usage:
    python3 tools/capture_runway_fixture.py [YYYY-MM-DD] [--out fixtures/runway_current.json]
"""

import argparse
import datetime
import json
import sys
import urllib.request

API_BASE = "https://www.dutchplanespotters.nl/api/runways/ams"


def fetch(date_str: str) -> dict:
    url = f"{API_BASE}?date={date_str}"
    with urllib.request.urlopen(url, timeout=10) as resp:
        return json.load(resp)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("date", nargs="?", default=None, help="YYYY-MM-DD (default: today, UTC)")
    parser.add_argument("--out", default="fixtures/runway_current.json")
    args = parser.parse_args()

    date_str = args.date or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
    data = fetch(date_str)

    times = data.get("times", [])
    sanitized = {"times": times}

    with open(args.out, "w") as f:
        json.dump(sanitized, f, indent=2)
        f.write("\n")

    body = json.dumps(sanitized)
    print(f"Saved {len(times)} slot(s) for {date_str} to {args.out}")
    print(f"Sanitized body size: {len(body)} bytes")
    print("Observed fields per slot: from, until, landingRunways, departingRunways")
    print("Ignored top-level fields: title, peakTimes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
